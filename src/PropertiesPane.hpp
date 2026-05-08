#pragma once

#include "Project.hpp"
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

    // Image-clip fields
    QGroupBox* m_imageClipGroup;
    QDoubleSpinBox* m_imageClipDuration;
    QDoubleSpinBox* m_imageClipRotation;
    QPushButton* m_btnCrop;
    QPushButton* m_btnClearCrop;
    QLabel* m_cropLabel;

    // Video-clip fields
    QGroupBox* m_videoClipGroup;
    QDoubleSpinBox* m_videoClipStart;
    QDoubleSpinBox* m_videoClipEnd;
    QLabel* m_videoClipInfo;

    // Text-clip fields
    QGroupBox* m_textClipGroup;
    QDoubleSpinBox* m_textClipDuration;
    QLineEdit* m_textClipBgPath;
    QPushButton* m_textClipBrowseBg;
    QPushButton* m_textClipClearBg;

    QStackedWidget* m_stack;
};

} // namespace vlip
