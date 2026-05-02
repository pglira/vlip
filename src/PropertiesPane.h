#pragma once

#include "Project.h"
#include <QWidget>
#include <QUuid>

class QStackedWidget;
class QLabel;
class QDoubleSpinBox;
class QGroupBox;

namespace vlip {

class MainWindow;

class PropertiesPane : public QWidget {
    Q_OBJECT
public:
    explicit PropertiesPane(MainWindow* mw, QWidget* parent = nullptr);

public slots:
    void onSelectionChanged(const QUuid& id);
    void refresh();

private:
    void buildUi();
    Item* current();

    MainWindow* m_mw;
    QUuid m_id;
    bool m_suspend = false;

    // Common fields
    QLabel* m_filename;
    QLabel* m_timestamp;

    // Image fields
    QGroupBox* m_imgGroup;
    QDoubleSpinBox* m_imgDuration;

    // Video fields
    QGroupBox* m_vidGroup;
    QDoubleSpinBox* m_vidStart;
    QDoubleSpinBox* m_vidEnd;
    QLabel* m_vidInfo;

    QStackedWidget* m_stack;
};

} // namespace vlip
