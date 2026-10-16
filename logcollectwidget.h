#ifndef LOGCOLLECTWIDGET_H
#define LOGCOLLECTWIDGET_H

#include <QWidget>
#include <QTreeWidget>
#include <QTableWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QTextEdit>
#include <QLabel>
#include <QJsonObject>
#include <QJsonArray>

#include "sshclient.h"

class SshClient;

struct HostConfig {
    QString name;
    QString host;
    int     port = 22;
    QString user;
    QString password;
    QStringList logPaths;   // 远程日志路径列表
    QString note;
};

class LogCollectWidget : public QWidget
{
    Q_OBJECT
public:
    explicit LogCollectWidget(SshClient *client, QWidget *parent = nullptr);

signals:
    void exportCompleted(const QString &zipPath);

private slots:
    void onAddHost();
    void onDeleteHost();
    void onEditHost();
    void onConnect();
    void onDisconnect();
    void onStartExport();
    void onBrowseLocalPath();
    void onPathChanged(int index);
    void onHostSelectionChanged();

    // SshClient 信号
    void onSshConnected(const QString &host);
    void onSshConnectFailed(const QString &host, const QString &error);
    void onSshDisconnected(const QString &host);
    void onFileListReceived(const QString &host, const QString &path,
                            const QVector<RemoteFileInfo> &files);
    void onRobotInfoReceived(const QString &sn, const QString &version,
                             const QString &deviceType);
    void onDownloadProgress(int current, int total, const QString &file, int percent);
    void onDownloadComplete(const QString &zipPath, int totalFiles);
    void onSshLog(const QString &level, const QString &msg);
    void onDownloadError(const QString &error);
    void onSshError(const QString &msg);
    void onRefreshFiles();
    void onSelectAll();
    void onDeselectAll();
    void onFileTableDoubleClicked(int row, int column);
    void onFileTableContextMenu(const QPoint &pos);

private:
    void setupUi();
    void loadHostConfigs();
    void saveHostConfigs();
    QString configFilePath() const;
    HostConfig currentHostConfig() const;
    void appendLog(const QString &msg);
    void updateConnectionState(bool connected);
    void previewRemoteFile(const QString &remotePath, const QString &fileName);

    SshClient *m_ssh;

    // 左侧 - 主机管理
    QTreeWidget  *m_hostTree;
    QPushButton  *m_btnAddHost;
    QPushButton  *m_btnDeleteHost;
    QPushButton  *m_btnEditHost;

    // 右侧 - 操作区
    QLabel       *m_statusLabel;
    QPushButton  *m_btnConnect;
    QPushButton  *m_btnDisconnect;
    QComboBox    *m_pathCombo;
    QTableWidget *m_fileTable;
    QPushButton  *m_btnRefresh;
    QPushButton  *m_btnSelectAll;
    QPushButton  *m_btnDeselectAll;
    QLineEdit    *m_localPathEdit;
    QPushButton  *m_btnBrowse;
    QPushButton  *m_btnExport;
    QProgressBar *m_progressBar;
    QTextEdit    *m_logView;

    // 状态
    QVector<HostConfig> m_hosts;
    bool m_connected = false;
    QString m_currentHost;
    int m_currentPort = 22;
    QString m_robotSn;
    QString m_robotVersion;
    QString m_robotDeviceType;
};

#endif // LOGCOLLECTWIDGET_H
