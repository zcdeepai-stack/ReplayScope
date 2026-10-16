#include "defaultpage.h"
#include <QPainter>
#include <QPen>
#include <QFont>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QProcess>
#include <QDesktopServices>
#include <QUrl>
#include <QCoreApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QTimer>
#include <QThread>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

// ---- 本地桌面卡片样式 ----
static const char *CARD_LOCAL_STYLE = R"(
    QPushButton {
        background: rgba(6, 14, 24, 200);
        border: 1px solid #1a3050;
        border-radius: 12px;
        color: #80a8c0;
        padding: 28px 20px;
        font-family: "Microsoft YaHei";
        font-size: 14px;
        min-width: 220px;
        min-height: 130px;
    }
    QPushButton:hover {
        background: rgba(0, 40, 80, 220);
        border: 1px solid #00d4ff;
        color: #00d4ff;
    }
    QPushButton:pressed {
        background: rgba(0, 60, 120, 240);
        border: 2px solid #00d4ff;
    }
)";

// ---- Web 浏览器卡片样式 ----
static const char *CARD_WEB_STYLE = R"(
    QPushButton {
        background: rgba(6, 14, 24, 200);
        border: 1px solid #1a3050;
        border-radius: 12px;
        color: #80a8c0;
        padding: 28px 20px;
        font-family: "Microsoft YaHei";
        font-size: 14px;
        min-width: 220px;
        min-height: 130px;
    }
    QPushButton:hover {
        background: rgba(10, 20, 50, 220);
        border: 1px solid #58a6ff;
        color: #58a6ff;
    }
    QPushButton:pressed {
        background: rgba(20, 40, 80, 240);
        border: 2px solid #58a6ff;
    }
)";

DefaultPage::DefaultPage(QWidget *parent)
    : QWidget(parent)
{
    // 透明背景，由 paintEvent 绘制
    setAttribute(Qt::WA_StyledBackground, false);

    auto *mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(0, 0, 0, 24);

    mainLay->addStretch(3);

    // ---- 主标题 ----
    auto *titleLabel = new QLabel(QString::fromUtf8(
        "\u4e09\u7ef4\u590d\u73b0\u4e0e\u6570\u636e\u5206\u6790\u5de5\u5177"));
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet(
        "QLabel { color: #00d4ff; font-family: 'Microsoft YaHei';"
        "  font-size: 28px; font-weight: bold; background: transparent; }");
    mainLay->addWidget(titleLabel);

    mainLay->addSpacing(4);

    // ---- 副标题 ----
    auto *subLabel = new QLabel("RobotReplay   ·   3D Replay & Data Analysis");
    subLabel->setAlignment(Qt::AlignCenter);
    subLabel->setStyleSheet(
        "QLabel { color: #5080a0; font-family: 'Consolas';"
        "  font-size: 14px; background: transparent; }");
    mainLay->addWidget(subLabel);

    mainLay->addSpacing(36);

    // ---- 模式选择提示 ----
    auto *hintLabel = new QLabel(QString::fromUtf8("\u9009\u62e9\u8fd0\u884c\u6a21\u5f0f"));
    hintLabel->setAlignment(Qt::AlignCenter);
    hintLabel->setStyleSheet(
        "QLabel { color: #3c5a78; font-family: 'Microsoft YaHei';"
        "  font-size: 11px; background: transparent; }");
    mainLay->addWidget(hintLabel);

    mainLay->addSpacing(16);

    // ---- 两张卡片按钮 ----
    auto *cardLay = new QHBoxLayout;
    cardLay->setSpacing(40);

    m_btnLocal = new QPushButton(
        QString::fromUtf8("\u672c\u5730\u4e0a\u4f4d\u673a\n\n\u2190 \u5de6\u4fa7\u5bfc\u822a\u680f\u9009\u62e9\u529f\u80fd"));
    m_btnLocal->setStyleSheet(CARD_LOCAL_STYLE);
    m_btnLocal->setCursor(Qt::PointingHandCursor);

    m_btnWeb = new QPushButton(
        QString::fromUtf8("Web \u6d4f\u89c8\u5668\u672c\u5730\u670d\u52a1\n\n\u542f\u52a8\u672c\u5730\u670d\u52a1\n\u6d4f\u89c8\u5668\u81ea\u52a8\u6253\u5f00"));
    m_btnWeb->setStyleSheet(CARD_WEB_STYLE);
    m_btnWeb->setCursor(Qt::PointingHandCursor);

    cardLay->addStretch();
    cardLay->addWidget(m_btnLocal);
    cardLay->addWidget(m_btnWeb);
    cardLay->addStretch();
    mainLay->addLayout(cardLay);

    mainLay->addSpacing(12);

    // ---- 状态标签 ----
    m_statusLabel = new QLabel;
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet(
        "QLabel { color: #3c5a78; font-family: 'Microsoft YaHei';"
        "  font-size: 10px; background: transparent; }");
    mainLay->addWidget(m_statusLabel);

    mainLay->addStretch(2);

    // ---- 底部版本号 ----
    auto *verLabel = new QLabel("v2.0");
    verLabel->setAlignment(Qt::AlignCenter);
    verLabel->setStyleSheet(
        "QLabel { color: #283c50; font-family: 'Consolas';"
        "  font-size: 9px; background: transparent; }");
    mainLay->addWidget(verLabel);

    // ---- 信号连接 ----
    connect(m_btnLocal, &QPushButton::clicked, this, &DefaultPage::onLocalClicked);
    connect(m_btnWeb,   &QPushButton::clicked, this, &DefaultPage::onWebClicked);
}

