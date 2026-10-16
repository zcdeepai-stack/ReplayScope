#include "logwidget.h"
#include <QScrollBar>
#include <QDateTime>
#include <QTextCursor>
#include <QTextCharFormat>

static const char *DARK_BG     = "#0a0e14";
static const char *NEON_BLUE   = "#00d4ff";
static const char *NEON_CYAN   = "#00ffc8";
static const char *PROGRESS_BG = "#2a3040";

LogWidget::LogWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    auto *group = new QGroupBox("系统日志");
    group->setStyleSheet(QString(R"(
        QGroupBox {
            font-weight: bold;
            font-size: 13px;
            color: %1;
            background-color: %2;
            border: 1px solid #2a3545;
            border-radius: 8px;
            margin-top: 12px;
            padding-top: 20px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 15px;
            padding: 0 8px;
        }
    )").arg(NEON_BLUE, DARK_BG));

    auto *groupLayout = new QVBoxLayout(group);

    m_logText = new QPlainTextEdit;
    m_logText->setReadOnly(true);
    m_logText->setMaximumBlockCount(500);
    m_logText->setStyleSheet(QString(R"(
        QPlainTextEdit {
            background-color: %1;
            color: %2;
            font-family: 'Consolas', 'Courier New', monospace;
            font-size: 10pt;
            border: none;
            border-radius: 4px;
            padding: 8px;
            selection-background-color: %3;
            selection-color: %4;
        }
        QScrollBar:vertical {
            background: %4;
            width: 8px;
            border-radius: 4px;
        }
        QScrollBar::handle:vertical {
            background: #3a4555;
            border-radius: 4px;
            min-height: 30px;
        }
        QScrollBar::handle:vertical:hover {
            background: %3;
        }
    )").arg(PROGRESS_BG, NEON_CYAN, NEON_BLUE, DARK_BG));

    groupLayout->addWidget(m_logText);
    mainLayout->addWidget(group);
}

void LogWidget::addLog(const QString &message, const QString &color)
{
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    QString logMsg = QString("[%1]\n%2").arg(timestamp, message);

    m_logText->appendPlainText(logMsg);

    QTextCursor cursor = m_logText->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
    cursor.movePosition(QTextCursor::PreviousBlock, QTextCursor::KeepAnchor);

    QTextCharFormat fmt;
    fmt.setForeground(QColor(color));
    cursor.mergeCharFormat(fmt);

    m_logText->verticalScrollBar()->setValue(
        m_logText->verticalScrollBar()->maximum());
}

void LogWidget::addInfo(const QString &message)
{
    addLog("INFO: " + message, "#00ffc8");
}

void LogWidget::addWarning(const QString &message)
{
    addLog("WARNING: " + message, "#ffc040");
}

void LogWidget::addError(const QString &message)
{
    addLog("ERROR: " + message, "#ff6b6b");
}

void LogWidget::clearLog()
{
    m_logText->clear();
}
