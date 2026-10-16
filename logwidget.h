#ifndef LOGWIDGET_H
#define LOGWIDGET_H

#include <QWidget>
#include <QPlainTextEdit>
#include <QGroupBox>
#include <QVBoxLayout>

class LogWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LogWidget(QWidget *parent = nullptr);

    void addLog(const QString &message, const QString &color = "#00ff00");
    void addInfo(const QString &message);
    void addWarning(const QString &message);
    void addError(const QString &message);
    void clearLog();

private:
    QPlainTextEdit *m_logText;
};

#endif // LOGWIDGET_H
