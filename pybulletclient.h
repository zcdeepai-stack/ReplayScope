#ifndef PYBULLETCLIENT_H
#define PYBULLETCLIENT_H

#include <QObject>
#include <QProcess>
#include <QTcpSocket>
#include <QJsonObject>
#include <QByteArray>

class PyBulletClient : public QObject
{
    Q_OBJECT

public:
    explicit PyBulletClient(const QString &serverSubDir, QObject *parent = nullptr);
    ~PyBulletClient();

    void startServer(const QString &pythonPath = "python");
    void stopServer();
    bool isConnected() const;

    // 命令接口
    void loadCsv(const QString &path);
    /// enabled=false 时服务端会跳过并卸载地形；path 为空同样不加载
    void loadTerrain(const QString &path, bool enabled);
    void play(int startFrame = 0, int endFrame = -1);
    void pause();
    void resume();
    void stop();
    void seek(int frame);
    void setSpeed(double speed);
    void setLoop(bool enabled);
    void setJointPosition(int jointIndex, double value);
    void setCamera(double distance, double yaw, double pitch,
                   double tx, double ty, double tz);
    void panCamera(double deltaX, double deltaY);
    void setModelOpacity(double opacity);

signals:
    void windowHandleReceived(quintptr hwnd);
    void logReceived(const QString &level, const QString &message);
    void statusReceived(const QJsonObject &status);
    void serverReady();
    void serverStopped();
    void errorOccurred(const QString &error);

private slots:
    void onProcessReadyReadStdout();
    void onProcessReadyReadStderr();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);

private:
    void sendCommand(const QJsonObject &cmd);
    void processMessage(const QByteArray &payload);
    void connectToServer(int port);
    void retryPackagedConnection();

    QProcess *m_process = nullptr;
    QTcpSocket *m_socket = nullptr;
    QByteArray m_readBuffer;
    int m_serverPort = 0;
    QString m_serverScript;
    QString m_serverExe;     // PyInstaller 打包后的 exe 路径
    QString m_serverDir;
    bool m_serverStarted = false;
    bool m_packagedMode = false;
    int m_connectRetries = 0;
};

#endif // PYBULLETCLIENT_H
