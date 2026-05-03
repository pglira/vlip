#include "VideoPreviewWidget.h"
#include "MainWindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QAudioOutput>
#include <QVideoWidget>
#include <QUrl>
#include <QPalette>
#include <QPainter>
#include <QStyleOptionSlider>
#include <QStyle>
#include <QMouseEvent>
#include <QShortcut>
#include <QSignalBlocker>
#include <QDebug>

#include <limits>

namespace vlip {

TrimSlider::TrimSlider(QWidget* parent) : QSlider(Qt::Horizontal, parent) {
    setRange(0, 0);
    setSingleStep(1000);     // arrow keys step 1 s
    setPageStep(5000);       // page keys step 5 s
    setTracking(false);      // we drive value updates ourselves
}

void TrimSlider::setTotalMs(qint64 totalMs) {
    if (totalMs < 0) totalMs = 0;
    if (totalMs > std::numeric_limits<int>::max()) totalMs = std::numeric_limits<int>::max();
    setRange(0, int(totalMs));
}

void TrimSlider::setMarkers(qint64 startMs, qint64 endMs) {
    if (m_startMs == startMs && m_endMs == endMs) return;
    m_startMs = startMs; m_endMs = endMs;
    update();
}

void TrimSlider::clearMarkers() {
    if (m_startMs < 0 && m_endMs < 0) return;
    m_startMs = -1; m_endMs = -1;
    update();
}

void TrimSlider::setPositionMs(qint64 ms) {
    if (m_dragging) return;            // user is authoritative while dragging
    int v = int(std::clamp<qint64>(ms, 0, maximum()));
    if (value() == v) return;
    QSignalBlocker block(this);        // don't echo back as a user-seek
    setValue(v);
}

qint64 TrimSlider::msAtX(int x) const {
    QStyleOptionSlider opt;
    initStyleOption(&opt);
    QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt,
                                           QStyle::SC_SliderGroove, this);
    QRect handle = style()->subControlRect(QStyle::CC_Slider, &opt,
                                           QStyle::SC_SliderHandle, this);
    int span = std::max(1, groove.width() - handle.width());
    int x0 = groove.left() + handle.width() / 2;
    int relX = std::clamp(x - x0, 0, span);
    return QStyle::sliderValueFromPosition(0, maximum(), relX, span, opt.upsideDown);
}

void TrimSlider::seekToX(int x) {
    qint64 ms = msAtX(x);
    int v = int(std::clamp<qint64>(ms, 0, maximum()));
    {
        QSignalBlocker block(this);
        setValue(v);
    }
    emit seekRequested(ms);
}

void TrimSlider::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && maximum() > 0) {
        m_dragging = true;
        seekToX(e->pos().x());
        e->accept();
        return;
    }
    QSlider::mousePressEvent(e);
}

void TrimSlider::mouseMoveEvent(QMouseEvent* e) {
    if (m_dragging) {
        seekToX(e->pos().x());
        e->accept();
        return;
    }
    QSlider::mouseMoveEvent(e);
}

void TrimSlider::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        e->accept();
        return;
    }
    QSlider::mouseReleaseEvent(e);
}

void TrimSlider::paintEvent(QPaintEvent* e) {
    QSlider::paintEvent(e);
    qint64 totalMs = maximum();
    if (totalMs <= 0 || (m_startMs < 0 && m_endMs < 0)) return;

    QStyleOptionSlider opt;
    initStyleOption(&opt);
    QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt,
                                           QStyle::SC_SliderGroove, this);
    QRect handle = style()->subControlRect(QStyle::CC_Slider, &opt,
                                           QStyle::SC_SliderHandle, this);
    if (groove.isEmpty()) return;

    // Map ms → screen X using the same math QSlider uses for its handle, so
    // markers and the playhead share an exact coordinate system.
    const int span = groove.width() - handle.width();
    const int x0 = groove.left() + handle.width() / 2;
    auto xForMs = [&](qint64 ms) {
        qint64 v = std::clamp<qint64>(ms, 0, totalMs);
        int p = QStyle::sliderPositionFromValue(
            0, int(totalMs), int(v), span, opt.upsideDown);
        return std::clamp(x0 + p, 0, width() - 1);
    };

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    if (m_startMs >= 0 && m_endMs >= 0 && m_endMs > m_startMs) {
        int xs = xForMs(m_startMs);
        int xe = xForMs(m_endMs);
        QRect band(xs, groove.top() + 1, std::max(1, xe - xs), groove.height() - 2);
        p.fillRect(band, QColor(60, 200, 120, 90));
    }

    auto drawMarker = [&](int x, const QColor& col) {
        // 3-px-wide bar that extends a bit above and below the groove so it
        // remains visible at the edges and over the handle's outline.
        int top = std::max(0, groove.top() - 5);
        int bot = std::min(height() - 1, groove.bottom() + 5);
        QRect r(std::clamp(x - 1, 0, width() - 3), top, 3, bot - top + 1);
        p.fillRect(r, col);
    };
    if (m_startMs >= 0) drawMarker(xForMs(m_startMs), QColor(60, 200, 120));
    if (m_endMs   >= 0) drawMarker(xForMs(m_endMs),   QColor(220, 80, 80));
}

