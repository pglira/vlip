#include "StatusPane.h"

#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QDateTime>

namespace vlip {

StatusPane::StatusPane(MainWindow*, QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);

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

} // namespace vlip
