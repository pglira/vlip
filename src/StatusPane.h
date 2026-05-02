#pragma once

#include <QWidget>

class QPlainTextEdit;
class QProgressBar;
class QLabel;

namespace vlip {

class MainWindow;

class StatusPane : public QWidget {
    Q_OBJECT
public:
    enum Level { Info, Warning, Error };
    Q_ENUM(Level)

    explicit StatusPane(MainWindow* mw, QWidget* parent = nullptr);

public slots:
    void appendMessage(const QString& s, int level = Info);
    void setProgress(double frac);

private:
    QPlainTextEdit* m_log;
    QProgressBar* m_bar;
    QLabel* m_pct;
};

} // namespace vlip