namespace {
QString fmtTime(double s) {
    if (s < 0) s = 0;
    int total_ms = int(std::round(s * 1000));
    int ms = total_ms % 1000;
    int sec = (total_ms / 1000) % 60;
    int mn = (total_ms / 60000) % 60;
    int hr = total_ms / 3600000;
    if (hr > 0) {
        return QString("%1:%2:%3.%4")
            .arg(hr).arg(mn, 2, 10, QChar('0'))
            .arg(sec, 2, 10, QChar('0')).arg(ms, 3, 10, QChar('0'));
    }
    return QString("%1:%2.%3")
        .arg(mn, 2, 10, QChar('0'))
        .arg(sec, 2, 10, QChar('0'))
        .arg(ms, 3, 10, QChar('0'));
}
}

VideoPreviewWidget::VideoPreviewWidget(MainWindow* mw, QWidget* parent)
    : QWidget(parent), m_mw(mw) {
    m_player = new QMediaPlayer(this);
    m_audio = new QAudioOutput(this);
    m_player->setAudioOutput(m_audio);

    m_videoWidget = new QVideoWidget(this);
    m_videoWidget->setMinimumSize(320, 180);
    {
        QPalette pal = m_videoWidget->palette();
        pal.setColor(QPalette::Window, QColor(15, 15, 15));
        m_videoWidget->setPalette(pal);
        m_videoWidget->setAutoFillBackground(true);
    }
    m_player->setVideoOutput(m_videoWidget);

    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    v->addWidget(m_videoWidget, 1);

    m_scrub = new TrimSlider(this);
    m_scrub->setRange(0, 10000);
    v->addWidget(m_scrub);

    auto* row = new QHBoxLayout;
    QStyle* st = style();
    m_btnHome = new QPushButton(st->standardIcon(QStyle::SP_MediaSkipBackward), QString(), this);
    m_btnHome->setToolTip(tr("Jump to source start"));
    m_btnPlay = new QPushButton(st->standardIcon(QStyle::SP_MediaPlay), QString(), this);
    m_btnPlay->setToolTip(tr("Play / pause"));
    m_btnEnd  = new QPushButton(st->standardIcon(QStyle::SP_MediaSkipForward), QString(), this);
    m_btnEnd->setToolTip(tr("Jump to source end"));
    m_btnGoStart = new QPushButton(tr("Clip start"), this);
    m_btnGoStart->setToolTip(tr("Jump to the trim-start of this clip"));
    m_btnGoEnd   = new QPushButton(tr("Clip end"), this);
    m_btnGoEnd->setToolTip(tr("Jump to the trim-end of this clip"));
    m_btnSetStart = new QPushButton(tr("Set as Start"), this);
    m_btnSetEnd   = new QPushButton(tr("Set as End"), this);
    m_pos = new QLabel("00:00.000", this);
    m_dur = new QLabel("/ 00:00.000", this);
    m_trim = new QLabel(tr("(no video selected)"), this);

    row->addWidget(m_btnHome);
    row->addWidget(m_btnPlay);
    row->addWidget(m_btnEnd);
    row->addWidget(m_pos);
    row->addWidget(m_dur);
    row->addStretch(1);
    row->addWidget(m_btnGoStart);
    row->addWidget(m_btnSetStart);
    row->addWidget(m_btnSetEnd);
    row->addWidget(m_btnGoEnd);
    v->addLayout(row);

    auto* row2 = new QHBoxLayout;
    row2->addWidget(m_trim);
    row2->addStretch(1);
    v->addLayout(row2);

    auto togglePlay = [this]() {
        if (m_loadedPath.isEmpty()) return;     // no media loaded
        if (m_player->playbackState() == QMediaPlayer::PlayingState) {
            m_player->pause();
        } else {
            m_player->play();
        }
    };
    connect(m_btnPlay, &QPushButton::clicked, this, togglePlay);

    // Spacebar toggles play / pause when the video preview is the visible
    // page of the preview stack. WindowShortcut so it fires regardless of
    // which sibling widget has focus, and we guard on isVisible() so it's
    // a no-op while an image / text-clip is being previewed instead.
    auto* scSpace = new QShortcut(QKeySequence(Qt::Key_Space), this);
    scSpace->setContext(Qt::WindowShortcut);
    connect(scSpace, &QShortcut::activated, this, [this, togglePlay]() {
        if (!isVisible()) return;
        togglePlay();
    });
    connect(m_btnHome, &QPushButton::clicked, this, [this]() {
        m_player->setPosition(0);
    });
    connect(m_btnEnd, &QPushButton::clicked, this, [this]() {
        if (m_durationMs > 100) m_player->setPosition(m_durationMs - 100);
    });
    connect(m_btnGoStart, &QPushButton::clicked, this, [this]() {
        if (m_id.isNull()) return;
        int idx = m_mw->project().indexOfId(m_id);
        if (idx < 0) return;
        const Item& it = m_mw->project().items[idx];
        if (it.kind != ItemKind::Video) return;
        m_player->setPosition(qint64(it.video.startSecs * 1000));
    });
    connect(m_btnGoEnd, &QPushButton::clicked, this, [this]() {
        if (m_id.isNull()) return;
        int idx = m_mw->project().indexOfId(m_id);
        if (idx < 0) return;
        const Item& it = m_mw->project().items[idx];
        if (it.kind != ItemKind::Video) return;
        double e = it.video.endSecs > 0 ? it.video.endSecs : it.video.sourceDurationSecs;
        m_player->setPosition(qint64(e * 1000));
    });
    connect(m_btnSetStart, &QPushButton::clicked, this, [this]() {
        if (m_id.isNull()) return;
        int idx = m_mw->project().indexOfId(m_id);
        if (idx < 0) return;
        const Item& it = m_mw->project().items[idx];
        if (it.kind != ItemKind::Video) return;
        double cur = m_player->position() / 1000.0;
        double end = it.video.endSecs > 0 ? it.video.endSecs : it.video.sourceDurationSecs;
        double s = std::min(cur, std::max(0.0, end - 0.001));
        m_mw->setVideoTrim(m_id, s, end);
        rebuildLabels();
    });
    connect(m_btnSetEnd, &QPushButton::clicked, this, [this]() {
        if (m_id.isNull()) return;
        int idx = m_mw->project().indexOfId(m_id);
        if (idx < 0) return;
        const Item& it = m_mw->project().items[idx];
        if (it.kind != ItemKind::Video) return;
        double cur = m_player->position() / 1000.0;
        double s = it.video.startSecs;
        double e = std::max(s + 0.001, cur);
        m_mw->setVideoTrim(m_id, s, e);
        rebuildLabels();
    });

    connect(m_scrub, &TrimSlider::seekRequested, this, [this](qint64 ms) {
        m_player->setPosition(ms);
    });

    connect(m_player, &QMediaPlayer::positionChanged, this, &VideoPreviewWidget::onPositionChanged);
    connect(m_player, &QMediaPlayer::durationChanged, this, &VideoPreviewWidget::onDurationChanged);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this,
            &VideoPreviewWidget::onPlaybackStateChanged);
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this,
            &VideoPreviewWidget::onMediaStatusChanged);
    connect(m_player, &QMediaPlayer::errorOccurred, this, &VideoPreviewWidget::onError);

    setPlayButtonText();
}