DefaultPage::~DefaultPage()
{
    // 结束自己拉起的后端。不再 taskkill /IM —— 那会误杀不属于本程序的实例。
    if (m_webServer && m_webServer->state() != QProcess::NotRunning) {
        m_webServer->terminate();
        if (!m_webServer->waitForFinished(2000)) {
            m_webServer->kill();
            m_webServer->waitForFinished(1000);
        }
    }
#ifdef Q_OS_WIN
    // 关闭作业对象。即便上面的正常终止没走到（例如崩溃退出），
    // 系统也会在句柄关闭时结束作业内的全部进程。
    if (m_jobHandle) {
        CloseHandle(static_cast<HANDLE>(m_jobHandle));
        m_jobHandle = nullptr;
    }
#endif
}

// ---- 背景绘制（渐变 + 辉光 + 装饰线） ----
void DefaultPage::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int w = width();
    int h = height();

    // 深色背景渐变
    QLinearGradient bg(0, 0, 0, h);
    bg.setColorAt(0.0, QColor(8, 12, 20));
    bg.setColorAt(0.5, QColor(10, 14, 22));
    bg.setColorAt(1.0, QColor(6, 10, 16));
    painter.fillRect(rect(), bg);

    // 中心辉光
    QRadialGradient glow(w / 2, h / 2 - 30, qMin(w, h) * 0.5);
    glow.setColorAt(0.0, QColor(0, 212, 255, 16));
    glow.setColorAt(0.5, QColor(0, 100, 180, 5));
    glow.setColorAt(1.0, QColor(0, 0, 0, 0));
    painter.fillRect(rect(), glow);

    // 四角装饰角标
    QPen cornerPen(QColor(0, 212, 255, 35), 1);
    painter.setPen(cornerPen);
    int c = 20;  // 角标长度
    int m = 12;  // 边距
    // 左上
    painter.drawLine(m, m, m + c, m);
    painter.drawLine(m, m, m, m + c);
    // 右上
    painter.drawLine(w - m, m, w - m - c, m);
    painter.drawLine(w - m, m, w - m, m + c);
    // 左下
    painter.drawLine(m, h - m, m + c, h - m);
    painter.drawLine(m, h - m, m, h - m - c);
    // 右下
    painter.drawLine(w - m, h - m, w - m - c, h - m);
    painter.drawLine(w - m, h - m, w - m, h - m - c);
}

// ---- 点击本地桌面 ----
void DefaultPage::onLocalClicked()
{
    m_statusLabel->setStyleSheet(
        "QLabel { color: #00d4ff; font-family: 'Microsoft YaHei';"
        "  font-size: 10px; background: transparent; }");
    m_statusLabel->setText(
        QString::fromUtf8("\u2190 \u8bf7\u4ece\u5de6\u4fa7\u5bfc\u822a\u680f\u9009\u62e9\u578b\u53f7\u6216\u529f\u80fd"));
    emit localModeSelected();
}

// ---- 辅助 ----
void DefaultPage::setStatus(const QString &text, const QString &color)
{
    m_statusLabel->setStyleSheet(
        QString("QLabel { color: %1; font-family: 'Microsoft YaHei';"
                "  font-size: 10px; background: transparent; }").arg(color));
    m_statusLabel->setText(text);
}

bool DefaultPage::backendAlive(int timeoutMs)
{
    // 只认自家服务：命中 /api/v1/health 且返回体里有 roboscope 标识。
    // 端口通不代表是我们的进程（可能被别的程序占用），必须验证内容。
    QNetworkAccessManager mgr;
    QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:9090/api/v1/health")));
    req.setTransferTimeout(timeoutMs);

    QNetworkReply *reply = mgr.get(req);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(timeoutMs + 200, &loop, &QEventLoop::quit);
    loop.exec();

    const bool ok = (reply->error() == QNetworkReply::NoError)
                    && reply->readAll().contains("roboscope");
    reply->deleteLater();
    return ok;
}

