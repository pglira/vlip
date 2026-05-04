#include "MainWindow.hpp"
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

namespace vlip {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("vlip — Slideshow Compiler"));
    setAcceptDrops(true);
    setDockNestingEnabled(true);
    resize(1400, 900);

    m_renderer = new Renderer(this);

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

    connect(m_renderer, &Renderer::log, this, [this](const QString& s) {
        emit message(s);
    });
    connect(m_renderer, &Renderer::finished, this, [this](bool ok, const QString& msg) {
        if (ok) {
            emit message(tr("Render complete: %1").arg(msg));
            QMessageBox::information(this, tr("Render"), tr("Render complete:\n%1").arg(msg));
        } else {
            emit message(tr("Render failed: %1").arg(msg), MessagesPane::Error);
            QMessageBox::critical(this, tr("Render failed"), msg);
        }
    });

    // Application-scope navigation shortcuts. Fire from any focused
    // widget; a focused QLineEdit / QSpinBox doesn't bind Ctrl+Up/Down,
    // so they don't get consumed by the editor.
    auto registerShortcut = [this](const QKeySequence& seq, void (MainWindow::*slot)()) {
        auto* sc = new QShortcut(seq, this);
        sc->setContext(Qt::ApplicationShortcut);
        connect(sc, &QShortcut::activated, this, slot);
    };
    // vim-style J/K alongside arrow keys: J = next (down), K = previous (up).
    registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_Down), &MainWindow::selectNextItem);
    registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_J),    &MainWindow::selectNextItem);
    registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_Up),   &MainWindow::selectPrevItem);
    registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_K),    &MainWindow::selectPrevItem);

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

    // Ctrl+P toggles the Project settings dock — same action the View
    // menu uses, so the menu also displays the shortcut next to the
    // entry.
    m_dockDefaults->toggleViewAction()->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
    m_dockDefaults->toggleViewAction()->setShortcutContext(Qt::ApplicationShortcut);

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

    auto* aNew = fileMenu->addAction(tr("&New project"));
    aNew->setShortcut(QKeySequence::New);
    connect(aNew, &QAction::triggered, this, &MainWindow::newProject);

    auto* aOpen = fileMenu->addAction(tr("&Open project…"));
    aOpen->setShortcut(QKeySequence::Open);
    connect(aOpen, &QAction::triggered, this, &MainWindow::openProject);

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
            m_project.sortChronologically();
            emit message(QString("Imported %1 file(s).").arg(added));
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
    emit projectChanged();
}

void MainWindow::onItemMutated(const QUuid& id) {
    emit itemChanged(id);
}

void MainWindow::removeItem(const QUuid& id) {
    int idx = m_project.indexOfId(id);
    if (idx < 0) return;
    m_project.items.remove(idx);
    if (m_selectedId == id) {
        m_selectedId = QUuid();
        emit selectionChanged(m_selectedId);
    }
    emit projectChanged();
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

    m_project.items.append(Item::makeTextClip(t));
    m_selectedId = t.common.id;
    onProjectMutated(true);
    emit selectionChanged(m_selectedId);
    return t.common.id;
}

void MainWindow::applyTextClipDurationToAll(double secs) {
    m_project.applyTextClipDurationAll(secs);
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
        // No selection: jump to the last visible item so Ctrl+K from
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

QString MainWindow::defaultProjectsDir() const {
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

void MainWindow::newProject() {
    m_project = Project();
    m_projectPath.clear();
    m_selectedId = QUuid();
    setWindowTitle(tr("vlip — (untitled)"));
    emit selectionChanged(m_selectedId);
    emit projectChanged();
    emit message(tr("New project."));
}

void MainWindow::openProject() {
    QString p = QFileDialog::getOpenFileName(this, tr("Open project"),
        defaultProjectsDir(), tr("vlip projects (*.vlip *.json);;All files (*.*)"));
    if (p.isEmpty()) return;
    Project np;
    QStringList warns;
    QString err;
    if (!ProjectIO::load(&np, p, &warns, &err)) {
        QMessageBox::critical(this, tr("Open failed"), err);
        return;
    }
    m_project = np;
    m_projectPath = p;
    m_selectedId = QUuid();
    setWindowTitle(QString("vlip — %1").arg(QFileInfo(p).fileName()));
    for (const auto& w : warns) emit message(w, MessagesPane::Warning);
    emit selectionChanged(m_selectedId);
    emit projectChanged();
    emit message(tr("Loaded %1 (%2 items)").arg(p).arg(m_project.items.size()));
}

void MainWindow::saveProject() {
    if (m_projectPath.isEmpty()) { saveProjectAs(); return; }
    QString err;
    if (!ProjectIO::save(m_project, m_projectPath, &err)) {
        QMessageBox::critical(this, tr("Save failed"), err);
        return;
    }
    emit message(tr("Saved %1").arg(m_projectPath));
}

void MainWindow::saveProjectAs() {
    QString p = QFileDialog::getSaveFileName(this, tr("Save project"),
        defaultProjectsDir() + "/untitled.vlip",
        tr("vlip projects (*.vlip);;JSON (*.json)"));
    if (p.isEmpty()) return;
    QString err;
    if (!ProjectIO::save(m_project, p, &err)) {
        QMessageBox::critical(this, tr("Save failed"), err);
        return;
    }
    m_projectPath = p;
    setWindowTitle(QString("vlip — %1").arg(QFileInfo(p).fileName()));
    emit message(tr("Saved %1").arg(p));
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
    m_renderer->start(m_project, out);
}

} // namespace vlip
