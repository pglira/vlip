#pragma once

#include "Project.h"
#include <QObject>
#include <QString>
#include <QProcess>

namespace vlip {

// Renders a project to an MP4 path. Drives ffmpeg via QProcess
// (asynchronous; never blocks the UI thread). Emits progress and
// log signals; finished() reports success or error message.
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
    void started();
    void log(const QString& line);          // human-readable progress lines
    void progress(double fraction);         // 0..1
    void finished(bool ok, const QString& message);

private:
    QProcess* m_proc = nullptr;
    QString m_outPath;
    double m_totalDuration = 0.0;
    QString m_logTail;
    QString m_workDir; // temp dir cleanup

    QString buildAndExecute(const Project& p, const QString& outPath, QString* err);
    void cleanupTempDir();
    void onStdoutLine(const QString& line);
};

} // namespace vlip
