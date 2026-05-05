#pragma once

#include "Project.hpp"

#include <QMainWindow>
#include <QUuid>
#include <QStringList>

class QDockWidget;
class QLabel;
class QMenu;
class QPushButton;

namespace vlip {

class TimelinePane;
class PropertiesPane;
class PreviewPane;
class DefaultsPane;
class MessagesPane;
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
    // Bulk removals — wired to the timeline pane's Remove submenu.
    // Both clear the selection if the previously-selected item is gone.
    void removeUnusedItems();
    void removeAllItems();
    void setSelected(const QUuid& id);
    void setUsed(const QUuid& id, bool used);
    void setSubtitle(const QUuid& id, const QString& s);
    void setImageClipDuration(const QUuid& id, double secs);
    void setImageClipCrop(const QUuid& id, const std::optional<QRectF>& rect);
    void setVideoClipTrim(const QUuid& id, double startSecs, double endSecs);
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

    void applyImageClipDurationToAll(double secs);
    void applyTextClipDurationToAll(double secs);

    // Background-music playlist mutations. Each emits projectChanged()
    // so the Project settings pane re-renders the list.
    void addMusicTrack(const QString& path);
    void removeMusicTrack(int index);
    void moveMusicTrack(int from, int to);

    // For each calendar day with at least one image / video clip in the
    // project (counted in the project's date-stamp time zone), insert a
    // text clip just before the day's first item with the date as its
    // text (DD.MM.YYYY). Idempotent: skips days that already have a text
    // clip with the matching date string.
    void insertDailyDateTextClips();

    // Crop UI: ask the preview pane to enter interactive crop mode for the
    // currently selected image clip.
    void beginImageClipCrop();

    // Move the timeline selection. Wired to Ctrl+Down / Ctrl+Up shortcuts
    // (Application-scope, so they fire from any focused widget).
    void selectNextItem();
    void selectPrevItem();
    void selectFirstItem();
    void selectLastItem();
    // Toggle the "used" flag on the selected item. Wired to Ctrl+Space.
    void toggleSelectedUsed();
    void removeSelectedItem();
    void addTextClipBeforeSelected();
    void addTextClipAfterSelected();
    void toggleHideUnused();
    void focusSubtitleEditor();

    // Project file lifecycle
    void newProject();
    void openProject();
    // Load `path` into the current MainWindow. Returns false (with an
    // error dialog) if the file is missing, unreadable, or fails parse.
    // Used by both the File → Open dialog and the CLI `vlip <path>`.
    bool loadProject(const QString& path);
    void saveProject();
    void saveProjectAs();

    void renderTo();    // prompt for path then render

signals:
    void projectChanged();
    // Emitted instead of projectChanged() for non-structural per-item
    // edits (toggle used, edit subtitle, change duration, etc.). Lets
    // panes update a single row instead of rebuilding everything — the
    // difference is dramatic on 1000+ item projects.
    void itemChanged(const QUuid& id);
    void selectionChanged(const QUuid& id);
    void message(const QString& msg, int level = 0);  // MessagesPane::Level

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
    // Per-item, non-structural change: emits itemChanged(id) only.
    void onItemMutated(const QUuid& id);
    QString defaultProjectsDir() const;

    Project m_project;
    QString m_projectPath;        // empty until first save
    QUuid m_selectedId;

    TimelinePane* m_timeline = nullptr;
    PropertiesPane* m_properties = nullptr;
    PreviewPane* m_preview = nullptr;
    DefaultsPane* m_defaults = nullptr;
    MessagesPane* m_messages = nullptr;

    QDockWidget* m_dockTimeline = nullptr;
    QDockWidget* m_dockProperties = nullptr;
    QDockWidget* m_dockPreview = nullptr;
    QDockWidget* m_dockDefaults = nullptr;
    QDockWidget* m_dockMessages = nullptr;

    Renderer* m_renderer = nullptr;
    QPushButton* m_cancelRenderBtn = nullptr;  // shown in menu bar's right corner only while rendering
    QMenu* m_recentMenu = nullptr;             // File → Open Recent submenu

    // Recent-projects MRU list, persisted via QSettings under
    // "recentProjects". Updated whenever a project is loaded or saved.
    static constexpr int kMaxRecentProjects = 10;
    void rebuildRecentProjectsMenu();
    void rememberRecentProject(const QString& path);
    QStringList loadRecentProjects() const;
    // Persist `list` and rebuild the submenu in lockstep — callers that
    // mutate the list always need both, so this helper prevents the
    // "saved but stale menu" footgun.
    void setRecentProjects(const QStringList& list);

    // Yes/No confirmation that fires before any action that would
    // discard the current in-memory project (New, Open, Open Recent,
    // Quit / window close). Default button is "No" so an accidental
    // Enter on the dialog doesn't destroy work.
    bool confirmDiscardCurrentProject(const QString& title);
};

} // namespace vlip
