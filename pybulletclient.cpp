#include "pybulletclient.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QCoreApplication>
#include <QProcessEnvironment>
#include <QDir>
#include <QFileInfo>
#include <QtEndian>
#include <QTimer>
#include <QTcpServer>
#include <QHostAddress>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// 消息类型常量
static constexpr quint8 MSG_LOG    = 0x4C;  // 'L'
static constexpr quint8 MSG_STATUS = 0x53;  // 'S'

PyBulletClient::PyBulletClient(const QString &serverSubDir, QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
    , m_socket(new QTcpSocket(this))
{
    // 定位 Python 服务器脚本（根据传入的子目录名）
    QString appDir = QCoreApplication::applicationDirPath();
#ifdef SRCDIR
    m_serverDir = QStringLiteral(SRCDIR "/") + serverSubDir;
#else
    // 打包/部署模式：子目录位于 exe 同级
    m_serverDir = appDir + "/" + serverSubDir;
#endif
    m_serverScript = m_serverDir + "/pybullet_server.py";
    // PyInstaller 打包后的可执行文件路径
    m_serverExe = m_serverDir + "/pybullet_server.exe";

    // QProcess 信号
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &PyBulletClient::onProcessReadyReadStdout);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &PyBulletClient::onProcessReadyReadStderr);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &PyBulletClient::onProcessFinished);

    // QTcpSocket 信号
    connect(m_socket, &QTcpSocket::connected,
            this, &PyBulletClient::onSocketConnected);
    connect(m_socket, &QTcpSocket::disconnected,
            this, &PyBulletClient::onSocketDisconnected);
    connect(m_socket, &QTcpSocket::readyRead,
            this, &PyBulletClient::onSocketReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred,
            this, &PyBulletClient::onSocketError);
}

PyBulletClient::~PyBulletClient()
{
    stopServer();
}

void PyBulletClient::startServer(const QString &pythonPath)
{
    if (m_process->state() != QProcess::NotRunning) {
        return;
    }

    // 优先检查 PyInstaller 打包的 exe（部署模式）
    const bool useExe = QFileInfo::exists(m_serverExe);
    if (!useExe && !QFileInfo::exists(m_serverScript)) {
        emit errorOccurred("找不到 PyBullet 服务器: " + m_serverScript);
        return;
    }

    m_serverStarted = false;
    m_packagedMode = useExe;
    m_connectRetries = 0;
    m_readBuffer.clear();

    m_process->setWorkingDirectory(m_serverDir);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);

    // 设置 Python UTF-8 编码环境
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONIOENCODING", "utf-8");
    env.insert("PYTHONUTF8", "1");
    m_process->setProcessEnvironment(env);

    if (useExe) {
        // windowed PyInstaller 进程没有可靠的 stdout。Qt 先取得空闲端口，
        // 再把明确端口交给服务，服务启动期间的短暂拒绝连接由下方重试处理。
        QTcpServer portProbe;
        if (!portProbe.listen(QHostAddress::LocalHost, 0)) {
            emit errorOccurred("无法分配 PyBullet 本机端口: " + portProbe.errorString());
            return;
        }
        m_serverPort = static_cast<int>(portProbe.serverPort());
        portProbe.close();
        m_serverStarted = true;
#ifdef Q_OS_WIN
        m_process->setCreateProcessArgumentsModifier(
            [](QProcess::CreateProcessArguments *args) {
                args->flags |= CREATE_NO_WINDOW;
            });
#endif
        m_process->start(m_serverExe, {QString::number(m_serverPort)});
        QTimer::singleShot(250, this, &PyBulletClient::retryPackagedConnection);
    } else {
        // 开发模式：调用系统 python 运行脚本
        m_process->start(pythonPath, {m_serverScript, "0"});
    }
}

void PyBulletClient::stopServer()
{
    // 关闭 Socket
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
        m_socket->waitForDisconnected(1000);
    }

    // 终止 Python 进程
    if (m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(3000)) {
            m_process->kill();
            m_process->waitForFinished(1000);
        }
    }
    m_serverStarted = false;
    m_packagedMode = false;
    m_connectRetries = 0;
}

bool PyBulletClient::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

// ---- 命令接口 ----
void PyBulletClient::loadCsv(const QString &path)
{
    sendCommand({{"cmd", "load_csv"}, {"path", QDir::toNativeSeparators(path)}});
}

void PyBulletClient::loadTerrain(const QString &path, bool enabled)
{
    sendCommand({{"cmd", "load_terrain"},
                 {"path", path.isEmpty() ? QString() : QDir::toNativeSeparators(path)},
                 {"enabled", enabled}});
}

void PyBulletClient::play(int startFrame, int endFrame)
{
    sendCommand({{"cmd", "play"}, {"start_frame", startFrame}, {"end_frame", endFrame}});
}

void PyBulletClient::pause()
{
    sendCommand({{"cmd", "pause"}});
}

void PyBulletClient::resume()
{
    sendCommand({{"cmd", "resume"}});
}

void PyBulletClient::stop()
{
    sendCommand({{"cmd", "stop"}});
}

void PyBulletClient::seek(int frame)
{
    sendCommand({{"cmd", "seek"}, {"frame", frame}});
}

void PyBulletClient::setSpeed(double speed)
{
    sendCommand({{"cmd", "set_speed"}, {"speed", speed}});
}

