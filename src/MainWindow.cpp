#include "MainWindow.hpp"
#include <algorithm>
#include <cmath>
#include "TimelinePane.hpp"
#include "PropertiesPane.hpp"
#include "PreviewPane.hpp"
#include "DefaultsPane.hpp"
#include "MessagesPane.hpp"
#include "Renderer.hpp"
#include "Importer.hpp"
#include "ProjectIO.hpp"

#include <QDockWidget>
#include <QMenuBar>
#include <QPushButton>
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QShortcut>
#include <QKeySequence>
#include <QSettings>
#include <QStandardPaths>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QStatusBar>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QApplication>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QTimeZone>

namespace vlip {

namespace {
// Fill in any empty font-family fields with the system UI font, so the
// DefaultsPane's QFontComboBox (initialised to QApplication::font())
// and the renderer (fc-match resolves the family name) agree from the
// start. Without this, an empty fontFamily makes the renderer fall
// through to its hard-coded DejaVu list while the UI displays the
// system default — they silently swap the moment the user touches any
// setting.
void applySystemFontDefaults(Defaults& d) {
    const QString sys = QApplication::font().family();
    if (d.subtitle.fontFamily.isEmpty())  d.subtitle.fontFamily  = sys;
    if (d.textClip.fontFamily.isEmpty())  d.textClip.fontFamily  = sys;
    if (d.datestamp.fontFamily.isEmpty()) d.datestamp.fontFamily = sys;
}
} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    updateWindowTitle();
    setAcceptDrops(true);
    setDockNestingEnabled(true);
    resize(1400, 900);

    m_renderer = new Renderer(this);

    applySystemFontDefaults(m_project.defaults);

    setupMenus();
    setupPanes();

    // Queued so that handlers triggered by the panes' own widgets
    // (e.g. a tree-item checkbox click → setUsed → projectChanged)
    // don't tear down the originating widget mid-signal.
    connect(this, &MainWindow::projectChanged, m_timeline, &TimelinePane::refresh,
            Qt::QueuedConnection);
    connect(this, &MainWindow::projectChanged, m_defaults, &DefaultsPane::refresh,
            Qt::QueuedConnection);
    connect(this, &MainWindow::projectChanged, m_properties, &PropertiesPane::refresh,
            Qt::QueuedConnection);
    connect(this, &MainWindow::projectChanged, m_preview, &PreviewPane::refresh,
            Qt::QueuedConnection);

    // Per-item changes: surgical update (no full pane rebuild). Queued so
    // we never tear the originating widget down mid-signal.
    connect(this, &MainWindow::itemChanged, m_timeline, &TimelinePane::refreshRow,
            Qt::QueuedConnection);
    connect(this, &MainWindow::itemChanged, m_properties, &PropertiesPane::onItemChanged,
            Qt::QueuedConnection);
    connect(this, &MainWindow::itemChanged, m_preview, &PreviewPane::onItemChanged,
            Qt::QueuedConnection);

    connect(this, &MainWindow::selectionChanged, m_properties, &PropertiesPane::onSelectionChanged);
    connect(this, &MainWindow::selectionChanged, m_preview, &PreviewPane::onSelectionChanged);
    connect(this, &MainWindow::selectionChanged, m_timeline, &TimelinePane::selectId);
    connect(this, &MainWindow::message, m_messages, &MessagesPane::appendMessage);

    // Keep order-dependent menu actions in sync with the project's
    // manual-order mode.
    connect(this, &MainWindow::projectChanged, this, &MainWindow::updateOrderDependentActions);
    updateOrderDependentActions();

    connect(m_renderer, &Renderer::log, this, [this](const QString& s) {
        emit message(s);
    });
    connect(m_renderer, &Renderer::finished, this, [this](bool ok, const QString& msg) {
        if (m_cancelRenderBtn) m_cancelRenderBtn->setVisible(false);
        if (ok) {
            emit message(tr("Render complete: %1").arg(msg));
            QMessageBox::information(this, tr("Render"), tr("Render complete:\n%1").arg(msg));
        } else {
            emit message(tr("Render failed: %1").arg(msg), MessagesPane::Error);
            QMessageBox::critical(this, tr("Render failed"), msg);
        }
    });
    connect(m_renderer, &Renderer::cancelled, this, [this]() {
        if (m_cancelRenderBtn) m_cancelRenderBtn->setVisible(false);
        emit message(tr("Render cancelled."));
    });