VideoPreviewWidget::~VideoPreviewWidget() {
    m_player->stop();
}

void VideoPreviewWidget::setItem(const QUuid& id) {
    if (id.isNull()) { clear(); return; }
    int idx = m_mw->project().indexOfId(id);
    if (idx < 0) { clear(); return; }
    const Item& it = m_mw->project().items[idx];
    if (it.kind != ItemKind::Video) { clear(); return; }

    // If we're already showing this exact source file, just refresh the
    // markers and labels — don't tear the player down. Otherwise edits
    // like "Set as Start" cause a full reload mid-playback.
    if (m_id == id && m_loadedPath == it.common().sourcePath) {
        rebuildLabels();
        return;
    }

    m_id = id;
    m_player->stop();
    m_durationMs = 0;
    m_loadedPath = it.common().sourcePath;
    m_seekOnLoadMs = qint64(it.video.startSecs * 1000);
    m_player->setSource(QUrl::fromLocalFile(it.common().sourcePath));
    rebuildLabels();
}

void VideoPreviewWidget::clear() {
    m_id = QUuid();
    m_loadedPath.clear();
    m_seekOnLoadMs = -1;
    m_player->stop();
    m_player->setSource(QUrl());
    m_scrub->setTotalMs(0);
    m_scrub->setPositionMs(0);
    m_scrub->clearMarkers();
    m_pos->setText("00:00.000");
    m_dur->setText("/ 00:00.000");
    m_trim->setText(tr("(no video selected)"));
    m_durationMs = 0;
    setPlayButtonText();
}

