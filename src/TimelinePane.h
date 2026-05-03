#pragma once

#include <QWidget>
#include <QUuid>
#include <QHash>
#include <QIcon>

class QTreeWidget;
class QTreeWidgetItem;

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

private slots:
    void onSelectionChanged();
    void onItemChanged(QTreeWidgetItem* it, int col);
    void onContextMenu(const QPoint& pt);

private:
    QTreeWidgetItem* findRow(const QUuid& id) const;
    void populateRow(QTreeWidgetItem* row, const Item& it);
    QIcon iconForItem(const Item& it);

    MainWindow* m_mw;
    QTreeWidget* m_tree;
    // Keyed by thumbPath (or by a synthetic key for text clips). Avoids
    // re-reading + re-scaling the file on every refresh().
    QHash<QString, QIcon> m_iconCache;
    bool m_suspendSignals = false;
};

} // namespace vlip