    // Application-scope navigation shortcuts. Fire from any focused
    // widget; a focused QLineEdit / QSpinBox doesn't bind Ctrl+Up/Down,
    // so they don't get consumed by the editor.
    auto registerShortcut = [this](const QKeySequence& seq, void (MainWindow::*slot)()) {
        auto* sc = new QShortcut(seq, this);
        sc->setContext(Qt::ApplicationShortcut);
        connect(sc, &QShortcut::activated, this, slot);
    };
    // N alongside arrow keys: N = next (down), Shift+N = previous (up).
    registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_Down),        &MainWindow::selectNextItem);
    registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_N),           &MainWindow::selectNextItem);
    registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_Up),          &MainWindow::selectPrevItem);
    registerShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N), &MainWindow::selectPrevItem);

    registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_Space),       &MainWindow::toggleSelectedUsed);
    registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_Home),        &MainWindow::selectFirstItem);
    registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_End),         &MainWindow::selectLastItem);
    registerShortcut(QKeySequence(Qt::Key_Delete),                 &MainWindow::removeSelectedItem);
    registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_T),           &MainWindow::addTextClipAfterSelected);
    registerShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T),
                     &MainWindow::addTextClipBeforeSelected);
    registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_H),           &MainWindow::toggleHideUnused);
    registerShortcut(QKeySequence(Qt::Key_F2),                     &MainWindow::focusSubtitleEditor);

    restoreLayoutAndGeometry();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupPanes() {
    m_timeline = new TimelinePane(this, this);
    m_properties = new PropertiesPane(this, this);
    m_preview = new PreviewPane(this, this);
    m_defaults = new DefaultsPane(this, this);
    m_messages = new MessagesPane(this, this);

    auto mkDock = [&](const QString& title, QWidget* w, const QString& objName,
                      Qt::DockWidgetArea area) {
        auto* dw = new QDockWidget(title, this);
        dw->setObjectName(objName);
        dw->setWidget(w);
        dw->setFeatures(QDockWidget::DockWidgetMovable
                        | QDockWidget::DockWidgetFloatable
                        | QDockWidget::DockWidgetClosable);
        addDockWidget(area, dw);
        return dw;
    };

    m_dockTimeline = mkDock(tr("Timeline"), m_timeline, "DockTimeline", Qt::LeftDockWidgetArea);
    m_dockPreview = mkDock(tr("Preview"), m_preview, "DockPreview", Qt::RightDockWidgetArea);
    m_dockProperties = mkDock(tr("Properties"), m_properties, "DockProperties", Qt::RightDockWidgetArea);
    m_dockDefaults = mkDock(tr("Project settings"), m_defaults, "DockDefaults", Qt::RightDockWidgetArea);
    m_dockMessages = mkDock(tr("Messages"), m_messages, "DockMessages", Qt::BottomDockWidgetArea);

    // Right column: Preview on top, Properties below (small).
    // Defaults is tabbed with Properties so the user can flip between them.
    splitDockWidget(m_dockPreview, m_dockProperties, Qt::Vertical);
    tabifyDockWidget(m_dockProperties, m_dockDefaults);
    m_dockProperties->raise();
    resizeDocks({m_dockPreview, m_dockProperties}, {800, 200}, Qt::Vertical);

    // Dock-toggle shortcuts. Application-scope so they fire even while
    // a text editor has focus, and they hang off the dock's
    // toggleViewAction so the View menu picks up the shortcut hint next
    // to each entry for free.
    auto bindDockToggle = [](QDockWidget* dock, const QKeySequence& seq) {
        QAction* a = dock->toggleViewAction();
        a->setShortcut(seq);
        a->setShortcutContext(Qt::ApplicationShortcut);
    };
    bindDockToggle(m_dockDefaults, QKeySequence(Qt::CTRL | Qt::Key_P));
    bindDockToggle(m_dockMessages, QKeySequence(Qt::CTRL | Qt::Key_M));

    // Provide a "View" menu listing each pane so the user can hide/show.
    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    for (QDockWidget* d : {m_dockTimeline, m_dockPreview, m_dockProperties,
                            m_dockDefaults, m_dockMessages}) {
        viewMenu->addAction(d->toggleViewAction());
    }
    viewMenu->addSeparator();
    auto* aReset = viewMenu->addAction(tr("Reset layout to defaults"));
    connect(aReset, &QAction::triggered, this, &MainWindow::resetLayoutToDefaults);
}

void MainWindow::resetLayoutToDefaults() {
    // Remove any prior placement and re-apply the defaults from setupPanes().
    for (auto* d : {m_dockTimeline, m_dockPreview, m_dockProperties,
                     m_dockDefaults, m_dockMessages}) {
        d->setFloating(false);
        d->show();
        removeDockWidget(d);
    }
    addDockWidget(Qt::LeftDockWidgetArea, m_dockTimeline);
    addDockWidget(Qt::RightDockWidgetArea, m_dockPreview);
    addDockWidget(Qt::RightDockWidgetArea, m_dockProperties);
    addDockWidget(Qt::RightDockWidgetArea, m_dockDefaults);
    addDockWidget(Qt::BottomDockWidgetArea, m_dockMessages);
    splitDockWidget(m_dockPreview, m_dockProperties, Qt::Vertical);
    tabifyDockWidget(m_dockProperties, m_dockDefaults);
    for (auto* d : {m_dockTimeline, m_dockPreview, m_dockProperties,
                     m_dockDefaults, m_dockMessages}) {
        d->show();
    }
    m_dockProperties->raise();
    resizeDocks({m_dockPreview, m_dockProperties}, {800, 200}, Qt::Vertical);
}