void DefaultPage::bindToJobObject(qint64 pid)
{
#ifdef Q_OS_WIN
    // 把后端进程放进作业对象，并设置 KILL_ON_JOB_CLOSE：
    // 只要本进程结束（正常退出、崩溃、被任务管理器结束都算），
    // 系统会自动结束作业里的所有进程，因此不可能留下孤儿后端。
    if (!m_jobHandle) {
        HANDLE job = CreateJobObjectW(nullptr, nullptr);
        if (!job) return;
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                     &info, sizeof(info))) {
            CloseHandle(job);
            return;
        }
        m_jobHandle = job;
    }
    HANDLE proc = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE,
                              static_cast<DWORD>(pid));
    if (proc) {
        AssignProcessToJobObject(static_cast<HANDLE>(m_jobHandle), proc);
        CloseHandle(proc);
    }
#else
    Q_UNUSED(pid);
#endif
}

// ---- 点击「Web 浏览器本地服务」----
void DefaultPage::onWebClicked()
{
    m_btnWeb->setEnabled(false);

    // 1) 已有后端在跑就直接开浏览器。不再无条件 taskkill —— 那会误杀
    //    用户自己起的实例，也会让每次点击都重启一遍服务。
    if (backendAlive()) {
        setStatus(QString::fromUtf8("服务已在运行，正在打开浏览器…"), "#58a6ff");
        QDesktopServices::openUrl(QUrl(QStringLiteral("http://localhost:9090")));
        m_btnWeb->setText(QString::fromUtf8("Web 浏览器本地服务\n\n服务运行中\n点击重新打开浏览器"));
        m_btnWeb->setEnabled(true);
        return;
    }

    // 2) 定位后端程序
    const QString appDir = QCoreApplication::applicationDirPath();
    QString serverExe;
#ifdef SRCDIR
    serverExe = QStringLiteral(SRCDIR "/roboscope-web/server/roboscope.exe");
#else
    serverExe = appDir + "/roboscope-web/server/roboscope.exe";
#endif
    if (!QFileInfo::exists(serverExe)) {
        setStatus(QString::fromUtf8("找不到本地服务程序，安装可能不完整"), "#ff6b6b");
        m_btnWeb->setEnabled(true);
        return;
    }

    // 3) 启动。同一时刻只允许一个：若已有自家进程在跑则复用，
    //    Go 端也会因为端口被占而直接退出，双重保险。
    if (m_webServer && m_webServer->state() != QProcess::NotRunning) {
        QDesktopServices::openUrl(QUrl(QStringLiteral("http://localhost:9090")));
        m_btnWeb->setEnabled(true);
        return;
    }

    if (!m_webServer) {
        m_webServer = new QProcess(this);
        connect(m_webServer, &QProcess::readyReadStandardError, this, [this]() {
            const QString err = QString::fromUtf8(m_webServer->readAllStandardError()).trimmed();
            if (!err.isEmpty()) setStatus(err.left(120), "#ff6b6b");
        });
    }

    m_webServer->setWorkingDirectory(QFileInfo(serverExe).absolutePath());
    m_webServer->start(serverExe);

    if (!m_webServer->waitForStarted(3000)) {
        setStatus(QString::fromUtf8("无法启动本地服务: ") + m_webServer->errorString(), "#ff6b6b");
        m_btnWeb->setEnabled(true);
        return;
    }
    bindToJobObject(m_webServer->processId());

    m_btnWeb->setText(QString::fromUtf8("Web 浏览器本地服务\n\n正在启动…"));
    setStatus(QString::fromUtf8("本地服务启动中，浏览器即将打开…"), "#58a6ff");

    // 4) 轮询探活，起来了再开浏览器（Go 端自身也会开一次，这里兜底）
    auto *tries = new int(0);
    auto *poll = new QTimer(this);
    poll->setInterval(400);
    connect(poll, &QTimer::timeout, this, [this, poll, tries]() {
        if (backendAlive(300)) {
            poll->stop(); poll->deleteLater(); delete tries;
            m_btnWeb->setText(QString::fromUtf8("Web 浏览器本地服务\n\n服务运行中\n点击重新打开浏览器"));
            setStatus(QString::fromUtf8("本地服务已启动 (http://localhost:9090)"), "#3fb950");
            m_btnWeb->setEnabled(true);
            return;
        }
        if (++(*tries) >= 20) {          // 约 8 秒
            poll->stop(); poll->deleteLater(); delete tries;
            m_btnWeb->setText(QString::fromUtf8("Web 浏览器本地服务\n\n启动本地服务\n浏览器自动打开"));
            setStatus(QString::fromUtf8("启动超时，端口 9090 可能被占用"), "#ff6b6b");
            m_btnWeb->setEnabled(true);
        }
    });
    poll->start();
}
