#include "StatusPane.h"

#include <QPlainTextEdit>
#include <QProgressBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDateTime>

namespace vlip {

StatusPane::StatusPane(MainWindow*, QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);

    auto* row = new QHBoxLayout;
    m_bar = new QProgressBar(this);
    m_bar->setRange(0, 1000);
    m_bar->setValue(0);
    m_pct = new QLabel(tr("idle"), this);
    row->addWidget(m_bar, 1);
    row->addWidget(m_pct);
    outer->addLayout(row);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(2000);
    outer->addWidget(m_log, 1);
}

void StatusPane::appendMessage(const QString& s, int level) {
    const char* tag = "INFO";
    switch (level) {
        case Warning: tag = "WARN"; break;
        case Error:   tag = "ERROR"; break;
        case Info:
        default:      tag = "INFO"; break;
    }
    QString stamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    m_log->appendPlainText(QString("[%1] [%2] %3").arg(stamp, tag, s));
}

void StatusPane::setProgress(double frac) {
    int v = std::clamp(int(std::round(frac * 1000)), 0, 1000);
    m_bar->setValue(v);
    m_pct->setText(QString("%1%").arg(frac * 100.0, 0, 'f', 1));
}

} // namespace vlip