void MainWindow::setupMenus() {
    auto* fileMenu = menuBar()->addMenu(tr("&File"));

    // No Ctrl+N accelerator: that key is reserved for clip navigation
    // (next item). New project stays reachable via the File menu mnemonic.
    auto* aNew = fileMenu->addAction(tr("&New project"));
    connect(aNew, &QAction::triggered, this, &MainWindow::newProject);

    auto* aOpen = fileMenu->addAction(tr("&Open project…"));
    aOpen->setShortcut(QKeySequence::Open);
    connect(aOpen, &QAction::triggered, this, &MainWindow::openProject);

    // Note: 'r' is already taken by &Render, so use 't' as the access
    // key here ("Open recen&t"). With Qt::ToolTipsVisible the per-item
    // tooltips below actually show on hover.
    m_recentMenu = fileMenu->addMenu(tr("Open recen&t"));
    m_recentMenu->setToolTipsVisible(true);
    rebuildRecentProjectsMenu();

    auto* aSave = fileMenu->addAction(tr("&Save"));
    aSave->setShortcut(QKeySequence::Save);
    connect(aSave, &QAction::triggered, this, &MainWindow::saveProject);

    auto* aSaveAs = fileMenu->addAction(tr("Save &as…"));
    aSaveAs->setShortcut(QKeySequence::SaveAs);
    connect(aSaveAs, &QAction::triggered, this, &MainWindow::saveProjectAs);

    fileMenu->addSeparator();

    auto* aImport = fileMenu->addAction(tr("&Import media…"));
    aImport->setShortcut(QKeySequence("Ctrl+I"));
    connect(aImport, &QAction::triggered, this, [this]() {
        QStringList exts;
        for (auto& e : Importer::imageExtensions()) exts << ("*." + e);
        for (auto& e : Importer::videoExtensions()) exts << ("*." + e);
        auto paths = QFileDialog::getOpenFileNames(
            this, tr("Import media"), QString(),
            tr("Media (%1);;All files (*.*)").arg(exts.join(' ')));
        if (!paths.isEmpty()) importPaths(paths);
    });

    auto* aRender = fileMenu->addAction(tr("&Render…"));
    aRender->setShortcut(QKeySequence("Ctrl+R"));
    connect(aRender, &QAction::triggered, this, &MainWindow::renderTo);

    fileMenu->addSeparator();
    auto* aQuit = fileMenu->addAction(tr("&Quit"));
    aQuit->setShortcut(QKeySequence::Quit);
    connect(aQuit, &QAction::triggered, this, &QWidget::close);

    // Cancel-render button — sits in the menu bar's right corner and is
    // only visible while a render is in progress.
    m_cancelRenderBtn = new QPushButton(tr("Cancel render"), this);
    m_cancelRenderBtn->setVisible(false);
    menuBar()->setCornerWidget(m_cancelRenderBtn, Qt::TopRightCorner);
    connect(m_cancelRenderBtn, &QPushButton::clicked, this, [this]() {
        m_cancelRenderBtn->setEnabled(false);  // guard against double-click during the kill window
        m_renderer->cancel();
    });

    // Edit menu — bulk actions grouped by item kind. Each action prompts
    // for a duration. The prompt seeds with the last value the user
    // entered (per-user QSettings), so it's independent of project
    // defaults; the action does not change the defaults either.
    auto* editMenu = menuBar()->addMenu(tr("&Edit"));

    auto promptDuration = [this](const QString& title, const QString& label,
                                 const QString& settingsKey, double fallback) {
        QSettings s("vlip", "vlip");
        double seed = s.value(settingsKey, fallback).toDouble();
        bool ok = false;
        double v = QInputDialog::getDouble(this, title, label,
                                            seed, 0.1, 600.0, 2, &ok);
        if (!ok) return std::optional<double>{};
        s.setValue(settingsKey, v);
        return std::optional<double>{v};
    };

    auto* imagesMenu = editMenu->addMenu(tr("&Image clips"));
    auto* aImgApplyDur = imagesMenu->addAction(tr("Apply duration to all items…"));
    connect(aImgApplyDur, &QAction::triggered, this, [this, promptDuration]() {
        auto v = promptDuration(tr("Apply duration to all image clips"),
                                 tr("Duration (seconds):"),
                                 "edit/lastImageClipBulkDuration", 4.0);
        if (v) applyImageClipDurationToAll(*v);
    });

    auto* textMenu = editMenu->addMenu(tr("&Text clips"));
    auto* aTextApplyDur = textMenu->addAction(tr("Apply duration to all items…"));
    connect(aTextApplyDur, &QAction::triggered, this, [this, promptDuration]() {
        auto v = promptDuration(tr("Apply duration to all text clips"),
                                 tr("Duration (seconds):"),
                                 "edit/lastTextClipBulkDuration", 5.0);
        if (v) applyTextClipDurationToAll(*v);
    });
    auto* aTextApplyBg = textMenu->addAction(tr("Apply &background image to all items…"));
    connect(aTextApplyBg, &QAction::triggered, this, [this]() {
        QSettings s("vlip", "vlip");
        const QString seed = s.value("edit/lastTextClipBulkBgDir").toString();
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Choose background image for all text clips"), seed,
            tr("Images (*.jpg *.jpeg *.png *.heic *.heif *.webp *.tif *.tiff *.bmp);;All files (*.*)"));
        if (path.isEmpty()) return;
        s.setValue("edit/lastTextClipBulkBgDir", QFileInfo(path).absolutePath());
        applyTextClipBackgroundToAll(path);
    });
    auto* aTextClearBg = textMenu->addAction(tr("Clear background image on all items"));
    connect(aTextClearBg, &QAction::triggered, this, [this]() {
        applyTextClipBackgroundToAll(QString());
    });
    m_actDailyDates = textMenu->addAction(tr("Insert &date text clip for each day"));
    m_actDailyDates->setToolTip(tr(
        "For each calendar day with image / video clips, insert a text clip\n"
        "with the date (DD.MM.YYYY) just before the day's first clip.\n"
        "Days that already have a matching date text clip are skipped.\n"
        "Available only in date-order mode."));
    connect(m_actDailyDates, &QAction::triggered, this, &MainWindow::insertDailyDateTextClips);
}

