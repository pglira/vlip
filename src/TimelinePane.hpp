#pragma once

#include <QWidget>
#include <QUuid>
#include <QHash>
#include <QIcon>

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QCheckBox;
class QPushButton;
class QEvent;

namespace vlip {

class MainWindow;
struct Item;

class TimelinePane : public QWidget {
    Q_OBJECT
public:
    explicit TimelinePane(MainWindow* mw, QWidget* parent = nullptr);

public slots:
    void refresh();
    // Update only the row for `id` (no-op if it's not in the tree —
    // caller should fall back to refresh() for structural changes).
    void refreshRow(const QUuid& id);
    void selectId(const QUuid& id);

public:
    // Persist / restore the tree-header column widths. The bytes are
    // QHeaderView::saveState() output. Empty bytes on restore = leave the
    // current widths alone.
    QByteArray saveHeaderState() const;
    void restoreHeaderState(const QByteArray& state);

    // True when the timeline filters out items where used == false.
    // Persisted as a per-user QSettings preference.
    bool hideUnused() const { return m_hideUnused; }
    void setHideUnused(bool on);
    void toggleHideUnused() { setHideUnused(!m_hideUnused); }

protected:
    // Watches the tree viewport for a drag-and-drop reorder (InternalMove)
    // completing, so the resulting row order can be committed to the project.
    bool eventFilter(QObject* obj, QEvent* ev) override;

private slots:
    void onSelectionChanged();
    void onItemChanged(QTreeWidgetItem* it, int col);
    void onContextMenu(const QPoint& pt);

private:
    QTreeWidgetItem* findRow(const QUuid& id) const;
    void populateRow(QTreeWidgetItem* row, const Item& it);
    QIcon iconForItem(const Item& it);
    void updateSummary();
    // Rebuild the project's item order from the tree's current row order
    // after a drag-and-drop reorder.
    void commitItemOrderFromTree();
    // Enable/disable the reorder affordances (drag, Up/Down) and sync the
    // manual-order checkbox to the project. Reordering is offered only in
    // manual-order mode with the "Hide unused" filter off.
    void updateReorderControls();
    // Move the selected item one step; no-op outside manual-order mode.
    void moveSelected(int delta);
    // Clear the in-progress-drag guard and run any refresh deferred while
    // the drag held the tree's items.
    void endDrag();

    MainWindow* m_mw;
    QTreeWidget* m_tree;
    QLabel* m_summary;
    QCheckBox* m_chkHideUnused;
    QCheckBox* m_chkManualOrder;
    QPushButton* m_btnUp;
    QPushButton* m_btnDown;
    // Keyed by thumbPath (or by a synthetic key for text clips). Avoids
    // re-reading + re-scaling the file on every refresh().
    QHash<QString, QIcon> m_iconCache;
    bool m_hideUnused = false;
    bool m_suspendSignals = false;
    // While an internal-move drag holds the tree's items, refresh() must not
    // rebuild (it would delete the dragged item mid-drag). Refreshes asked
    // for during that window are deferred and run once the drag ends.
    bool m_dragInProgress = false;
    bool m_refreshPending = false;
};

} // namespace vlip
