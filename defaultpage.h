#ifndef DEFAULTPAGE_H
#define DEFAULTPAGE_H

#include <QWidget>

class QPushButton;
class QProcess;
class QLabel;

class DefaultPage : public QWidget
{
    Q_OBJECT

public:
    explicit DefaultPage(QWidget *parent = nullptr);
    ~DefaultPage() override;

signals:
    void localModeSelected();

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void onLocalClicked();
    void onWebClicked();

private:
    /// 探测本机 9090 上是否已有本服务在跑（据此避免重复拉起后端）
    bool backendAlive(int timeoutMs = 800);
    /// 把子进程绑定到作业对象：主程序一旦退出（含崩溃），系统会连带结束后端，
    /// 从根上杜绝孤儿进程。
    void bindToJobObject(qint64 pid);
    void setStatus(const QString &text, const QString &color);

    QPushButton *m_btnLocal   = nullptr;
    QPushButton *m_btnWeb     = nullptr;
    QLabel      *m_statusLabel = nullptr;
    QProcess    *m_webServer  = nullptr;
    void        *m_jobHandle  = nullptr;   ///< HANDLE，仅 Windows 使用
};

#endif // DEFAULTPAGE_H
