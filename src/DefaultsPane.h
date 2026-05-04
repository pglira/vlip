#pragma once

#include <QWidget>
#include <QColor>

class QSpinBox;
class QDoubleSpinBox;
class QFontComboBox;
class QComboBox;
class QPushButton;
class QCheckBox;

namespace vlip {

class MainWindow;

class DefaultsPane : public QWidget {
    Q_OBJECT
public:
    explicit DefaultsPane(MainWindow* mw, QWidget* parent = nullptr);

public slots:
    void refresh();

private:
    void pickColor(QPushButton* btn, QColor& target, bool withAlpha);
    void paintSwatch(QPushButton* btn, const QColor& c);

    MainWindow* m_mw;
    QComboBox *m_canvasPreset;
    QSpinBox *m_w, *m_h, *m_fps;
    QDoubleSpinBox *m_imageClipDuration;
    QDoubleSpinBox *m_transition;
    QFontComboBox *m_fontFamily;
    QSpinBox *m_fontSize;
    QPushButton *m_fontColor, *m_bgColor;
    QComboBox *m_position;
    QDoubleSpinBox *m_subtitleDuration;
    // Text-clip defaults
    QFontComboBox *m_textClipFont;
    QSpinBox *m_textClipFontSize;
    QPushButton *m_textClipFontColor;
    QComboBox *m_textClipVAlign;
    QDoubleSpinBox *m_textClipDuration;
    // Date-stamp overlay
    QCheckBox *m_dsActive;
    QFontComboBox *m_dsFont;
    QSpinBox *m_dsFontSize;
    QComboBox *m_dsCorner;
    QSpinBox *m_dsMargin;
    QComboBox *m_timeZone;
    bool m_suspend = false;
};

} // namespace vlip
