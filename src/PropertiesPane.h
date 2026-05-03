#pragma once

#include "Project.h"
#include <QWidget>
#include <QUuid>

class QStackedWidget;
class QLabel;
class QLineEdit;
class QDoubleSpinBox;
class QPushButton;
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
    // Refresh only when the changed item is the one we're showing.
    void onItemChanged(const QUuid& id);

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
    QPushButton* m_btnCrop;
    QPushButton* m_btnClearCrop;
    QLabel* m_cropLabel;

    // Video fields
    QGroupBox* m_vidGroup;
    QDoubleSpinBox* m_vidStart;
    QDoubleSpinBox* m_vidEnd;
    QLabel* m_vidInfo;

    // Text-clip fields
    QGroupBox* m_textGroup;
    QDoubleSpinBox* m_textDuration;
    QLineEdit* m_textBgPath;
    QPushButton* m_textBrowseBg;
    QPushButton* m_textClearBg;

    QStackedWidget* m_stack;
};

} // namespace vlip