void MainWindow::persistLayout() {
    QSettings s("vlip", "vlip");
    s.setValue("mainWindow/geometry", saveGeometry());
    s.setValue("mainWindow/state", saveState());
    if (m_timeline) {
        s.setValue("timeline/headerState", m_timeline->saveHeaderState());
    }
    s.setValue("project/lastPath", m_projectPath);
}

void MainWindow::restoreLayoutAndGeometry() {
    QSettings s("vlip", "vlip");
    auto g = s.value("mainWindow/geometry").toByteArray();
    auto st = s.value("mainWindow/state").toByteArray();
    if (!g.isEmpty()) restoreGeometry(g);
    if (!st.isEmpty()) restoreState(st);
    if (m_timeline) {
        m_timeline->restoreHeaderState(s.value("timeline/headerState").toByteArray());
    }
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (!confirmDiscardCurrentProject(tr("Quit"))) {
        e->ignore();
        return;
    }
    persistLayout();
    QMainWindow::closeEvent(e);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}
void MainWindow::dropEvent(QDropEvent* e) {
    QStringList paths;
    for (const QUrl& u : e->mimeData()->urls()) {
        if (u.isLocalFile()) paths << u.toLocalFile();
    }
    if (!paths.isEmpty()) importPaths(paths);
}

void MainWindow::importPaths(const QStringList& paths) {
    if (paths.isEmpty()) return;
    const int total = paths.size();
    emit message(QString("Importing %1 file(s)…").arg(total));

    // QtConcurrent::mapped runs Importer::importPath on the global thread
    // pool, one task per file. importPath shells out to ffprobe / ffmpeg
    // per file with no shared mutable state, so this parallelises cleanly.
    const double dur = m_project.defaults.imageClipDuration;
    auto worker = [dur](const QString& p) { return Importer::importPath(p, dur); };

    auto* watcher = new QFutureWatcher<ImportResult>(this);
    auto* lastBucket = new int(-1);   // shared between progress + finished

    connect(watcher, &QFutureWatcher<ImportResult>::progressValueChanged, this,
        [this, total, lastBucket](int v) {
            // 5 %-resolution buckets — enough to feel live without flooding
            // the log on a 2000-file import.
            int pct = (total > 0) ? int(100.0 * v / total) : 0;
            int bucket = pct / 5;
            if (bucket != *lastBucket) {
                *lastBucket = bucket;
                emit message(QString("Importing… %1% (%2 / %3)")
                    .arg(pct).arg(v).arg(total));
            }
        });

    connect(watcher, &QFutureWatcher<ImportResult>::finished, this,
        [this, watcher, lastBucket]() {
            int added = 0;
            const int n = watcher->future().resultCount();
            for (int i = 0; i < n; ++i) {
                const ImportResult& r = watcher->future().resultAt(i);
                if (!r.ok) {
                    emit message(tr("Skipped %1: %2")
                                       .arg(QFileInfo(r.error).fileName())
                                       .arg(r.error), MessagesPane::Warning);
                    continue;
                }
                m_project.items.append(r.item);
                ++added;
                if (!r.warning.isEmpty()) {
                    emit message(QString("%1: %2")
                        .arg(QFileInfo(r.item.common().sourcePath).fileName())
                        .arg(r.warning), MessagesPane::Warning);
                }
            }
            // In manual-order mode new items stay appended at the end;
            // otherwise they're merged into chronological order.
            if (!m_project.manualOrder) m_project.sortChronologically();
            emit message(QString("Imported %1 file(s).").arg(added));
            if (added > 0) markDirty();
            emit projectChanged();
            delete lastBucket;
            watcher->deleteLater();
        });

    watcher->setFuture(QtConcurrent::mapped(paths, worker));
}

Item* MainWindow::findItem(const QUuid& id) {
    int idx = m_project.indexOfId(id);
    return idx < 0 ? nullptr : &m_project.items[idx];
}

void MainWindow::onProjectMutated(bool resort) {
    if (resort) m_project.sortChronologically();
    markDirty();
    emit projectChanged();
}

void MainWindow::onItemMutated(const QUuid& id) {
    markDirty();
    emit itemChanged(id);
}

void MainWindow::updateOrderDependentActions() {
    if (m_actDailyDates) m_actDailyDates->setEnabled(!m_project.manualOrder);
}

void MainWindow::removeItem(const QUuid& id) {
    int idx = m_project.indexOfId(id);
    if (idx < 0) return;
    m_project.items.remove(idx);
    if (m_selectedId == id) {
        m_selectedId = QUuid();
        emit selectionChanged(m_selectedId);
    }
    markDirty();
    emit projectChanged();
}

void MainWindow::removeUnusedItems() {
    auto& items = m_project.items;
    auto newEnd = std::remove_if(items.begin(), items.end(),
        [](const Item& it) { return !it.common().used; });
    const int removed = int(items.end() - newEnd);
    if (removed == 0) return;
    items.erase(newEnd, items.end());
    if (!m_selectedId.isNull() && !findItem(m_selectedId)) {
        m_selectedId = QUuid();
        emit selectionChanged(m_selectedId);
    }
    onProjectMutated(false);
    emit message(tr("Removed %1 unused item(s).").arg(removed));
}

