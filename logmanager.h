#ifndef LOGMANAGER_H
#define LOGMANAGER_H

#include <QWidget>
#include <QTabWidget>
#include <QLabel>
#include <QTimer>

class SshClient;
class LogCollectWidget;
class LogAnalysisWidget;

class LogManager : public QWidget
{
    Q_OBJECT
public:
    explicit LogManager(QWidget *parent = nullptr);
    ~LogManager();
    void switchToAnalysisTab();

private:
    QTabWidget        *m_tabWidget;
    SshClient         *m_sshClient;
    LogCollectWidget  *m_collectWidget;
    LogAnalysisWidget *m_analysisWidget;
    QLabel            *m_clockLabel;
    QTimer            *m_clockTimer;
};

#endif // LOGMANAGER_H
