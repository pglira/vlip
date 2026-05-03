#include "MainWindow.h"
#include "TimelinePane.h"
#include "PropertiesPane.h"
#include "PreviewPane.h"
#include "DefaultsPane.h"
#include "StatusPane.h"
#include "Renderer.h"
#include "Importer.h"
#include "ProjectIO.h"

#include <QDockWidget>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
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
    connect(this, &MainWindow::selectionChanged, m_properties, &PropertiesPane::onSelectionChanged);
    connect(this, &MainWindow::selectionChanged, m_preview, &PreviewPane::onSelectionChanged);
    connect(this, &MainWindow::selectionChanged, m_timeline, &TimelinePane::selectId);
    connect(this, &MainWindow::statusMessage, m_status, &StatusPane::appendMessage);

    connect(m_renderer, &Renderer::log, this, [this](const QString& s) {
        emit statusMessage(s);
    });
    connect(m_renderer, &Renderer::finished, this, [this](bool ok, const QString& msg) {
        if (ok) {
            emit statusMessage(tr("Render complete: %1").arg(msg));
            QMessageBox::information(this, tr("Render"), tr("Render complete:\n%1").arg(msg));
        } else {
            emit statusMessage(tr("Render failed: %1").arg(msg), StatusPane::Error);
            QMessageBox::critical(this, tr("Render failed"), msg);
        }
    });

    restoreLayoutAndGeometry();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupPanes() {
    m_timeline = new TimelinePane(this, this);
    m_properties = new PropertiesPane(this, this);
    m_preview = new PreviewPane(this, this);
    m_defaults = new DefaultsPane(this, this);
    m_status = new StatusPane(this, this);

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
    m_dockDefaults = mkDock(tr("Project defaults"), m_defaults, "DockDefaults", Qt::RightDockWidgetArea);
    m_dockStatus = mkDock(tr("Status / progress"), m_status, "DockStatus", Qt::BottomDockWidgetArea);

    // Right column: Preview on top, Properties below (small).
    // Defaults is tabbed with Properties so the user can flip between them.
    splitDockWidget(m_dockPreview, m_dockProperties, Qt::Vertical);
    tabifyDockWidget(m_dockProperties, m_dockDefaults);
    m_dockProperties->raise();
    resizeDocks({m_dockPreview, m_dockProperties}, {800, 200}, Qt::Vertical);

    // Provide a "View" menu listing each pane so the user can hide/show.
    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    for (QDockWidget* d : {m_dockTimeline, m_dockPreview, m_dockProperties,
                            m_dockDefaults, m_dockStatus}) {
        viewMenu->addAction(d->toggleViewAction());
    }
    viewMenu->addSeparator();
    auto* aReset = viewMenu->addAction(tr("Reset layout to defaults"));
    connect(aReset, &QAction::triggered, this, &MainWindow::resetLayoutToDefaults);
}

