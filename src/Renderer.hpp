#pragma once

#include "Project.hpp"
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QProcess>
#include <utility>

namespace vlip {

// Renders a project to an MP4 path. Drives ffmpeg via QProcess
// (asynchronous; never blocks the UI thread). finished() reports success
// or an error message; log() emits a small number of high-level lines
// (start, completion). Verbose ffmpeg stderr is captured into m_logTail
// and surfaced only on failure.
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
    QProcess* m_proc = nullptr;
    QString m_outPath;
    QString m_logTail;
    QString m_workDir; // temp dir cleanup
    double m_totalDuration = 0.0;
    qint64 m_lastProgressLogMs = 0;
    bool m_cancelRequested = false;

    QString buildAndExecute(const Project& p, const QString& outPath, QString* err);
    // If `backgroundMusic` is non-empty and the rendered video has any
    // duration, append the music inputs (to `args`) and the music filter
    // chains (to `chains`), advancing `inputIndex` accordingly. Returns
    // the audio map label the encoder should use — either the unchanged
    // `currentAudioLabel` (no music) or the new mixed label.
    QString appendBackgroundMusicChain(
        QStringList& args,
        QStringList& chains,
        int& inputIndex,
        const QStringList& backgroundMusic,
        double totalDuration,
        double transition,
        int sampleRate,
        const QVector<QPair<double, double>>& videoWindows,
        const QString& currentAudioLabel);
    void cleanupTempDir();
    void onProgressLine(const QString& line);
};

} // namespace vlip
