#include "sshclient.h"
#include <QJsonDocument>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QtEndian>
#include <QProcessEnvironment>

static constexpr quint8 MSG_LOG    = 0x4C;
static constexpr quint8 MSG_STATUS = 0x53;

SshClient::SshClient(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
    , m_socket(new QTcpSocket(this))
{
    QString appDir = QCoreApplication::applicationDirPath();
#ifdef SRCDIR
    m_helperScript = QStringLiteral(SRCDIR "/log_ssh_helper.py");
#else
    m_helperScript = appDir + "/log_ssh_helper.py";
#endif
    m_helperExe = appDir + "/log_ssh_helper.exe";

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &SshClient::onProcessReadyReadStdout);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &SshClient::onProcessReadyReadStderr);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &SshClient::onProcessFinished);

    connect(m_socket, &QTcpSocket::connected,
            this, &SshClient::onSocketConnected);
    connect(m_socket, &QTcpSocket::readyRead,
            this, &SshClient::onSocketReadyRead);
}

SshClient::~SshClient()
{
    stopHelper();
}

void SshClient::startHelper()
{
    if (m_process->state() != QProcess::NotRunning)
        return;

    bool useExe = QFileInfo::exists(m_helperExe);
    if (!useExe && !QFileInfo::exists(m_helperScript)) {
        emit errorOccurred("找不到 SSH 助手: " + m_helperScript);
        return;
    }

    m_helperStarted = false;
    m_readBuffer.clear();

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONIOENCODING", "utf-8");
    env.insert("PYTHONUTF8", "1");
    m_process->setProcessEnvironment(env);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);

    if (useExe) {
        m_process->start(m_helperExe, {"0"});
    } else {
        m_process->start("python", {m_helperScript, "0"});
    }
}

void SshClient::stopHelper()
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
        m_socket->waitForDisconnected(1000);
    }
    if (m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(3000)) {
            m_process->kill();
            m_process->waitForFinished(1000);
        }
    }
    m_helperStarted = false;
}

bool SshClient::isHelperRunning() const
{
    return m_helperStarted && m_socket->state() == QAbstractSocket::ConnectedState;
}

// ---- 命令接口 ----

void SshClient::connectHost(const QString &host, int port,
                            const QString &user, const QString &password)
{
    sendCommand({
        {"cmd", "connect"},
        {"host", host},
        {"port", port},
        {"user", user},
        {"password", password}
    });
}

void SshClient::disconnectHost(const QString &host, int port)
{
    sendCommand({
        {"cmd", "disconnect"},
        {"host", host},
        {"port", port}
    });
}

void SshClient::listFiles(const QString &host, int port, const QString &remotePath)
{
    sendCommand({
        {"cmd", "list_files"},
        {"host", host},
        {"port", port},
        {"remote_path", remotePath}
    });
}

void SshClient::getRobotInfo(const QString &host, int port)
{
    sendCommand({
        {"cmd", "get_robot_info"},
        {"host", host},
        {"port", port}
    });
}

void SshClient::downloadFiles(const QString &host, int port,
                              const QJsonArray &files,
                              const QString &localDir, const QString &zipName)
{
    sendCommand({
        {"cmd", "download"},
        {"host", host},
        {"port", port},
        {"files", files},
        {"local_dir", localDir},
        {"zip_name", zipName}
    });
}

void SshClient::execCommand(const QString &host, int port, const QString &command)
{
    sendCommand({
        {"cmd", "exec_cmd"},
        {"host", host},
        {"port", port},
        {"command", command}
    });
}

// ---- 内部通信 ----

void SshClient::sendCommand(const QJsonObject &cmd)
{
    if (m_socket->state() != QAbstractSocket::ConnectedState)
        return;
    QByteArray payload = QJsonDocument(cmd).toJson(QJsonDocument::Compact);
    quint32 len = static_cast<quint32>(payload.size());
    QByteArray header(4, '\0');
    qToBigEndian(len, reinterpret_cast<uchar*>(header.data()));
    m_socket->write(header + payload);
}

void SshClient::connectToHelper(int port)
{
    m_socket->connectToHost("127.0.0.1", static_cast<quint16>(port));
}

void SshClient::onProcessReadyReadStdout()
{
    while (m_process->canReadLine()) {
        QString line = QString::fromUtf8(m_process->readLine()).trimmed();
        if (!m_helperStarted && line.startsWith("READY ")) {
            bool ok = false;
            int port = line.mid(6).toInt(&ok);
            if (ok && port > 0) {
                m_helperStarted = true;
                connectToHelper(port);
            }
        }
    }
}

void SshClient::onProcessReadyReadStderr()
{
    QByteArray data = m_process->readAllStandardError();
    QString text = QString::fromUtf8(data).trimmed();
    if (!text.isEmpty())
        emit errorOccurred("SSH Helper: " + text);
}

void SshClient::onProcessFinished(int, QProcess::ExitStatus)
{
    m_helperStarted = false;
}

void SshClient::onSocketConnected()
{
    emit helperReady();
}

void SshClient::onSocketReadyRead()
{
    m_readBuffer.append(m_socket->readAll());

    while (m_readBuffer.size() >= 4) {
        quint32 len = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar*>(m_readBuffer.constData()));
        if (len > 10 * 1024 * 1024) {
            m_readBuffer.clear();
            return;
        }
        if (static_cast<quint32>(m_readBuffer.size()) < 4 + len)
            break;

        QByteArray payload = m_readBuffer.mid(4, static_cast<int>(len));
        m_readBuffer.remove(0, 4 + static_cast<int>(len));
        processMessage(payload);
    }
}

void SshClient::processMessage(const QByteArray &payload)
{
    if (payload.isEmpty()) return;

    quint8 type = static_cast<quint8>(payload[0]);
    QByteArray data = payload.mid(1);
    QJsonObject obj = QJsonDocument::fromJson(data).object();

    switch (type) {
    case MSG_LOG: {
        QString level = obj["level"].toString();
        QString msg = obj["msg"].toString();
        emit logMessage(level, msg);
        break;
    }
    case MSG_STATUS: {
        QString status = obj["status"].toString();
        if (status == "connected") {
            emit connected(obj["host"].toString());
        } else if (status == "connect_failed") {
            emit connectFailed(obj["host"].toString(), obj["error"].toString());
        } else if (status == "disconnected") {
            emit disconnected(obj["host"].toString());
        } else if (status == "file_list") {
            QVector<RemoteFileInfo> files;
            for (auto v : obj["files"].toArray()) {
                QJsonObject fo = v.toObject();
                RemoteFileInfo fi;
                fi.name = fo["name"].toString();
                fi.size = fo["size"].toInteger();
                fi.mtime = fo["mtime"].toInteger();
                fi.isDir = fo["is_dir"].toBool();
                files.append(fi);
            }
            emit fileListReceived(obj["host"].toString(),
                                  obj["path"].toString(), files);
        } else if (status == "robot_info") {
            emit robotInfoReceived(obj["sn"].toString(),
                                   obj["version"].toString(),
                                   obj["device_type"].toString());
        } else if (status == "download_progress") {
            emit downloadProgress(obj["current"].toInt(),
                                  obj["total"].toInt(),
                                  obj["file"].toString(),
                                  obj["percent"].toInt());
        } else if (status == "download_complete") {
            emit downloadComplete(obj["zip_path"].toString(),
                                  obj["total_files"].toInt());
        } else if (status == "download_error") {
            emit downloadError(obj["error"].toString());
        } else if (status == "exec_result") {
            emit execResult(obj["stdout"].toString(), obj["stderr"].toString());
        }
        break;
    }
    default:
        break;
    }
}