void MainWindow::removeAllItems() {
    if (m_project.items.isEmpty()) return;
    const int n = m_project.items.size();
    m_project.items.clear();
    if (!m_selectedId.isNull()) {
        m_selectedId = QUuid();
        emit selectionChanged(m_selectedId);
    }
    onProjectMutated(false);
    emit message(tr("Removed %1 item(s).").arg(n));
}

void MainWindow::setSelected(const QUuid& id) {
    if (m_selectedId == id) return;
    m_selectedId = id;
    emit selectionChanged(id);
}

void MainWindow::setUsed(const QUuid& id, bool used) {
    auto* it = findItem(id);
    if (!it) return;
    it->common().used = used;
    onItemMutated(id);
}

void MainWindow::setSubtitle(const QUuid& id, const QString& s) {
    auto* it = findItem(id);
    if (!it) return;
    it->common().subtitle = s;
    onItemMutated(id);
}

void MainWindow::setImageClipDuration(const QUuid& id, double secs) {
    auto* it = findItem(id);
    if (!it || it->kind != ItemKind::ImageClip) return;
    it->imageClip.durationSecs = std::max(0.05, secs);
    onItemMutated(id);
}

void MainWindow::setImageClipCrop(const QUuid& id, const std::optional<QRectF>& rect) {
    auto* it = findItem(id);
    if (!it || it->kind != ItemKind::ImageClip) return;
    it->imageClip.crop = rect;
    onItemMutated(id);
}

void MainWindow::setImageClipRotation(const QUuid& id, double degrees) {
    auto* it = findItem(id);
    if (!it || it->kind != ItemKind::ImageClip) return;
    // Wrap into (-180, 180] so the persisted value stays bounded regardless
    // of how many ±90 buttons the user mashes.
    double d = std::fmod(degrees, 360.0);
    if (d > 180.0) d -= 360.0;
    if (d <= -180.0) d += 360.0;
    it->imageClip.rotationDegrees = d;
    onItemMutated(id);
}

void MainWindow::setTextClipText(const QUuid& id, const QString& text) {
    auto* it = findItem(id);
    if (!it || it->kind != ItemKind::TextClip) return;
    it->textClip.text = text;
    onItemMutated(id);
}

void MainWindow::setTextClipDuration(const QUuid& id, double secs) {
    auto* it = findItem(id);
    if (!it || it->kind != ItemKind::TextClip) return;
    it->textClip.durationSecs = std::max(0.05, secs);
    onItemMutated(id);
}

void MainWindow::setTextClipBackground(const QUuid& id, const QString& path) {
    auto* it = findItem(id);
    if (!it || it->kind != ItemKind::TextClip) return;
    it->textClip.backgroundPath = path;
    onItemMutated(id);
}

QUuid MainWindow::addTextClip(const QUuid& referenceId, InsertPosition pos) {
    TextClip t;
    t.common.id = QUuid::createUuid();
    t.common.used = true;
    t.text = tr("Text");
    t.durationSecs = m_project.defaults.textClip.defaultDuration;

    // Synthesise a timestamp so the chronological sort places the new
    // clip on the requested side of the reference. 1 ms offset is enough
    // since timestamps have ms precision.
    QDateTime ts;
    int idx = referenceId.isNull() ? -1 : m_project.indexOfId(referenceId);
    if (idx >= 0) {
        ts = m_project.items[idx].common().timestamp
                .addMSecs(pos == InsertPosition::Before ? -1 : +1);
    } else if (!m_project.items.isEmpty()) {
        // No selection: anchor at the start or end of the timeline.
        const auto& anchor = (pos == InsertPosition::Before)
            ? m_project.items.first()
            : m_project.items.last();
        ts = anchor.common().timestamp
                .addMSecs(pos == InsertPosition::Before ? -1 : +1);
    } else {
        ts = QDateTime::currentDateTimeUtc();
    }
    ts.setTimeSpec(Qt::UTC);
    t.common.timestamp = ts;

    if (m_project.manualOrder) {
        // No resort will place the clip in manual mode, so insert it
        // directly on the requested side of the reference. The synthetic
        // timestamp above is still kept so the position survives a later
        // switch back to date-order.
        int insertAt;
        if (idx >= 0) {
            insertAt = (pos == InsertPosition::Before) ? idx : idx + 1;
        } else {
            insertAt = (pos == InsertPosition::Before) ? 0 : m_project.items.size();
        }
        m_project.items.insert(insertAt, Item::makeTextClip(t));
        m_selectedId = t.common.id;
        onProjectMutated(false);
    } else {
        m_project.items.append(Item::makeTextClip(t));
        m_selectedId = t.common.id;
        onProjectMutated(true);
    }
    emit selectionChanged(m_selectedId);
    return t.common.id;
}

void MainWindow::applyTextClipDurationToAll(double secs) {
    m_project.applyTextClipDurationAll(secs);
    onProjectMutated(false);
}

void MainWindow::applyTextClipBackgroundToAll(const QString& path) {
    m_project.applyTextClipBackgroundAll(path);
    onProjectMutated(false);
}

void MainWindow::beginImageClipCrop() {
    if (m_dockPreview) {
        m_dockPreview->show();   // un-hides the dock if the user closed it
        m_dockPreview->raise();  // brings it to the front of any tab group
    }
    if (m_preview) m_preview->beginImageClipCrop();
}

