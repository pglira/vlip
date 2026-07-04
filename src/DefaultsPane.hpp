#pragma once

#include <QWidget>
#include <QColor>
#include <QHash>
#include <QSet>
#include <QString>

class QSpinBox;
class QDoubleSpinBox;
class QFontComboBox;
class QComboBox;
class QPushButton;
class QCheckBox;
class QListWidget;
class QLineEdit;
class QLabel;
class QEvent;

namespace vlip {

class MainWindow;

class DefaultsPane : public QWidget {
    Q_OBJECT
public:
    explicit DefaultsPane(MainWindow* mw, QWidget* parent = nullptr);

public slots:
    void refresh();

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    void pickColor(QPushButton* btn, QColor& target, bool withAlpha,
                   const QString& title = {});
    void paintSwatch(QPushButton* btn, const QColor& c);
    // Push the music list's current visual order back to the project after
    // a drag-and-drop reorder.
    void commitMusicOrderFromList();
    // Update the Audio-tab summary: total playlist length vs. the timeline
    // length background music has to cover. Kicks off async duration probes
    // for any track whose length isn't cached yet.
    void refreshAudioSummary();
    // Timeline length music must cover: the frame-rounded duration of every
    // used, present, non-video clip (music pauses over video clips), matching
    // the renderer's per-clip duration.
    double musicCoverageSecs() const;

    MainWindow* m_mw;
    QComboBox *m_canvasPreset;
    QSpinBox *m_w, *m_h, *m_fps;
    QDoubleSpinBox *m_imageClipDuration;
    QDoubleSpinBox *m_transition;
    QFontComboBox *m_fontFamily;
    QSpinBox *m_fontSize;
    QPushButton *m_fontColor, *m_bgColor;
    QPushButton *m_outlineColor;
    QSpinBox *m_outlineWidth;
    QComboBox *m_position;
    QSpinBox *m_subtitleMargin;
    QDoubleSpinBox *m_subtitleDuration;
    // Text-clip defaults
    QFontComboBox *m_textClipFont;
    QSpinBox *m_textClipFontSize;
    QPushButton *m_textClipFontColor;
    QPushButton *m_textClipOutlineColor;
    QSpinBox *m_textClipOutlineWidth;
    QComboBox *m_textClipVAlign;
    QDoubleSpinBox *m_textClipDuration;
    // Date-stamp overlay
    QCheckBox *m_dsActive;
    QFontComboBox *m_dsFont;
    QSpinBox *m_dsFontSize;
    QComboBox *m_dsCorner;
    QSpinBox *m_dsMargin;
    QComboBox *m_timeZone;
    QLineEdit *m_dsFormat;
    // Background-music playlist
    QListWidget *m_musicList;
    QPushButton *m_musicAdd, *m_musicRemove, *m_musicUp, *m_musicDown;
    QLabel *m_audioSummary;
    // Cached track length in seconds, keyed by source path; paths currently
    // being probed off-thread. 0 means "probed but unknown/missing".
    QHash<QString, double> m_musicDurations;
    QSet<QString> m_musicProbing;
    // Automatic loudness levelling toggle. Drives the renderer's per-source
    // EBU R128 probe + bias pass.
    QCheckBox *m_audioLevellingActive;
    bool m_suspend = false;
};

} // namespace vlip
