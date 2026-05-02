#pragma once

#include <QWidget>
#include <QUuid>

class QTreeWidget;
class QTreeWidgetItem;

namespace vlip {

class MainWindow;

class TimelinePane : public QWidget {
    Q_OBJECT
public:
    explicit TimelinePane(MainWindow* mw, QWidget* parent = nullptr);

public slots:
    void refresh();
    void selectId(const QUuid& id);

private slots:
    void onSelectionChanged();
    void onItemChanged(QTreeWidgetItem* it, int col);
    void onContextMenu(const QPoint& pt);

private:
    MainWindow* m_mw;
    QTreeWidget* m_tree;
    bool m_suspendSignals = false;
};

} // namespace vlip