void MainWindow::selectNextItem() {
    if (m_project.items.isEmpty()) return;
    const bool skipUnused = m_timeline && m_timeline->hideUnused();
    auto matches = [&](int i) {
        return !skipUnused || m_project.items[i].common().used;
    };
    int idx = m_project.indexOfId(m_selectedId);
    int n = m_project.items.size();
    if (idx < 0) {
        for (int i = 0; i < n; ++i) {
            if (matches(i)) { setSelected(m_project.items[i].common().id); return; }
        }
        return;
    }
    for (int i = idx + 1; i < n; ++i) {
        if (matches(i)) { setSelected(m_project.items[i].common().id); return; }
    }
}

void MainWindow::selectPrevItem() {
    if (m_project.items.isEmpty()) return;
    const bool skipUnused = m_timeline && m_timeline->hideUnused();
    auto matches = [&](int i) {
        return !skipUnused || m_project.items[i].common().used;
    };
    int idx = m_project.indexOfId(m_selectedId);
    if (idx < 0) {
        // No selection: jump to the last visible item so Ctrl+Shift+N from
        // nowhere lands somewhere sensible.
        for (int i = m_project.items.size() - 1; i >= 0; --i) {
            if (matches(i)) { setSelected(m_project.items[i].common().id); return; }
        }
        return;
    }
    for (int i = idx - 1; i >= 0; --i) {
        if (matches(i)) { setSelected(m_project.items[i].common().id); return; }
    }
}

void MainWindow::selectFirstItem() {
    if (m_project.items.isEmpty()) return;
    const bool skipUnused = m_timeline && m_timeline->hideUnused();
    for (int i = 0; i < m_project.items.size(); ++i) {
        if (!skipUnused || m_project.items[i].common().used) {
            setSelected(m_project.items[i].common().id);
            return;
        }
    }
}

void MainWindow::selectLastItem() {
    if (m_project.items.isEmpty()) return;
    const bool skipUnused = m_timeline && m_timeline->hideUnused();
    for (int i = m_project.items.size() - 1; i >= 0; --i) {
        if (!skipUnused || m_project.items[i].common().used) {
            setSelected(m_project.items[i].common().id);
            return;
        }
    }
}

void MainWindow::toggleSelectedUsed() {
    if (m_selectedId.isNull()) return;
    auto* it = findItem(m_selectedId);
    if (!it) return;
    setUsed(m_selectedId, !it->common().used);
}

void MainWindow::removeSelectedItem() {
    if (m_selectedId.isNull()) return;
    removeItem(m_selectedId);
}

void MainWindow::addTextClipBeforeSelected() {
    addTextClip(m_selectedId, InsertPosition::Before);
}

void MainWindow::addTextClipAfterSelected() {
    addTextClip(m_selectedId, InsertPosition::After);
}

void MainWindow::toggleHideUnused() {
    if (m_timeline) m_timeline->toggleHideUnused();
}

void MainWindow::focusSubtitleEditor() {
    if (m_dockPreview) {
        m_dockPreview->show();
        m_dockPreview->raise();
    }
    if (m_preview) m_preview->focusSubtitleEditor();
}


void MainWindow::setVideoClipTrim(const QUuid& id, double startSecs, double endSecs) {
    auto* it = findItem(id);
    if (!it || it->kind != ItemKind::VideoClip) return;
    it->videoClip.startSecs = std::max(0.0, startSecs);
    it->videoClip.endSecs = std::max(it->videoClip.startSecs + 0.001, endSecs);
    onItemMutated(id);
}

void MainWindow::setCanvas(int w, int h, int fps) {
    m_project.canvas.width = w;
    m_project.canvas.height = h;
    m_project.canvas.fps = fps;
    onProjectMutated(false);
}

void MainWindow::setDefaults(const Defaults& d) {
    m_project.defaults = d;
    onProjectMutated(false);
}

void MainWindow::applyImageClipDurationToAll(double secs) {
    m_project.applyImageClipDurationAll(secs);
    onProjectMutated(false);
}

void MainWindow::addMusicTrack(const QString& path) {
    if (path.isEmpty()) return;
    m_project.backgroundMusic.append(path);
    onProjectMutated(false);
}

void MainWindow::removeMusicTrack(int index) {
    if (index < 0 || index >= m_project.backgroundMusic.size()) return;
    m_project.backgroundMusic.removeAt(index);
    onProjectMutated(false);
}

void MainWindow::moveMusicTrack(int from, int to) {
    const int n = m_project.backgroundMusic.size();
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;
    m_project.backgroundMusic.move(from, to);
    onProjectMutated(false);
}

void MainWindow::setMusicOrder(const QStringList& order) {
    if (order.size() != m_project.backgroundMusic.size()) return;
    if (order == m_project.backgroundMusic) return;
    m_project.backgroundMusic = order;
    onProjectMutated(false);
}

void MainWindow::moveItem(int from, int to) {
    if (!m_project.manualOrder) return;
    const int n = m_project.items.size();
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;
    m_project.items.move(from, to);
    onProjectMutated(false);
}