void VideoPreviewWidget::onPositionChanged(qint64 ms) {
    m_scrub->setPositionMs(ms);
    m_pos->setText(fmtTime(ms / 1000.0));
}

void VideoPreviewWidget::onDurationChanged(qint64 ms) {
    m_durationMs = ms;
    m_scrub->setTotalMs(ms);
    m_dur->setText("/ " + fmtTime(ms / 1000.0));
    rebuildLabels();
}

void VideoPreviewWidget::onPlaybackStateChanged(QMediaPlayer::PlaybackState) {
    setPlayButtonText();
}

void VideoPreviewWidget::onMediaStatusChanged(QMediaPlayer::MediaStatus s) {
    // Once the source is fully buffered, position the playhead at the
    // clip's trim-start so a later Play click begins from the right place.
    // The player stays paused; the canvas remains black until the user
    // explicitly plays.
    if (m_seekOnLoadMs >= 0 && s == QMediaPlayer::BufferedMedia) {
        m_player->setPosition(m_seekOnLoadMs);
        m_seekOnLoadMs = -1;
    }
}

void VideoPreviewWidget::onError(QMediaPlayer::Error, const QString& msg) {
    qWarning("QMediaPlayer error: %s", qPrintable(msg));
}

void VideoPreviewWidget::setPlayButtonText() {
    QStyle::StandardPixmap sp = (m_player->playbackState() == QMediaPlayer::PlayingState)
        ? QStyle::SP_MediaPause
        : QStyle::SP_MediaPlay;
    m_btnPlay->setIcon(style()->standardIcon(sp));
}

void VideoPreviewWidget::rebuildLabels() {
    if (m_id.isNull()) return;
    int idx = m_mw->project().indexOfId(m_id);
    if (idx < 0) return;
    const Item& it = m_mw->project().items[idx];
    if (it.kind != ItemKind::Video) return;
    double s = it.video.startSecs;
    double e = it.video.endSecs > 0 ? it.video.endSecs : it.video.sourceDurationSecs;
    m_trim->setText(QString(tr("trim %1 → %2  (clip duration %3)"))
        .arg(fmtTime(s)).arg(fmtTime(e)).arg(fmtTime(std::max(0.0, e - s))));

    qint64 totalMs = m_durationMs > 0 ? m_durationMs
                                      : qint64(it.video.sourceDurationSecs * 1000);
    if (totalMs > 0) {
        if (m_scrub->maximum() != int(totalMs)) m_scrub->setTotalMs(totalMs);
        m_scrub->setMarkers(qint64(s * 1000), qint64(e * 1000));
    } else {
        m_scrub->clearMarkers();
    }
}

} // namespace vlip
