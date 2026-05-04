#pragma once

#include "Project.hpp"
#include <QObject>
#include <QString>
#include <QProcess>

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

private:
    QProcess* m_proc = nullptr;
    QString m_outPath;
    QString m_logTail;
    QString m_workDir; // temp dir cleanup
    double m_totalDuration = 0.0;
    qint64 m_lastProgressLogMs = 0;

    QString buildAndExecute(const Project& p, const QString& outPath, QString* err);
    void cleanupTempDir();
    void onProgressLine(const QString& line);
};

} // namespace vlip