void MainWindow::setItemOrder(const QList<QUuid>& order) {
    if (!m_project.manualOrder) return;
    // A size mismatch means the list changed under the drag (an add/remove);
    // let the caller rebuild from the project.
    if (order.size() != m_project.items.size()) return;

    QHash<QUuid, int> indexById;
    indexById.reserve(m_project.items.size());
    for (int i = 0; i < m_project.items.size(); ++i) {
        indexById.insert(m_project.items[i].common().id, i);
    }

    QVector<Item> reordered;
    reordered.reserve(order.size());
    QSet<QUuid> seen;
    bool changed = false;
    for (int pos = 0; pos < order.size(); ++pos) {
        const auto it = indexById.constFind(order[pos]);
        if (it == indexById.constEnd() || seen.contains(order[pos])) {
            // `order` isn't a clean permutation of the current ids (an
            // unknown or duplicated id). Don't corrupt the list — re-emit so
            // the views snap back to the unchanged project order.
            emit projectChanged();
            return;
        }
        seen.insert(order[pos]);
        if (it.value() != pos) changed = true;
        reordered.append(m_project.items[it.value()]);
    }
    if (!changed) return;
    m_project.items.swap(reordered);
    onProjectMutated(false);
}

void MainWindow::setManualOrder(bool on) {
    if (m_project.manualOrder == on) return;
    m_project.manualOrder = on;
    // Leaving manual mode re-imposes chronological order at once; entering
    // it freezes the current order, so no resort is needed.
    onProjectMutated(!on);
}

void MainWindow::insertDailyDateTextClips() {
    // Day-boundary detection walks the items in chronological order; that
    // premise doesn't hold once the user takes over ordering manually.
    if (m_project.manualOrder) {
        emit message(tr("Date text clips can only be inserted in date-order mode."),
                     MessagesPane::Warning);
        return;
    }
    if (m_project.items.isEmpty()) {
        emit message(tr("No items in the project."));
        return;
    }

    // Group days by the project's date-stamp time zone (same convention
    // as the burned-in date stamp), so labels match what the renderer
    // overlays.
    QTimeZone tz = m_project.defaults.timeZone.isEmpty()
                       ? QTimeZone::systemTimeZone()
                       : QTimeZone(m_project.defaults.timeZone);
    if (!tz.isValid()) tz = QTimeZone::systemTimeZone();

    // Sort first so the iteration is in chronological order — that's what
    // the day-boundary detection relies on.
    m_project.sortChronologically();

    // Index of date strings already present as text clips, so re-running
    // the action is idempotent.
    QSet<QString> existingDateTexts;
    for (const auto& it : m_project.items) {
        if (it.kind == ItemKind::TextClip) {
            existingDateTexts.insert(it.textClip.text);
        }
    }

    // Snapshot the current items because we mutate m_project.items in the
    // loop below.
    const QVector<Item> originals = m_project.items;
    QString lastDay;
    int added = 0;
    for (const Item& it : originals) {
        // Day boundaries are derived from the actual media; text clips
        // (which carry synthesised timestamps) don't define a "day".
        if (it.kind == ItemKind::TextClip) continue;
        const QDateTime ts = it.common().timestamp;
        if (!ts.isValid()) continue;
        const QString day = ts.toTimeZone(tz).toString("dd.MM.yyyy");
        if (day == lastDay) continue;
        lastDay = day;
        if (existingDateTexts.contains(day)) continue;

        TextClip t;
        t.common.id = QUuid::createUuid();
        t.common.used = true;
        t.common.timestamp = ts.addMSecs(-1);   // sort places it just before the day's first item
        t.text = day;
        t.durationSecs = m_project.defaults.textClip.defaultDuration;
        m_project.items.append(Item::makeTextClip(t));
        existingDateTexts.insert(day);
        ++added;
    }

    if (added == 0) {
        emit message(tr("No date text clips to insert."));
        return;
    }
    onProjectMutated(true);
    emit message(tr("Inserted %1 date text clip(s).").arg(added));
}