void MainWindow::resetLayoutToDefaults() {
    // Remove any prior placement and re-apply the defaults from setupPanes().
    for (auto* d : {m_dockTimeline, m_dockPreview, m_dockProperties,
                     m_dockDefaults, m_dockStatus}) {
        d->setFloating(false);
        d->show();
        removeDockWidget(d);
    }
    addDockWidget(Qt::LeftDockWidgetArea, m_dockTimeline);
    addDockWidget(Qt::RightDockWidgetArea, m_dockPreview);
    addDockWidget(Qt::RightDockWidgetArea, m_dockProperties);
    addDockWidget(Qt::RightDockWidgetArea, m_dockDefaults);
    addDockWidget(Qt::BottomDockWidgetArea, m_dockStatus);
    splitDockWidget(m_dockPreview, m_dockProperties, Qt::Vertical);
    tabifyDockWidget(m_dockProperties, m_dockDefaults);
    for (auto* d : {m_dockTimeline, m_dockPreview, m_dockProperties,
                     m_dockDefaults, m_dockStatus}) {
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
}

void MainWindow::persistLayout() {
    QSettings s("vlip", "vlip");
    s.setValue("mainWindow/geometry", saveGeometry());
    s.setValue("mainWindow/state", saveState());
    s.setValue("project/lastPath", m_projectPath);
}

void MainWindow::restoreLayoutAndGeometry() {
    QSettings s("vlip", "vlip");
    auto g = s.value("mainWindow/geometry").toByteArray();
    auto st = s.value("mainWindow/state").toByteArray();
    if (!g.isEmpty()) restoreGeometry(g);
    if (!st.isEmpty()) restoreState(st);
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
    emit statusMessage(QString("Importing %1 file(s)…").arg(paths.size()));
    auto* watcher = new QFutureWatcher<QVector<ImportResult>>(this);
    QFuture<QVector<ImportResult>> fut = QtConcurrent::run([this, paths]() {
        QVector<ImportResult> out;
        out.reserve(paths.size());
        double dur = m_project.defaults.imageDuration;
        for (const auto& p : paths) {
            out << Importer::importPath(p, dur);
        }
        return out;
    });
    connect(watcher, &QFutureWatcher<QVector<ImportResult>>::finished, this,
        [this, watcher]() {
            auto results = watcher->result();
            int added = 0;
            for (const auto& r : results) {
                if (!r.ok) {
                    emit statusMessage(tr("Skipped %1: %2")
                                       .arg(QFileInfo(r.error).fileName())
                                       .arg(r.error), StatusPane::Warning);
                    continue;
                }
                m_project.items.append(r.item);
                ++added;
                if (!r.warning.isEmpty()) {
                    emit statusMessage(QString("%1: %2")
                        .arg(QFileInfo(r.item.common().sourcePath).fileName())
                        .arg(r.warning), StatusPane::Warning);
                }
            }
            m_project.sortChronologically();
            emit statusMessage(QString("Imported %1 file(s).").arg(added));
            emit projectChanged();
            watcher->deleteLater();
        });
    watcher->setFuture(fut);
}

Item* MainWindow::findItem(const QUuid& id) {
    int idx = m_project.indexOfId(id);
    return idx < 0 ? nullptr : &m_project.items[idx];
}

void MainWindow::onProjectMutated(bool resort) {
    if (resort) m_project.sortChronologically();
    emit projectChanged();
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
    onProjectMutated(false);
}

void MainWindow::setSubtitle(const QUuid& id, const QString& s) {
    auto* it = findItem(id);
    if (!it) return;
    it->common().subtitle = s;
    onProjectMutated(false);
}

void MainWindow::setImageDuration(const QUuid& id, double secs) {
    auto* it = findItem(id);
    if (!it || it->kind != ItemKind::Image) return;
    it->image.durationSecs = std::max(0.05, secs);
    onProjectMutated(false);
}

void MainWindow::setImageCrop(const QUuid& id, const std::optional<QRectF>& rect) {
    auto* it = findItem(id);
    if (!it || it->kind != ItemKind::Image) return;
    it->image.crop = rect;
    onProjectMutated(false);
}

void MainWindow::beginImageCrop() {
    if (m_dockPreview) {
        m_dockPreview->show();   // un-hides the dock if the user closed it
        m_dockPreview->raise();  // brings it to the front of any tab group
    }
    if (m_preview) m_preview->beginImageCrop();
}

void MainWindow::setVideoTrim(const QUuid& id, double startSecs, double endSecs) {
    auto* it = findItem(id);
    if (!it || it->kind != ItemKind::Video) return;
    it->video.startSecs = std::max(0.0, startSecs);
    it->video.endSecs = std::max(it->video.startSecs + 0.001, endSecs);
    onProjectMutated(false);
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

void MainWindow::applyImageDurationToAll(double secs) {
    m_project.applyImageDurationAll(secs);
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
    emit statusMessage(tr("New project."));
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
    for (const auto& w : warns) emit statusMessage(w, StatusPane::Warning);
    emit selectionChanged(m_selectedId);
    emit projectChanged();
    emit statusMessage(tr("Loaded %1 (%2 items)").arg(p).arg(m_project.items.size()));
}

void MainWindow::saveProject() {
    if (m_projectPath.isEmpty()) { saveProjectAs(); return; }
    QString err;
    if (!ProjectIO::save(m_project, m_projectPath, &err)) {
        QMessageBox::critical(this, tr("Save failed"), err);
        return;
    }
    emit statusMessage(tr("Saved %1").arg(m_projectPath));
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
    emit statusMessage(tr("Saved %1").arg(p));
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
    emit statusMessage(tr("Rendering to %1…").arg(out));
    m_renderer->start(m_project, out);
}

} // namespace vlip
