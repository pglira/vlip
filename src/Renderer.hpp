#pragma once

#include "Project.hpp"
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QProcess>

namespace vlip {

// Renders a project to an MP4 path, asynchronously, in two phases.
//
//  1. Batch phase: the timeline is split into m_batchSize-item chunks
//     (auto-scaled by canvas resolution so 4K renders use ~2 items per
//     batch and 1080p ones use 10). Each batch produces a *video-only*
//     .mkv intermediate via its own ffmpeg invocation. Bounded peak
//     memory regardless of project size — a single ffmpeg invocation
//     with 100+ simultaneous 4K inputs OOMs even on 32 GB machines.
//
//  2. Concat phase: a final ffmpeg invocation stream-copies the video
//     intermediates via the concat demuxer, builds the entire audio
//     timeline (per-clip silence sources or video-clip audio extracts)
//     in one filter graph, mixes background music with ducking around
//     video clips, and writes the final MP4. Audio encodes in a single
//     uninterrupted run, so AAC priming-delay slop doesn't accumulate
//     per batch boundary.
//
// finished() reports success or an error message; cancelled() fires
// instead when the user invoked cancel(). log() emits high-level lines.
// Verbose ffmpeg stderr is captured into m_logTail, surfaced only on
// failure.
class Renderer : public QObject {
    Q_OBJECT
public:
    explicit Renderer(QObject* parent = nullptr);
    ~Renderer() override;

    // Validate the render request (e.g. at least one used item).
    // Returns empty string on success or error message.
    static QString validate(const Project& p);

    // Asynchronously start the render. If validate() fails, returns
    // false and emits finished(false, error).
    bool start(const Project& p, const QString& outPath);

    void cancel();
    bool isRunning() const;

signals:
    void log(const QString& line);
    void finished(bool ok, const QString& message);
    // Emitted instead of finished() when the user invoked cancel(). Lets
    // the UI show a neutral "cancelled" message instead of treating the
    // non-zero ffmpeg exit as a render failure.
    void cancelled();

private:
    enum class State { Idle, Batches, Concat };
    // Batch size auto-scales: anchored at "10 items per batch is
    // comfortable at 1080p", inversely scaled by canvas pixel count so
    // a 4K render uses ~2-3 items per batch and a 720p one stays at 10.
    // The chosen value is stored in m_batchSize at start().
    static constexpr int kBaseBatchSize = 10;
    static constexpr int kBaseCanvasW   = 1920;
    static constexpr int kBaseCanvasH   = 1080;
    static constexpr int kSampleRate    = 48000;

    QProcess* m_proc = nullptr;
    QString m_outPath;
    QString m_logTail;
    QString m_workDir;          // holds intermediates while rendering
    double m_totalDuration = 0.0;
    qint64 m_lastProgressLogMs = 0;
    bool m_cancelRequested = false;

    State m_state = State::Idle;
    Project m_renderProject;            // snapshot taken at start()
    QVector<int> m_usedIndices;         // indices into m_renderProject.items, in render order
    QVector<double> m_batchDurations;   // duration of each batch (sum of items)
    QStringList m_intermediates;        // .mkv paths produced so far
    QVector<QPair<double,double>> m_videoWindows;  // for music ducking, computed once
    int m_batchIndex = 0;
    int m_batchSize = kBaseBatchSize;       // chosen per-render from canvas size
    double m_completedBatchDuration = 0.0;  // running offset for global progress

    bool startNextBatch(QString* err);
    bool startConcatPass(QString* err);
    QStringList buildBatchArgs(int firstUsedIndex, int count,
                               const QString& batchOutPath);
    QStringList buildConcatArgs(const QString& outPath);
    bool spawnFfmpeg(const QStringList& args, QString* err);
    void onBatchFinished(int code, int exitStatus);
    void onConcatFinished(int code, int exitStatus);
    void abortWithFailure(const QString& msg);
    void emitCancelled();        // shared cleanup-and-emit for both phases
    // If the project has any background-music files and the rendered
    // video has any duration, append the music inputs (to `args`) and
    // the music filter chains (to `chains`), advancing `inputIndex`.
    // Returns the audio map label the encoder should map — the unchanged
    // `currentAudioLabel` if no music, or the new mixed label otherwise.
    QString appendBackgroundMusicChain(QStringList& args, QStringList& chains,
                                       int& inputIndex,
                                       const QString& currentAudioLabel);
    void cleanupTempDir();
    void onProgressLine(const QString& line);
};

} // namespace vlip
