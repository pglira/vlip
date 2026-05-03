#pragma once

#include "Project.h"

#include <QWidget>
#include <QUuid>
#include <QMediaPlayer>
#include <QSlider>

class QLabel;
class QPushButton;
class QAudioOutput;
class QVideoWidget;

namespace vlip {

// Timeline slider for video preview. Owns its mouse handling, so any
// user interaction (click *or* drag) emits a single seekRequested(ms)
// signal. setPositionMs() is the inverse direction (player → slider) and
// is ignored while the user is dragging.
//
// The slider's value is in milliseconds (range = [0, totalMs]); markers
// for trim-start / trim-end are painted over the groove.
class TrimSlider : public QSlider {
    Q_OBJECT
public:
    explicit TrimSlider(QWidget* parent = nullptr);

    void setTotalMs(qint64 totalMs);
    void setMarkers(qint64 startMs, qint64 endMs);
    void clearMarkers();
    void setPositionMs(qint64 ms);
    bool isDragging() const { return m_dragging; }

signals:
    void seekRequested(qint64 ms);

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void paintEvent(QPaintEvent* e) override;

private:
    qint64 msAtX(int x) const;
    void seekToX(int x);

    bool m_dragging = false;
    qint64 m_startMs = -1, m_endMs = -1;
};

class MainWindow;

// Video preview built on Qt 6 Multimedia (QMediaPlayer + QAudioOutput +
// QVideoWidget). The Qt backend handles A/V sync, codec support, container
// rotation, and scrubbing natively.
class VideoPreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit VideoPreviewWidget(MainWindow* mw, QWidget* parent = nullptr);
    ~VideoPreviewWidget() override;

    void setItem(const QUuid& id);
    void clear();

private slots:
    void onPositionChanged(qint64 ms);
    void onDurationChanged(qint64 ms);
    void onPlaybackStateChanged(QMediaPlayer::PlaybackState s);
    void onMediaStatusChanged(QMediaPlayer::MediaStatus s);
    void onError(QMediaPlayer::Error err, const QString& msg);

private:
    void rebuildLabels();
    void setPlayButtonText();

    // Set on setItem(); applied as setPosition() once the media reports
    // BufferedMedia so a later Play click starts from the trim-start.
    // Does NOT trigger any playback.
    qint64 m_seekOnLoadMs = -1;

    MainWindow* m_mw;
    QUuid m_id;
    QMediaPlayer* m_player;
    QAudioOutput* m_audio;
    QVideoWidget* m_videoWidget;
    TrimSlider* m_scrub;
    QLabel* m_pos;
    QLabel* m_dur;
    QLabel* m_trim;
    QPushButton *m_btnPlay, *m_btnHome, *m_btnEnd;
    QPushButton *m_btnGoStart, *m_btnGoEnd;
    QPushButton *m_btnSetStart, *m_btnSetEnd;
    qint64 m_durationMs = 0;
    QString m_loadedPath;       // currently loaded media URL path
};

} // namespace vlip