QString MainWindow::defaultProjectsDir() const {
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

void MainWindow::newProject() {
    if (!confirmDiscardCurrentProject(tr("New project"))) return;
    m_project = Project();
    applySystemFontDefaults(m_project.defaults);
    m_projectPath.clear();
    m_selectedId = QUuid();
    markClean();
    updateWindowTitle();
    emit selectionChanged(m_selectedId);
    emit projectChanged();
    emit message(tr("New project."));
}

void MainWindow::openProject() {
    if (!confirmDiscardCurrentProject(tr("Open project"))) return;
    QString p = QFileDialog::getOpenFileName(this, tr("Open project"),
        defaultProjectsDir(), tr("vlip projects (*.vlip *.json);;All files (*.*)"));
    if (p.isEmpty()) return;
    loadProject(p);
}

bool MainWindow::loadProject(const QString& path) {
    Project np;
    QStringList warns;
    QString err;
    if (!ProjectIO::load(&np, path, &warns, &err)) {
        QMessageBox::critical(this, tr("Open failed"), err);
        return false;
    }
    m_project = np;
    applySystemFontDefaults(m_project.defaults);
    m_projectPath = path;
    m_selectedId = QUuid();
    markClean();
    updateWindowTitle();
    for (const auto& w : warns) emit message(w, MessagesPane::Warning);
    emit selectionChanged(m_selectedId);
    emit projectChanged();
    emit message(tr("Loaded %1 (%2 items)").arg(path).arg(m_project.items.size()));
    rememberRecentProject(path);
    return true;
}

bool MainWindow::saveProject() {
    if (m_projectPath.isEmpty()) return saveProjectAs();
    QString err;
    if (!ProjectIO::save(m_project, m_projectPath, &err)) {
        QMessageBox::critical(this, tr("Save failed"), err);
        return false;
    }
    markClean();
    emit message(tr("Saved %1").arg(m_projectPath));
    rememberRecentProject(m_projectPath);
    return true;
}

bool MainWindow::saveProjectAs() {
    QString p = QFileDialog::getSaveFileName(this, tr("Save project"),
        defaultProjectsDir() + "/untitled.vlip",
        tr("vlip projects (*.vlip);;JSON (*.json)"));
    if (p.isEmpty()) return false;
    QString err;
    if (!ProjectIO::save(m_project, p, &err)) {
        QMessageBox::critical(this, tr("Save failed"), err);
        return false;
    }
    m_projectPath = p;
    markClean();
    updateWindowTitle();
    emit message(tr("Saved %1").arg(p));
    rememberRecentProject(p);
    return true;
}

void MainWindow::markDirty() {
    setWindowModified(true);
}

void MainWindow::markClean() {
    setWindowModified(false);
}

void MainWindow::updateWindowTitle() {
    const QString name = m_projectPath.isEmpty()
        ? tr("(untitled)")
        : QFileInfo(m_projectPath).fileName();
    // The "[*]" placeholder renders as "*" while the project is modified
    // and disappears once it is saved (driven by setWindowModified).
    setWindowTitle(tr("vlip — %1[*]").arg(name));
}

bool MainWindow::confirmDiscardCurrentProject(const QString& title) {
    // Only interrupt the user when there are unsaved changes to lose.
    if (!isDirty()) return true;
    const auto btn = QMessageBox::warning(this, title,
        tr("The current project has unsaved changes.\n"
           "Do you want to save them first?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    switch (btn) {
        case QMessageBox::Save:    return saveProject();  // proceed only if the save actually succeeds
        case QMessageBox::Discard: return true;
        default:                   return false;          // Cancel / dialog dismissed
    }
}

QStringList MainWindow::loadRecentProjects() const {
    QSettings s("vlip", "vlip");
    return s.value("recentProjects").toStringList();
}

void MainWindow::setRecentProjects(const QStringList& list) {
    QSettings("vlip", "vlip").setValue("recentProjects", list);
    rebuildRecentProjectsMenu();
}

void MainWindow::rememberRecentProject(const QString& path) {
    const QString abs = QFileInfo(path).absoluteFilePath();
    QStringList list = loadRecentProjects();
    list.removeAll(abs);
    list.prepend(abs);
    while (list.size() > kMaxRecentProjects) list.removeLast();
    setRecentProjects(list);
}

void MainWindow::rebuildRecentProjectsMenu() {
    if (!m_recentMenu) return;
    m_recentMenu->clear();
    const QStringList list = loadRecentProjects();
    if (list.isEmpty()) {
        QAction* empty = m_recentMenu->addAction(tr("(no recent projects)"));
        empty->setEnabled(false);
        return;
    }
    // Numbers 1..9 are access keys; entries past 9 are mouse-only since
    // a QMenu access key is a single character.
    for (int i = 0; i < list.size(); ++i) {
        const QString p = list[i];
        // '&' is the menu access-key marker — escape it so a path like
        // "AT&T project.vlip" renders verbatim instead of stealing 'T'.
        const QString safeName = QFileInfo(p).fileName().replace('&', "&&");
        const QString label = (i < 9)
            ? QString("&%1  %2").arg(i + 1).arg(safeName)
            : QString("    %1").arg(safeName);
        QAction* a = m_recentMenu->addAction(label);
        a->setToolTip(p);
        connect(a, &QAction::triggered, this, [this, p]() {
            if (!QFileInfo::exists(p)) {
                emit message(tr("Recent project no longer exists, removing: %1").arg(p),
                             MessagesPane::Warning);
                QStringList l = loadRecentProjects();
                l.removeAll(p);
                setRecentProjects(l);
                return;
            }
            if (!confirmDiscardCurrentProject(tr("Open recent"))) return;
            loadProject(p);
        });
    }
    m_recentMenu->addSeparator();
    QAction* clear = m_recentMenu->addAction(tr("&Clear list"));
    connect(clear, &QAction::triggered, this, [this]() {
        setRecentProjects({});
    });
}

void MainWindow::renderTo() {
    QString err = Renderer::validate(m_project);
    if (!err.isEmpty()) {
        QMessageBox::warning(this, tr("Cannot render"), err);
        return;
    }
    QString def = m_projectPath.isEmpty()
        ? (defaultProjectsDir() + "/output.mp4")
        : (QFileInfo(m_projectPath).absolutePath() + "/" +
           QFileInfo(m_projectPath).completeBaseName() + ".mp4");
    QString out = QFileDialog::getSaveFileName(this, tr("Render to MP4"),
        def, tr("MP4 (*.mp4)"));
    if (out.isEmpty()) return;
    if (!out.endsWith(".mp4", Qt::CaseInsensitive)) out += ".mp4";
    emit message(tr("Rendering to %1…").arg(out));
    m_cancelRenderBtn->setEnabled(true);
    m_cancelRenderBtn->setVisible(true);
    m_renderer->start(m_project, out);  // synchronous validation failure will hide the button via finished()
}

} // namespace vlip
