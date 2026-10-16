#ifndef SSHCLIENT_H
#define SSHCLIENT_H

#include <QObject>
#include <QProcess>
#include <QTcpSocket>
#include <QJsonObject>
#include <QJsonArray>
#include <QByteArray>
#include <QMetaType>
#include <QDebug>

struct RemoteFileInfo {
    QString name;
    qint64  size = 0;
    qint64  mtime = 0;
    bool    isDir = false;
};

Q_DECLARE_METATYPE(RemoteFileInfo)

inline QDebug operator<<(QDebug dbg, const RemoteFileInfo &info)
{
    dbg.nospace() << "RemoteFileInfo(" << info.name << ", size=" << info.size
                  << ", isDir=" << info.isDir << ")";
    return dbg.space();
}

class SshClient : public QObject
{
    Q_OBJECT
public:
    explicit SshClient(QObject *parent = nullptr);
    ~SshClient();

    void startHelper();
    void stopHelper();
    bool isHelperRunning() const;

    // SSH 命令
    void connectHost(const QString &host, int port,
                     const QString &user, const QString &password);
    void disconnectHost(const QString &host, int port);
    void listFiles(const QString &host, int port, const QString &remotePath);
    void getRobotInfo(const QString &host, int port);
    void downloadFiles(const QString &host, int port,
                       const QJsonArray &files,
                       const QString &localDir, const QString &zipName);
    void execCommand(const QString &host, int port, const QString &command);

signals:
    void helperReady();
    void connected(const QString &host);
    void connectFailed(const QString &host, const QString &error);
    void disconnected(const QString &host);
    void fileListReceived(const QString &host, const QString &path,
                          const QVector<RemoteFileInfo> &files);
    void robotInfoReceived(const QString &sn, const QString &version,
                           const QString &deviceType);
    void downloadProgress(int current, int total, const QString &file, int percent);
    void downloadComplete(const QString &zipPath, int totalFiles);
    void downloadError(const QString &error);
    void execResult(const QString &stdOut, const QString &stdErr);
    void errorOccurred(const QString &msg);
    void logMessage(const QString &level, const QString &msg);

private slots:
    void onProcessReadyReadStdout();
    void onProcessReadyReadStderr();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onSocketConnected();
    void onSocketReadyRead();

private:
    void sendCommand(const QJsonObject &cmd);
    void processMessage(const QByteArray &payload);
    void connectToHelper(int port);

    QProcess   *m_process = nullptr;
    QTcpSocket *m_socket  = nullptr;
    QByteArray  m_readBuffer;
    QString     m_helperScript;
    QString     m_helperExe;
    bool        m_helperStarted = false;
};

#endif // SSHCLIENT_H
