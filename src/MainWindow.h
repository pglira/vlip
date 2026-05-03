#pragma once

#include "Project.h"

#include <QMainWindow>
#include <QUuid>
#include <QStringList>

class QDockWidget;
class QLabel;

namespace vlip {

class TimelinePane;
class PropertiesPane;
class PreviewPane;
class DefaultsPane;
class StatusPane;
class Renderer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    Project& project() { return m_project; }
    const Project& project() const { return m_project; }
    QUuid selectedId() const { return m_selectedId; }

    // Mutation API used by panes. These all emit projectChanged
    // and re-sort when needed.
    void importPaths(const QStringList& paths);
    void removeItem(const QUuid& id);
    void setSelected(const QUuid& id);
    void setUsed(const QUuid& id, bool used);
    void setSubtitle(const QUuid& id, const QString& s);
    void setImageDuration(const QUuid& id, double secs);
    void setImageCrop(const QUuid& id, const std::optional<QRectF>& rect);
    void setVideoTrim(const QUuid& id, double startSecs, double endSecs);
    void setTextClipText(const QUuid& id, const QString& text);
    void setTextClipDuration(const QUuid& id, double secs);
    void setTextClipBackground(const QUuid& id, const QString& path);
    enum class InsertPosition { Before, After };
    // Add a new text clip just before/after `referenceId`. If the id is
    // null, "Before" inserts at the start of the timeline and "After" at
    // the end. Returns the new clip's id.
    QUuid addTextClip(const QUuid& referenceId, InsertPosition pos);
    void setCanvas(int w, int h, int fps);
    void setDefaults(const Defaults& d);

    void applyImageDurationToAll(double secs);
    void applyTextClipDurationToAll(double secs);

    // Crop UI: ask the preview pane to enter interactive crop mode for the
    // currently selected image item.
    void beginImageCrop();

    // Project file lifecycle
    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();

    void renderTo();    // prompt for path then render

signals:
    void projectChanged();
    void selectionChanged(const QUuid& id);
    void statusMessage(const QString& msg, int level = 0);  // StatusPane::Level

protected:
    void closeEvent(QCloseEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    void setupPanes();
    void setupMenus();
    void persistLayout();
    void restoreLayoutAndGeometry();
    void resetLayoutToDefaults();
    Item* findItem(const QUuid& id);
    void onProjectMutated(bool resort);
    QString defaultProjectsDir() const;

    Project m_project;
    QString m_projectPath;        // empty until first save
    QUuid m_selectedId;

    TimelinePane* m_timeline = nullptr;
    PropertiesPane* m_properties = nullptr;
    PreviewPane* m_preview = nullptr;
    DefaultsPane* m_defaults = nullptr;
    StatusPane* m_status = nullptr;

    QDockWidget* m_dockTimeline = nullptr;
    QDockWidget* m_dockProperties = nullptr;
    QDockWidget* m_dockPreview = nullptr;
    QDockWidget* m_dockDefaults = nullptr;
    QDockWidget* m_dockStatus = nullptr;

    Renderer* m_renderer = nullptr;
};

} // namespace vlip