void PyBulletClient::setLoop(bool enabled)
{
    sendCommand({{"cmd", "set_loop"}, {"enabled", enabled}});
}

void PyBulletClient::setJointPosition(int jointIndex, double value)
{
    sendCommand({{"cmd", "set_joint"}, {"joint", jointIndex}, {"value", value}});
}

void PyBulletClient::setCamera(double distance, double yaw, double pitch,
                                double tx, double ty, double tz)
{
    sendCommand({{"cmd", "set_camera"},
                 {"distance", distance}, {"yaw", yaw}, {"pitch", pitch},
                 {"tx", tx}, {"ty", ty}, {"tz", tz}});
}

// ---- 内部方法 ----
void PyBulletClient::sendCommand(const QJsonObject &cmd)
{
    if (!isConnected()) return;

    QByteArray json = QJsonDocument(cmd).toJson(QJsonDocument::Compact);
    quint32 len = static_cast<quint32>(json.size());
    QByteArray header(4, '\0');
    qToBigEndian(len, header.data());
    m_socket->write(header + json);
    m_socket->flush();
}

void PyBulletClient::connectToServer(int port)
{
    m_serverPort = port;
    m_socket->abort();
    m_socket->connectToHost("127.0.0.1", static_cast<quint16>(port));
}

void PyBulletClient::panCamera(double deltaX, double deltaY)
{
    sendCommand({{"cmd", "pan_camera"}, {"dx", deltaX}, {"dy", deltaY}});
}

void PyBulletClient::setModelOpacity(double opacity)
{
    sendCommand({{"cmd", "set_opacity"}, {"opacity", qBound(0.2, opacity, 1.0)}});
}

void PyBulletClient::retryPackagedConnection()
{
    if (!m_packagedMode || !m_serverStarted || isConnected()
        || m_process->state() == QProcess::NotRunning)
        return;
    ++m_connectRetries;
    connectToServer(m_serverPort);
}

// ---- QProcess Slots ----
void PyBulletClient::onProcessReadyReadStdout()
{
    while (m_process->canReadLine()) {
        QString line = QString::fromUtf8(m_process->readLine()).trimmed();
        if (!m_serverStarted && line.startsWith("READY ")) {
            bool ok = false;
            int port = line.mid(6).toInt(&ok);
            if (ok && port > 0) {
                m_serverStarted = true;
                connectToServer(port);
            }
        }
    }
    // 握手之后 stdout 的内容一律不再需要。地形回放时 Python 端用 '\r' 结尾打进度，
    // 这些字节永远凑不成完整行，canReadLine() 取不走，会让 QProcess 内部缓冲无限增长。
    if (m_serverStarted)
        m_process->readAllStandardOutput();
}

void PyBulletClient::onProcessReadyReadStderr()
{
    QByteArray data = m_process->readAllStandardError();
    QString text = QString::fromUtf8(data).trimmed();
    if (!text.isEmpty()) {
        emit errorOccurred("Python: " + text);
    }
}

void PyBulletClient::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(exitCode);
    Q_UNUSED(status);
    m_serverStarted = false;
    emit serverStopped();
}

// ---- QTcpSocket Slots ----
void PyBulletClient::onSocketConnected()
{
    m_connectRetries = 0;
    emit serverReady();
}

void PyBulletClient::onSocketDisconnected()
{
    m_readBuffer.clear();
}

void PyBulletClient::onSocketReadyRead()
{
    m_readBuffer.append(m_socket->readAll());

    // 解析完整消息
    while (m_readBuffer.size() >= 4) {
        quint32 msgLen = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar*>(m_readBuffer.constData()));

        if (static_cast<qsizetype>(msgLen) > m_readBuffer.size() - 4) {
            break;  // 数据不完整，等待更多
        }

        QByteArray payload = m_readBuffer.mid(4, static_cast<qsizetype>(msgLen));
        m_readBuffer.remove(0, 4 + static_cast<int>(msgLen));

        processMessage(payload);
    }
}

void PyBulletClient::onSocketError(QAbstractSocket::SocketError error)
{
    if (m_packagedMode && m_serverStarted
        && m_process->state() != QProcess::NotRunning
        && m_connectRetries < 60) {
        QTimer::singleShot(250, this, &PyBulletClient::retryPackagedConnection);
        return;
    }
    if (error != QAbstractSocket::RemoteHostClosedError) {
        emit errorOccurred("Socket 错误: " + m_socket->errorString());
    }
}

// ---- 消息解析 ----
void PyBulletClient::processMessage(const QByteArray &payload)
{
    if (payload.size() < 1) return;

    quint8 type = static_cast<quint8>(payload[0]);
    QByteArray data = payload.mid(1);

    switch (type) {
    case MSG_LOG: {
        QJsonObject obj = QJsonDocument::fromJson(data).object();
        emit logReceived(obj["level"].toString(), obj["msg"].toString());
        break;
    }
    case MSG_STATUS: {
        QJsonObject obj = QJsonDocument::fromJson(data).object();
        // 处理窗口句柄
        if (obj["status"].toString() == "window_ready") {
            qint64 hwnd = obj["hwnd"].toInteger();
            if (hwnd != 0) {
                emit windowHandleReceived(static_cast<quintptr>(hwnd));
            }
        }
        emit statusReceived(obj);
        break;
    }
    default:
        break;
    }
}
