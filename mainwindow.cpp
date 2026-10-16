#include "mainwindow.h"
#include "pybulletclient.h"
#include "renderview.h"
#include "controlpanel.h"
#include "playerbar.h"
#include "logwidget.h"
#include "curveviewer.h"
#include "customanalyzer.h"
#include "logmanager.h"
#include "defaultpage.h"
#include "m20jointpanel.h"
#include "x30jointpanel.h"
#include "dr02jointpanel.h"
#include "screenrecorder.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QStackedWidget>
#include <QButtonGroup>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QFileDialog>
#include <QFileInfo>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QJsonDocument>
#include <QJsonArray>
#include <QApplication>
#include <QScreen>
#include <QTimer>
#include <QElapsedTimer>
#include <QDir>
#include <QDateTime>

static const char *DARK_BG = "#0a0e14";
static constexpr double FRAME_INTERVAL = 0.005; // 200 Hz

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // \u4e0e\u5b89\u88c5\u5305 / exe \u7248\u672c\u4fe1\u606f\u4e2d\u7684\u4ea7\u54c1\u540d\u4fdd\u6301\u4e00\u81f4
    setWindowTitle(QStringLiteral("ReplayScope"));

    // 允许窗口缩到比较小，笔记本/小分辨率屏上不至于被最小尺寸顶住。
    // 左侧控制面板与右侧关节面板都带滚动区，压缩后靠滚动查看，不会丢内容。
    setMinimumSize(900, 560);

    if (auto *scr = QApplication::primaryScreen()) {
        QRect avail = scr->availableGeometry();
        // 上限取屏幕可用区域，绝不超出：客户笔记本常见 1366x768（可用高约 728）
        int w = qBound(900, static_cast<int>(avail.width()  * 0.88), avail.width());
        int h = qBound(560, static_cast<int>(avail.height() * 0.90), avail.height());
        w = qMin(w, qMin(1440, avail.width()));
        h = qMin(h, qMin(900,  avail.height()));
        resize(w, h);
        move(avail.x() + (avail.width()  - w) / 2,
             avail.y() + (avail.height() - h) / 2);
    } else {
        resize(1280, 760);
    }
    initUi();
    connectSignals();
}

MainWindow::~MainWindow()
{
    if (m_client)
        m_client->stopServer();
    if (m_m20Client)
        m_m20Client->stopServer();
    if (m_dr02Client)
        m_dr02Client->stopServer();
    delete m_recordElapsed;
}

// ---- 侧边导航按钮样式 ----
static const char *NAV_BTN_STYLE = R"(
    QPushButton {
        background: transparent;
        color: #5080a0;
        border: none;
        border-left: 3px solid transparent;
        padding: 14px 8px;
        font-family: "Microsoft YaHei";
        font-size: 11px;
        font-weight: bold;
        text-align: center;
    }
    QPushButton:hover {
        background: #0f1a28;
        color: #80c0e0;
    }
    QPushButton:checked {
        background: qlineargradient(x1:0,y1:0,x2:1,y2:0,
                                    stop:0 #0a2040, stop:1 #0a0e14);
        color: #00d4ff;
        border-left: 3px solid #00d4ff;
    }
)";

void MainWindow::initUi()
{
    setStyleSheet(QString("QMainWindow { background-color: %1; }").arg(DARK_BG));

    auto *central = new QWidget;
    setCentralWidget(central);
    auto *rootLay = new QHBoxLayout(central);
    rootLay->setContentsMargins(0, 0, 0, 0);
    rootLay->setSpacing(0);

    // ---- 左侧导航栏 ----
    rootLay->addWidget(createNavBar());

    // ---- 右侧 QStackedWidget 多页面 ----
    m_stackedWidget = new QStackedWidget;
    m_stackedWidget->addWidget(new DefaultPage);       // index 0: 默认页
    m_stackedWidget->addWidget(createX30Page());       // index 1: X30
    m_stackedWidget->addWidget(createM20Page());       // index 2: M20
    m_stackedWidget->addWidget(createDR02Page());      // index 3: DR02
    m_customAnalyzer = new CustomAnalyzer;
    m_stackedWidget->addWidget(m_customAnalyzer);      // index 4: 波形分析
    m_logManager = new LogManager;
    m_stackedWidget->addWidget(m_logManager);          // index 5: 日志管理
    m_stackedWidget->setCurrentIndex(0);

    rootLay->addWidget(m_stackedWidget, 1);

    // ---- 数据分析浮动窗口 ----
    m_curveViewer = new CurveViewer(this);

    // ---- 三维画面录制 ----
    m_recorder = new ScreenRecorder(this);
    m_recordElapsed = new QElapsedTimer;
    m_recordUiTimer = new QTimer(this);
    m_recordUiTimer->setInterval(500);
    connect(m_recordUiTimer, &QTimer::timeout, this, &MainWindow::onRecordTick);

    connect(m_recorder, &ScreenRecorder::started, this,
            [this](const QString &path, const QSize &sz, int fps) {
        if (auto *cp = activeControlPanel()) cp->setRecordingState(true);
        m_recordElapsed->restart();
        m_recordUiTimer->start();
        auto *lw = logWidgetForPage(m_recordPage);
        lw->addInfo(QString("开始录制: %1  (%2×%3 @%4fps)")
                        .arg(QFileInfo(path).fileName())
                        .arg(sz.width()).arg(sz.height()).arg(fps));
    });

    connect(m_recorder, &ScreenRecorder::finished, this,
            [this](const QString &path, int frames, qint64 bytes, int dropped) {
        m_recordUiTimer->stop();
        if (auto *cp = activeControlPanel()) cp->setRecordingState(false);
        auto *lw = logWidgetForPage(m_recordPage);
        // 绝对不能在这里弹模态框：PyBullet 覆盖窗口有 30ms 定时器不断 HWND_TOP 置顶，
        // 弹窗会被压到 3D 画面底下看不见，而模态又锁住整个程序，表现就是彻底卡死。
        QString msg = QString("录制完成: %1 帧, %2 MB -> %3")
                          .arg(frames).arg(bytes / 1048576.0, 0, 'f', 1)
                          .arg(QDir::toNativeSeparators(path));
        if (dropped > 0) msg += QString("（编码不及丢弃 %1 帧）").arg(dropped);
        lw->addInfo(msg);
        m_recordPage = 0;
    });

    connect(m_recorder, &ScreenRecorder::errorOccurred, this,
            [this](const QString &err) {
        m_recordUiTimer->stop();
        if (auto *cp = activeControlPanel()) cp->setRecordingState(false);
        auto *lw = logWidgetForPage(m_recordPage);
        lw->addError("录制失败: " + err);
        m_recordPage = 0;
    });
}

RenderView *MainWindow::activeRenderView() const
{
    if (m_currentPage == 1) return m_renderView;
    if (m_currentPage == 2) return m_m20RenderView;
    if (m_currentPage == 3) return m_dr02RenderView;
    return nullptr;
}

ControlPanel *MainWindow::activeControlPanel() const
{
    if (m_recordPage == 1) return m_controlPanel;
    if (m_recordPage == 2) return m_m20ControlPanel;
    if (m_recordPage == 3) return m_dr02ControlPanel;
    if (m_currentPage == 1) return m_controlPanel;
    if (m_currentPage == 2) return m_m20ControlPanel;
    if (m_currentPage == 3) return m_dr02ControlPanel;
    return nullptr;
}

LogWidget *MainWindow::logWidgetForPage(int page) const
{
    if (page == 2) return m_m20LogWidget;
    if (page == 3) return m_dr02LogWidget;
    return m_logWidget;
}

QWidget *MainWindow::createX30Page()
{
    auto *page = new QWidget;
    auto *mainLayout = new QVBoxLayout(page);
    mainLayout->setContentsMargins(10, 10, 10, 8);
    mainLayout->setSpacing(8);

    auto *topSplitter = new QSplitter(Qt::Horizontal);

    m_controlPanel = new ControlPanel;
    m_controlPanel->setTerrainOptionVisible(true);   // 只有 X30 支持地形回放
    m_renderView   = new RenderView;
    m_logWidget    = new LogWidget;
    m_x30JointPanel = new X30JointPanel;

    // 右侧垂直分割: 关节控制面板(上) + 日志(下)
    auto *rightSplitter = new QSplitter(Qt::Vertical);
    rightSplitter->addWidget(m_x30JointPanel);
    rightSplitter->addWidget(m_logWidget);
    rightSplitter->setStretchFactor(0, 3);
    rightSplitter->setStretchFactor(1, 1);
    rightSplitter->setSizes({480, 160});
    rightSplitter->setStyleSheet(R"(
        QSplitter::handle { background: #2a3545; height: 3px; }
        QSplitter::handle:hover { background: #00d4ff; }
    )");

    topSplitter->addWidget(m_controlPanel);
    topSplitter->addWidget(m_renderView);
    topSplitter->addWidget(rightSplitter);
    topSplitter->setStretchFactor(0, 1);
    topSplitter->setStretchFactor(1, 4);
    topSplitter->setStretchFactor(2, 1);
    topSplitter->setSizes({240, 720, 260});

    topSplitter->setStyleSheet(R"(
        QSplitter::handle { background: #2a3545; width: 3px; }
        QSplitter::handle:hover { background: #00d4ff; }
    )");

    m_playerBar = new PlayerBar;

    mainLayout->addWidget(topSplitter, 1);
    mainLayout->addWidget(m_playerBar, 0);

    return page;
}

QWidget *MainWindow::createM20Page()
{
    auto *page = new QWidget;
    auto *mainLayout = new QVBoxLayout(page);
    mainLayout->setContentsMargins(10, 10, 10, 8);
    mainLayout->setSpacing(8);

    auto *topSplitter = new QSplitter(Qt::Horizontal);

    m_m20ControlPanel = new ControlPanel;
    m_m20ControlPanel->setTerrainOptionVisible(false);  // M20 服务端不支持地形
    m_m20RenderView   = new RenderView;
    m_m20LogWidget    = new LogWidget;
    m_m20JointPanel   = new M20JointPanel;

    // 右侧垂直分割: 关节控制面板(上) + 日志(下)
    auto *rightSplitter = new QSplitter(Qt::Vertical);
    rightSplitter->addWidget(m_m20JointPanel);
    rightSplitter->addWidget(m_m20LogWidget);
    rightSplitter->setStretchFactor(0, 3);
    rightSplitter->setStretchFactor(1, 1);
    rightSplitter->setSizes({480, 160});
    rightSplitter->setStyleSheet(R"(
        QSplitter::handle { background: #2a3545; height: 3px; }
        QSplitter::handle:hover { background: #00d4ff; }
    )");

    topSplitter->addWidget(m_m20ControlPanel);
    topSplitter->addWidget(m_m20RenderView);
    topSplitter->addWidget(rightSplitter);
    topSplitter->setStretchFactor(0, 1);
    topSplitter->setStretchFactor(1, 4);
    topSplitter->setStretchFactor(2, 1);
    topSplitter->setSizes({240, 720, 260});

    topSplitter->setStyleSheet(R"(
        QSplitter::handle { background: #2a3545; width: 3px; }
        QSplitter::handle:hover { background: #00d4ff; }
    )");

    m_m20PlayerBar = new PlayerBar;

    mainLayout->addWidget(topSplitter, 1);
    mainLayout->addWidget(m_m20PlayerBar, 0);

    return page;
}

QWidget *MainWindow::createDR02Page()
{
    auto *page = new QWidget;
    auto *mainLayout = new QVBoxLayout(page);
    mainLayout->setContentsMargins(10, 10, 10, 8);
    mainLayout->setSpacing(8);

    auto *topSplitter = new QSplitter(Qt::Horizontal);
    m_dr02ControlPanel = new ControlPanel;
    m_dr02ControlPanel->setTerrainOptionVisible(false);
    m_dr02RenderView = new RenderView;
    m_dr02LogWidget = new LogWidget;
    m_dr02JointPanel = new DR02JointPanel;

    auto *rightSplitter = new QSplitter(Qt::Vertical);
    rightSplitter->addWidget(m_dr02JointPanel);
    rightSplitter->addWidget(m_dr02LogWidget);
    rightSplitter->setStretchFactor(0, 3);
    rightSplitter->setStretchFactor(1, 1);
    rightSplitter->setSizes({480, 160});
    rightSplitter->setStyleSheet(R"(
        QSplitter::handle { background: #2a3545; height: 3px; }
        QSplitter::handle:hover { background: #00d4ff; }
    )");

    topSplitter->addWidget(m_dr02ControlPanel);
    topSplitter->addWidget(m_dr02RenderView);
    topSplitter->addWidget(rightSplitter);
    topSplitter->setStretchFactor(0, 1);
    topSplitter->setStretchFactor(1, 4);
    topSplitter->setStretchFactor(2, 1);
    topSplitter->setSizes({240, 720, 280});
    topSplitter->setStyleSheet(R"(
        QSplitter::handle { background: #2a3545; width: 3px; }
        QSplitter::handle:hover { background: #00d4ff; }
    )");

    m_dr02PlayerBar = new PlayerBar;
    mainLayout->addWidget(topSplitter, 1);
    mainLayout->addWidget(m_dr02PlayerBar, 0);
    return page;
}

QWidget *MainWindow::createNavBar()
{
    auto *nav = new QWidget;
    nav->setFixedWidth(72);
    nav->setStyleSheet("QWidget { background: #060a10; border-right: 1px solid #1a3050; }");

    auto *navLay = new QVBoxLayout(nav);
    navLay->setContentsMargins(0, 12, 0, 12);
    navLay->setSpacing(4);

    // Logo区去掉，直接分隔线 + 按钮
    navLay->addSpacing(8);

    // 页面切换按钮（互斥）
    m_navGroup = new QButtonGroup(this);
    m_navGroup->setExclusive(true);

    m_navHome = new QPushButton(QString::fromUtf8("\u9996\u9875"));
    m_navHome->setCheckable(true);
    m_navHome->setChecked(true);
    m_navHome->setStyleSheet(NAV_BTN_STYLE);

    m_navX30 = new QPushButton("X30");
    m_navX30->setCheckable(true);
    m_navX30->setStyleSheet(NAV_BTN_STYLE);

    m_navM20 = new QPushButton("M20");
    m_navM20->setCheckable(true);
    m_navM20->setStyleSheet(NAV_BTN_STYLE);

    m_navDR02 = new QPushButton("DR02");
    m_navDR02->setCheckable(true);
    m_navDR02->setStyleSheet(NAV_BTN_STYLE);

    m_navGroup->addButton(m_navHome, 0);
    m_navGroup->addButton(m_navX30,  1);
    m_navGroup->addButton(m_navM20,  2);
    m_navGroup->addButton(m_navDR02, 3);

    m_navWave = new QPushButton(QString::fromUtf8("\u6ce2\u5f62\n\u5206\u6790"));
    m_navWave->setCheckable(true);
    m_navWave->setStyleSheet(NAV_BTN_STYLE);
    m_navGroup->addButton(m_navWave, 4);

    m_navLog = new QPushButton(QString::fromUtf8("\u65e5\u5fd7\n\u7ba1\u7406"));
    m_navLog->setCheckable(true);
    m_navLog->setStyleSheet(NAV_BTN_STYLE);
    m_navGroup->addButton(m_navLog, 5);

    navLay->addWidget(m_navHome);
    navLay->addWidget(m_navX30);
    navLay->addWidget(m_navM20);
    navLay->addWidget(m_navDR02);
    navLay->addWidget(m_navWave);
    navLay->addWidget(m_navLog);

    // 分隔线
    auto *sep2 = new QFrame;
    sep2->setFrameShape(QFrame::HLine);
    sep2->setStyleSheet("QFrame { color: #1a3050; border: none; max-height: 1px; background: #1a3050; }");
    navLay->addSpacing(4);
    navLay->addWidget(sep2);
    navLay->addSpacing(4);

    // 数据分析按钮（独立 toggle）
    m_navCurve = new QPushButton(QString::fromUtf8("\u6570\u636e\n\u5206\u6790"));
    m_navCurve->setCheckable(true);
    m_navCurve->setStyleSheet(NAV_BTN_STYLE);

    navLay->addWidget(m_navCurve);
    navLay->addStretch();

    // 页面切换信号
    connect(m_navGroup, &QButtonGroup::idClicked, this, &MainWindow::switchToPage);

    // 数据分析浮动窗口 toggle
    connect(m_navCurve, &QPushButton::clicked, this, [this]() {
        if (m_curveViewer->isVisible()) {
            m_curveViewer->hide();
            m_navCurve->setChecked(false);
        } else {
            m_curveViewer->show();
            m_curveViewer->raise();
            m_curveViewer->activateWindow();
            m_navCurve->setChecked(true);
        }
    });

    return nav;
}

void MainWindow::connectSignals()
{
    // ---- X30 客户端 ----
    m_client = new PyBulletClient("x30PybusSimlu", this);
    connect(m_client, &PyBulletClient::logReceived,    this, &MainWindow::onLogReceived);
    connect(m_client, &PyBulletClient::statusReceived, this, &MainWindow::onStatusReceived);
    connect(m_client, &PyBulletClient::serverReady,    this, &MainWindow::onServerReady);
    connect(m_client, &PyBulletClient::serverStopped,  this, &MainWindow::onServerStopped);
    connect(m_client, &PyBulletClient::errorOccurred,  this, &MainWindow::onClientError);
    connect(m_client, &PyBulletClient::windowHandleReceived,
            this, &MainWindow::onWindowHandleReceived);

    connect(m_controlPanel, &ControlPanel::loadCsvClicked,     this, &MainWindow::onLoadCsv);
    connect(m_controlPanel, &ControlPanel::loadTerrainClicked, this, &MainWindow::onLoadTerrain);
    connect(m_controlPanel, &ControlPanel::terrainEnabledChanged,
            this, &MainWindow::onTerrainEnabledChanged);
    connect(m_controlPanel, &ControlPanel::screenshotClicked,  this, &MainWindow::onScreenshot);
    connect(m_controlPanel, &ControlPanel::recordToggled,      this, &MainWindow::onRecordToggled);
    connect(m_controlPanel, &ControlPanel::cameraPresetClicked, this, &MainWindow::onCameraPreset);
    connect(m_controlPanel, &ControlPanel::modelOpacityChanged,
            m_client, &PyBulletClient::setModelOpacity);
    connect(m_renderView, &RenderView::cameraPanRequested,
            m_client, &PyBulletClient::panCamera);

    connect(m_playerBar, &PlayerBar::playClicked,     this, &MainWindow::onPlayClicked);
    connect(m_playerBar, &PlayerBar::stopClicked,     this, &MainWindow::onStopClicked);
    connect(m_playerBar, &PlayerBar::speedChanged,    this, &MainWindow::onSpeedChanged);
    connect(m_playerBar, &PlayerBar::loopToggled,     this, &MainWindow::onLoopToggled);
    connect(m_playerBar, &PlayerBar::positionChanged, this, &MainWindow::onPositionChanged);

    connect(m_x30JointPanel, &X30JointPanel::jointValueChanged,
            this, &MainWindow::onX30JointSliderChanged);

    connect(m_curveViewer, &CurveViewer::csvLoaded, this,
            [this](const QString &path, int rows, int cols) {
                m_logWidget->addInfo(
                    QString("数据分析: 已加载 %1 (%2行 \u00d7 %3列)")
                        .arg(QFileInfo(path).fileName()).arg(rows).arg(cols));
            });

    connect(m_curveViewer, &CurveViewer::viewerClosed, this, [this]() {
        m_navCurve->setChecked(false);
    });

    // 勿用 QWidget::visibleChanged：Qt 5/6 的 QWidget 均无此信号。由 CurveViewer::showEvent/hideEvent 转发。
    connect(m_curveViewer, &CurveViewer::analysisVisibilityChanged, this,
            [this](bool visible) {
        const bool overlayOnTop = !visible;
        if (m_renderView)
            m_renderView->setRaiseOverlayToTop(overlayOnTop);
        if (m_m20RenderView)
            m_m20RenderView->setRaiseOverlayToTop(overlayOnTop);
        if (m_dr02RenderView)
            m_dr02RenderView->setRaiseOverlayToTop(overlayOnTop);
        if (visible) {
            m_curveViewer->raise();
            m_curveViewer->activateWindow();
            QTimer::singleShot(0, this, [this]() {
                if (m_curveViewer && m_curveViewer->isVisible()) {
                    m_curveViewer->raise();
                    m_curveViewer->activateWindow();
                }
            });
        }
    });

    // ---- M20 客户端（完全独立） ----
    m_m20Client = new PyBulletClient("M20pybusSimlu", this);
    connect(m_m20Client, &PyBulletClient::logReceived,    this, &MainWindow::onM20LogReceived);
    connect(m_m20Client, &PyBulletClient::statusReceived, this, &MainWindow::onM20StatusReceived);
    connect(m_m20Client, &PyBulletClient::serverReady,    this, &MainWindow::onM20ServerReady);
    connect(m_m20Client, &PyBulletClient::serverStopped,  this, &MainWindow::onM20ServerStopped);
    connect(m_m20Client, &PyBulletClient::errorOccurred,  this, &MainWindow::onM20ClientError);
    connect(m_m20Client, &PyBulletClient::windowHandleReceived,
            this, &MainWindow::onM20WindowHandleReceived);

    connect(m_m20ControlPanel, &ControlPanel::loadCsvClicked,     this, &MainWindow::onM20LoadCsv);
    connect(m_m20ControlPanel, &ControlPanel::loadTerrainClicked, this, &MainWindow::onM20LoadTerrain);
    connect(m_m20ControlPanel, &ControlPanel::screenshotClicked,  this, &MainWindow::onM20Screenshot);
    connect(m_m20ControlPanel, &ControlPanel::recordToggled,      this, &MainWindow::onRecordToggled);
    connect(m_m20ControlPanel, &ControlPanel::cameraPresetClicked, this, &MainWindow::onM20CameraPreset);
    connect(m_m20ControlPanel, &ControlPanel::modelOpacityChanged,
            m_m20Client, &PyBulletClient::setModelOpacity);
    connect(m_m20RenderView, &RenderView::cameraPanRequested,
            m_m20Client, &PyBulletClient::panCamera);

    connect(m_m20PlayerBar, &PlayerBar::playClicked,     this, &MainWindow::onM20PlayClicked);
    connect(m_m20PlayerBar, &PlayerBar::stopClicked,     this, &MainWindow::onM20StopClicked);
    connect(m_m20PlayerBar, &PlayerBar::speedChanged,    this, &MainWindow::onM20SpeedChanged);
    connect(m_m20PlayerBar, &PlayerBar::loopToggled,     this, &MainWindow::onM20LoopToggled);
    connect(m_m20PlayerBar, &PlayerBar::positionChanged, this, &MainWindow::onM20PositionChanged);

    connect(m_m20JointPanel, &M20JointPanel::jointValueChanged,
            this, &MainWindow::onM20JointSliderChanged);

    // ---- DR02 客户端（独立进程、独立页面状态） ----
    m_dr02Client = new PyBulletClient("DR02pybusSimlu", this);
    connect(m_dr02Client, &PyBulletClient::logReceived, this, &MainWindow::onDR02LogReceived);
    connect(m_dr02Client, &PyBulletClient::statusReceived, this, &MainWindow::onDR02StatusReceived);
    connect(m_dr02Client, &PyBulletClient::serverReady, this, &MainWindow::onDR02ServerReady);
    connect(m_dr02Client, &PyBulletClient::serverStopped, this, &MainWindow::onDR02ServerStopped);
    connect(m_dr02Client, &PyBulletClient::errorOccurred, this, &MainWindow::onDR02ClientError);
    connect(m_dr02Client, &PyBulletClient::windowHandleReceived,
            this, &MainWindow::onDR02WindowHandleReceived);

    connect(m_dr02ControlPanel, &ControlPanel::loadCsvClicked, this, &MainWindow::onDR02LoadCsv);
    connect(m_dr02ControlPanel, &ControlPanel::screenshotClicked, this, &MainWindow::onDR02Screenshot);
    connect(m_dr02ControlPanel, &ControlPanel::recordToggled, this, &MainWindow::onRecordToggled);
    connect(m_dr02ControlPanel, &ControlPanel::cameraPresetClicked,
            this, &MainWindow::onDR02CameraPreset);
    connect(m_dr02ControlPanel, &ControlPanel::modelOpacityChanged,
            m_dr02Client, &PyBulletClient::setModelOpacity);
    connect(m_dr02RenderView, &RenderView::cameraPanRequested,
            m_dr02Client, &PyBulletClient::panCamera);

    connect(m_dr02PlayerBar, &PlayerBar::playClicked, this, &MainWindow::onDR02PlayClicked);
    connect(m_dr02PlayerBar, &PlayerBar::stopClicked, this, &MainWindow::onDR02StopClicked);
    connect(m_dr02PlayerBar, &PlayerBar::speedChanged, this, &MainWindow::onDR02SpeedChanged);
    connect(m_dr02PlayerBar, &PlayerBar::loopToggled, this, &MainWindow::onDR02LoopToggled);
    connect(m_dr02PlayerBar, &PlayerBar::positionChanged, this, &MainWindow::onDR02PositionChanged);
    connect(m_dr02JointPanel, &DR02JointPanel::jointValueChanged,
            this, &MainWindow::onDR02JointSliderChanged);
}

void MainWindow::startX30Server()
{
    if (m_x30ServerStarted) return;
    m_x30ServerStarted = true;
    m_logWidget->addInfo("正在启动 X30 PyBullet 渲染引擎...");
    m_client->startServer("python");
}

void MainWindow::startM20Server()
{
    if (m_m20ServerStarted) return;
    m_m20ServerStarted = true;
    m_m20LogWidget->addInfo("正在启动 M20 PyBullet 渲染引擎...");
    m_m20Client->startServer("python");
}

void MainWindow::startDR02Server()
{
    if (m_dr02ServerStarted) return;
    m_dr02ServerStarted = true;
    m_dr02LogWidget->addInfo("正在启动 DR02 人形渲染引擎...");
    m_dr02Client->startServer("python");
}

void MainWindow::switchToPage(int index)
{
    // 离开当前页会销毁 PyBullet 窗口，录制必须先收尾，否则会抓到无效句柄
    if (index != m_currentPage)
        stopRecordingIfAny();

    m_currentPage = index;
    m_stackedWidget->setCurrentIndex(index);

    // 离开某页时释放对端 PyBullet 进程与 GPU，避免双开卡顿
    if (index == 0) {
        shutdownX30Resources();
        shutdownM20Resources();
        shutdownDR02Resources();
    } else if (index == 1) {
        shutdownM20Resources();
        shutdownDR02Resources();
        startX30Server();
    } else if (index == 2) {
        shutdownX30Resources();
        shutdownDR02Resources();
        startM20Server();
    } else if (index == 3) {
        shutdownX30Resources();
        shutdownM20Resources();
        startDR02Server();
    } else if (index == 4 || index == 5) {
        // 波形分析页/日志管理页，不需要 PyBullet 服务
        shutdownX30Resources();
        shutdownM20Resources();
        shutdownDR02Resources();
    }
}

void MainWindow::shutdownX30Resources()
{
    if (!m_client)
        return;

    if (m_client->isConnected())
        m_client->stop();
    m_client->stopServer();

    m_x30ServerStarted = false;
    m_renderView->clear();
    m_isPlaybackActive = false;
    m_isPaused = false;
    m_totalFrames = 0;
    m_pendingStartFrame = 0;
    m_pendingEndFrame = 0;
    m_csvPath.clear();
    m_terrainPath.clear();
    m_playerBar->resetTimeline();
    m_controlPanel->resetDataDisplay();
    m_x30JointPanel->setInteractive(true);
}

void MainWindow::shutdownM20Resources()
{
    if (!m_m20Client)
        return;

    if (m_m20Client->isConnected())
        m_m20Client->stop();
    m_m20Client->stopServer();

    m_m20ServerStarted = false;
    m_m20RenderView->clear();
    m_m20IsPlaybackActive = false;
    m_m20IsPaused = false;
    m_m20TotalFrames = 0;
    m_m20PendingStartFrame = 0;
    m_m20PendingEndFrame = 0;
    m_m20CsvPath.clear();
    m_m20PlayerBar->resetTimeline();
    m_m20ControlPanel->resetDataDisplay();
    m_m20JointPanel->setInteractive(true);
}

void MainWindow::shutdownDR02Resources()
{
    if (!m_dr02Client)
        return;

    if (m_dr02Client->isConnected())
        m_dr02Client->stop();
    m_dr02Client->stopServer();

    m_dr02ServerStarted = false;
    m_dr02RenderView->clear();
    m_dr02IsPlaybackActive = false;
    m_dr02IsPaused = false;
    m_dr02TotalFrames = 0;
    m_dr02PendingStartFrame = 0;
    m_dr02PendingEndFrame = 0;
    m_dr02CsvPath.clear();
    m_dr02PlayerBar->resetTimeline();
    m_dr02ControlPanel->resetDataDisplay();
    m_dr02JointPanel->setInteractive(true);
}

// ---- UI 操作 ----
void MainWindow::onPlayClicked()
{
    if (!m_client->isConnected()) {
        m_logWidget->addError("PyBullet 服务未连接");
        m_playerBar->setPlaying(false);
        return;
    }

    if (m_playerBar->isPlaying()) {
        if (m_isPlaybackActive && m_isPaused) {
            m_client->resume();
            m_isPaused = false;
            m_controlPanel->setStatus("播放中");
            m_logWidget->addInfo("恢复播放");
        } else {
            if (m_csvPath.isEmpty()) {
                m_logWidget->addError("请先选择关节数据CSV文件");
                m_playerBar->setPlaying(false);
                return;
            }
            // CSV 选了但服务端解析失败（缺列等）时 m_totalFrames 仍为 0，
            // 此时 qBound(0, x, -1) 的 min>max 在 Debug 版会直接断言崩溃
            if (m_totalFrames <= 0) {
                m_logWidget->addError("关节数据尚未就绪，请重新选择CSV文件");
                m_playerBar->setPlaying(false);
                return;
            }

            int startFrame = qBound(0, m_pendingStartFrame, m_totalFrames - 1);
            int endFrame = qBound(startFrame + 1, m_pendingEndFrame, m_totalFrames);

            // 播放前重新确认地形开关与文件状态（文件可能在选择之后被删除/移动）
            syncX30Terrain();
            m_client->setSpeed(m_frameSpeed);
            m_client->setLoop(m_playerBar->isLoop());
            m_client->play(startFrame, endFrame);

            m_isPlaybackActive = true;
            m_isPaused = false;
            m_controlPanel->setStatus("播放中");
            m_x30JointPanel->setInteractive(false);

            double tStart = startFrame * FRAME_INTERVAL;
            double tEnd   = endFrame * FRAME_INTERVAL;
            m_logWidget->addInfo(
                QString("播放: 帧 %1-%2 (%3s-%4s)")
                    .arg(startFrame).arg(endFrame)
                    .arg(tStart, 0, 'f', 1).arg(tEnd, 0, 'f', 1));
        }
    } else {
        if (m_isPlaybackActive) {
            m_client->pause();
            m_isPaused = true;
            m_controlPanel->setStatus("已暂停");
            m_logWidget->addInfo("暂停播放");
        }
    }
}

void MainWindow::onStopClicked()
{
    m_client->stop();
    m_isPlaybackActive = false;
    m_isPaused = false;
    m_playerBar->setPlaying(false);
    m_controlPanel->setStatus("已停止");
    m_x30JointPanel->setInteractive(true);
    m_logWidget->addInfo("停止回放");
}

void MainWindow::onSpeedChanged(int idx)
{
    static const double speeds[] = {0.01, 0.005, 0.003, 0.001};
    static const char *labels[]  = {"0.5x", "1x", "1.5x", "2x"};
    if (idx < 0 || idx >= 4) return;
    m_frameSpeed = speeds[idx];
    m_client->setSpeed(m_frameSpeed);
    m_logWidget->addInfo(QString("播放速度: %1").arg(labels[idx]));
}

void MainWindow::onLoopToggled(bool loop)
{
    m_client->setLoop(loop);
    m_logWidget->addInfo(QString("循环播放: %1").arg(loop ? "开启" : "关闭"));
}

void MainWindow::onPositionChanged(int value)
{
    if (m_totalFrames > 0) {
        int targetFrame = static_cast<int>(qint64(value) * m_totalFrames / 1000);
        m_pendingStartFrame = targetFrame;
        m_pendingEndFrame = m_totalFrames;

        double sec = targetFrame * FRAME_INTERVAL;
        m_logWidget->addInfo(QString("跳转: %1s (帧:%2)").arg(sec, 0, 'f', 1).arg(targetFrame));

        if (m_isPlaybackActive) {
            m_client->seek(targetFrame);
        }
    }
}

void MainWindow::onLoadCsv()
{
    m_renderView->setOverlayVisible(false);
    QString path = QFileDialog::getOpenFileName(
        this, "请选择关节数据CSV文件", "", "CSV文件 (*.csv)");
    m_renderView->setOverlayVisible(true);
    if (path.isEmpty()) return;

    m_csvPath = path;
    m_client->loadCsv(path);

    QString filename = QFileInfo(path).fileName();
    m_controlPanel->setJointLabel("已加载: " + filename);
    m_logWidget->addInfo("正在加载关节数据: " + filename);
}

void MainWindow::onLoadTerrain()
{
    m_renderView->setOverlayVisible(false);
    QString path = QFileDialog::getOpenFileName(
        this, "请选择地形图CSV文件", "", "CSV文件 (*.csv)");
    m_renderView->setOverlayVisible(true);
    if (path.isEmpty()) return;

    m_terrainPath = path;

    QString filename = QFileInfo(path).fileName();
    m_controlPanel->setTerrainLabel("已选择: " + filename);
    m_logWidget->addInfo("地形图路径: " + filename);

    // 主动选了文件，视为希望加载；若之前未勾选则自动勾上
    if (!m_controlPanel->isTerrainEnabled())
        m_controlPanel->setTerrainEnabled(true);   // 触发 onTerrainEnabledChanged -> syncX30Terrain
    else
        syncX30Terrain();
}

void MainWindow::onTerrainEnabledChanged(bool enabled)
{
    // 切页时 resetDataDisplay 也会复位这个开关，那时服务已停止，不必刷日志
    if (m_client && m_client->isConnected()) {
        if (enabled && m_terrainPath.isEmpty())
            m_logWidget->addWarning("尚未选择地形图文件，播放时将不加载地形");
        else
            m_logWidget->addInfo(QString("播放时加载地形图: %1").arg(enabled ? "开启" : "关闭"));
    }
    syncX30Terrain();
}

void MainWindow::syncX30Terrain()
{
    if (!m_client || !m_client->isConnected())
        return;

    // 最终结论：勾选了 且 选过文件 且 文件确实存在
    const bool wanted = m_controlPanel->isTerrainEnabled();
    const bool usable = wanted && !m_terrainPath.isEmpty()
                        && QFileInfo::exists(m_terrainPath);

    if (wanted && !m_terrainPath.isEmpty() && !usable)
        m_logWidget->addWarning("地形图文件不存在，播放时将不加载: " + m_terrainPath);

    m_client->loadTerrain(usable ? m_terrainPath : QString(), usable);
}

void MainWindow::onX30JointSliderChanged(int jointIndex, double value)
{
    if (m_client && m_client->isConnected() && !m_isPlaybackActive) {
        m_client->setJointPosition(jointIndex, value);
    }
}

// ---- PyBullet 回调 ----
void MainWindow::onLogReceived(const QString &level, const QString &message)
{
    if (level == "error")
        m_logWidget->addError(message);
    else if (level == "warn" || level == "warning")
        m_logWidget->addWarning(message);
    else
        m_logWidget->addInfo(message);
}

void MainWindow::onStatusReceived(const QJsonObject &status)
{
    QString st = status["status"].toString();

    if (st == "csv_loaded") {
        m_totalFrames = status["frames"].toInt();
        double duration = status["duration"].toDouble();
        m_controlPanel->updateInfo(m_totalFrames, duration);
        m_playerBar->setTotalFrames(m_totalFrames, FRAME_INTERVAL);
        m_pendingStartFrame = 0;
        m_pendingEndFrame = m_totalFrames;
        m_logWidget->addInfo(
            QString("数据就绪: %1帧, %2秒").arg(m_totalFrames).arg(duration, 0, 'f', 1));
    } else if (st == "terrain_loaded") {
        m_logWidget->addInfo("地形图已就绪: " + status["filename"].toString());
    } else if (st == "terrain_disabled") {
        m_controlPanel->setTerrainLabel(m_terrainPath.isEmpty()
                                        ? "未选择地形图文件"
                                        : "已选择(未启用): " + QFileInfo(m_terrainPath).fileName());
    } else if (st == "progress") {
        int frame = status["frame"].toInt();
        m_playerBar->setCurrentPosition(frame);

        // 关节面板实时跟随
        QJsonArray ja = status["joints"].toArray();
        if (!ja.isEmpty()) {
            QVector<double> positions;
            for (const auto &v : ja)
                positions.append(v.toDouble());
            m_x30JointPanel->updateJointPositions(positions);
        }

        // CurveViewer 播放光标联动
        if (m_curveViewer && m_curveViewer->isVisible()) {
            double timeSec = frame * FRAME_INTERVAL;
            m_curveViewer->setPlaybackTime(timeSec);
        }
    } else if (st == "finished") {
        m_isPlaybackActive = false;
        m_isPaused = false;
        m_playerBar->setPlaying(false);
        m_controlPanel->setStatus("播放完成");
        m_x30JointPanel->setInteractive(true);
        if (m_curveViewer) m_curveViewer->clearPlaybackCursor();
        // 不调用 m_renderView->clear()，保留 3D 场景可见可旋转
        m_logWidget->addInfo("播放完成");
    } else if (st == "error") {
        m_isPlaybackActive = false;
        m_isPaused = false;
        m_playerBar->setPlaying(false);
        m_controlPanel->setStatus("错误");
        m_x30JointPanel->setInteractive(true);
    } else if (st == "pong") {
        m_logWidget->addInfo("服务器连接正常");
    }
}

void MainWindow::onServerReady()
{
    m_logWidget->addInfo("PyBullet 渲染引擎已就绪");
    m_controlPanel->setStatus("就绪");
    m_renderView->setLoadingText("X30 模型加载中...");
    syncX30Terrain();   // 新起的服务端默认不加载地形，这里把界面上的选择同步过去
}

void MainWindow::onServerStopped()
{
    m_x30ServerStarted = false;
    m_logWidget->addWarning("PyBullet 服务已停止");
    m_renderView->clear();
    m_isPlaybackActive = false;
    m_isPaused = false;
    m_playerBar->setPlaying(false);
    m_x30JointPanel->setInteractive(true);
}

void MainWindow::onClientError(const QString &error)
{
    m_logWidget->addError(error);
}

void MainWindow::onWindowHandleReceived(quintptr hwnd)
{
    m_renderView->embedWindow(hwnd);
    m_client->setModelOpacity(m_controlPanel->modelOpacity());
    m_logWidget->addInfo("X30 3D渲染窗口已嵌入");
}

// ---- M20 回调 ----
void MainWindow::onM20LogReceived(const QString &level, const QString &message)
{
    if (level == "error")
        m_m20LogWidget->addError(message);
    else if (level == "warn" || level == "warning")
        m_m20LogWidget->addWarning(message);
    else
        m_m20LogWidget->addInfo(message);
}

void MainWindow::onM20StatusReceived(const QJsonObject &status)
{
    QString st = status["status"].toString();

    if (st == "csv_loaded") {
        m_m20TotalFrames = status["frames"].toInt();
        double duration = status["duration"].toDouble();
        m_m20FrameInterval = status["frame_interval"].toDouble(0.004);
        if (m_m20FrameInterval <= 0.0)
            m_m20FrameInterval = 0.004;
        static const double rates[] = {0.5, 1.0, 1.5, 2.0};
        const int speedIndex = qBound(0, m_m20PlayerBar->speedIndex(), 3);
        m_m20FrameSpeed = m_m20FrameInterval / rates[speedIndex];
        m_m20ControlPanel->updateInfo(m_m20TotalFrames, duration);
        m_m20PlayerBar->setTotalFrames(m_m20TotalFrames, m_m20FrameInterval);
        m_m20PendingStartFrame = 0;
        m_m20PendingEndFrame = m_m20TotalFrames;
        m_m20LogWidget->addInfo(
            QString("数据就绪: %1帧, %2秒, %3Hz, %4关节")
                .arg(m_m20TotalFrames).arg(duration, 0, 'f', 1)
                .arg(status["sample_rate"].toDouble(), 0, 'f', 1)
                .arg(status["joint_count"].toInt()));
    } else if (st == "progress") {
        int frame = status["frame"].toInt();
        m_m20PlayerBar->setCurrentPosition(frame);

        // 关节面板实时跟随
        QJsonArray ja = status["joints"].toArray();
        if (!ja.isEmpty()) {
            QVector<double> positions;
            for (const auto &v : ja)
                positions.append(v.toDouble());
            m_m20JointPanel->updateJointPositions(positions);
        }

        // CurveViewer 播放光标联动
        if (m_curveViewer && m_curveViewer->isVisible()) {
            double timeSec = frame * m_m20FrameInterval;
            m_curveViewer->setPlaybackTime(timeSec);
        }
    } else if (st == "finished") {
        m_m20IsPlaybackActive = false;
        m_m20IsPaused = false;
        m_m20PlayerBar->setPlaying(false);
        m_m20ControlPanel->setStatus("播放完成");
        m_m20JointPanel->setInteractive(true);
        if (m_curveViewer) m_curveViewer->clearPlaybackCursor();
        m_m20LogWidget->addInfo("播放完成");
    } else if (st == "error") {
        m_m20IsPlaybackActive = false;
        m_m20IsPaused = false;
        m_m20PlayerBar->setPlaying(false);
        m_m20ControlPanel->setStatus("错误");
        m_m20JointPanel->setInteractive(true);
    } else if (st == "pong") {
        m_m20LogWidget->addInfo("服务器连接正常");
    }
}

void MainWindow::onM20ServerReady()
{
    m_m20LogWidget->addInfo("M20 PyBullet 渲染引擎已就绪");
    m_m20ControlPanel->setStatus("就绪");
    m_m20RenderView->setLoadingText("M20 模型加载中...");
}

void MainWindow::onM20ServerStopped()
{
    m_m20LogWidget->addWarning("M20 PyBullet 服务已停止");
    m_m20RenderView->clear();
    m_m20ServerStarted = false;
    m_m20IsPlaybackActive = false;
    m_m20IsPaused = false;
    m_m20PlayerBar->setPlaying(false);
}

void MainWindow::onM20ClientError(const QString &error)
{
    m_m20LogWidget->addError(error);
}

void MainWindow::onM20WindowHandleReceived(quintptr hwnd)
{
    m_m20RenderView->embedWindow(hwnd);
    m_m20Client->setModelOpacity(m_m20ControlPanel->modelOpacity());
    m_m20LogWidget->addInfo("M20 3D渲染窗口已嵌入");
}

// ---- M20 播放控制 ----
void MainWindow::onM20PlayClicked()
{
    if (!m_m20Client->isConnected()) {
        m_m20LogWidget->addError("M20 PyBullet 服务未连接");
        m_m20PlayerBar->setPlaying(false);
        return;
    }

    if (m_m20PlayerBar->isPlaying()) {
        if (m_m20IsPlaybackActive && m_m20IsPaused) {
            m_m20Client->resume();
            m_m20IsPaused = false;
            m_m20ControlPanel->setStatus("播放中");
            m_m20LogWidget->addInfo("恢复播放");
        } else {
            if (m_m20CsvPath.isEmpty()) {
                m_m20LogWidget->addError("请先选择关节数据CSV文件");
                m_m20PlayerBar->setPlaying(false);
                return;
            }
            // 同 X30：解析失败时 m_m20TotalFrames 为 0，qBound 的 min>max 会断言
            if (m_m20TotalFrames <= 0) {
                m_m20LogWidget->addError("关节数据尚未就绪，请重新选择CSV文件");
                m_m20PlayerBar->setPlaying(false);
                return;
            }

            int startFrame = qBound(0, m_m20PendingStartFrame, m_m20TotalFrames - 1);
            int endFrame = qBound(startFrame + 1, m_m20PendingEndFrame, m_m20TotalFrames);

            m_m20Client->setSpeed(m_m20FrameSpeed);
            m_m20Client->setLoop(m_m20PlayerBar->isLoop());
            m_m20Client->play(startFrame, endFrame);

            m_m20IsPlaybackActive = true;
            m_m20IsPaused = false;
            m_m20ControlPanel->setStatus("播放中");
            m_m20JointPanel->setInteractive(false);

            double tStart = startFrame * m_m20FrameInterval;
            double tEnd   = endFrame * m_m20FrameInterval;
            m_m20LogWidget->addInfo(
                QString("播放: 帧 %1-%2 (%3s-%4s)")
                    .arg(startFrame).arg(endFrame)
                    .arg(tStart, 0, 'f', 1).arg(tEnd, 0, 'f', 1));
        }
    } else {
        if (m_m20IsPlaybackActive) {
            m_m20Client->pause();
            m_m20IsPaused = true;
            m_m20ControlPanel->setStatus("已暂停");
            m_m20LogWidget->addInfo("暂停播放");
        }
    }
}

void MainWindow::onM20StopClicked()
{
    m_m20Client->stop();
    m_m20IsPlaybackActive = false;
    m_m20IsPaused = false;
    m_m20PlayerBar->setPlaying(false);
    m_m20ControlPanel->setStatus("已停止");
    m_m20JointPanel->setInteractive(true);
    m_m20LogWidget->addInfo("停止回放");
}

void MainWindow::onM20SpeedChanged(int idx)
{
    static const double rates[] = {0.5, 1.0, 1.5, 2.0};
    static const char *labels[]  = {"0.5x", "1x", "1.5x", "2x"};
    if (idx < 0 || idx >= 4) return;
    m_m20FrameSpeed = m_m20FrameInterval / rates[idx];
    m_m20Client->setSpeed(m_m20FrameSpeed);
    m_m20LogWidget->addInfo(QString("播放速度: %1").arg(labels[idx]));
}

void MainWindow::onM20LoopToggled(bool loop)
{
    m_m20Client->setLoop(loop);
    m_m20LogWidget->addInfo(QString("循环播放: %1").arg(loop ? "开启" : "关闭"));
}

void MainWindow::onM20PositionChanged(int value)
{
    if (m_m20TotalFrames > 0) {
        int targetFrame = static_cast<int>(qint64(value) * m_m20TotalFrames / 1000);
        m_m20PendingStartFrame = targetFrame;
        m_m20PendingEndFrame = m_m20TotalFrames;

        double sec = targetFrame * m_m20FrameInterval;
        m_m20LogWidget->addInfo(QString("跳转: %1s (帧:%2)").arg(sec, 0, 'f', 1).arg(targetFrame));

        if (m_m20IsPlaybackActive) {
            m_m20Client->seek(targetFrame);
        }
    }
}

void MainWindow::onM20LoadCsv()
{
    m_m20RenderView->setOverlayVisible(false);
    QString path = QFileDialog::getOpenFileName(
        this, "请选择M20关节数据CSV文件", "", "CSV文件 (*.csv)");
    m_m20RenderView->setOverlayVisible(true);
    if (path.isEmpty()) return;

    m_m20CsvPath = path;
    m_m20Client->loadCsv(path);

    QString filename = QFileInfo(path).fileName();
    m_m20ControlPanel->setJointLabel("已加载: " + filename);
    m_m20LogWidget->addInfo("正在加载关节数据: " + filename);
}

void MainWindow::onM20LoadTerrain()
{
    m_m20RenderView->setOverlayVisible(false);
    QString path = QFileDialog::getOpenFileName(
        this, "请选择M20地形图CSV文件", "", "CSV文件 (*.csv)");
    m_m20RenderView->setOverlayVisible(true);
    if (path.isEmpty()) return;

    m_m20Client->loadTerrain(path, true);

    QString filename = QFileInfo(path).fileName();
    m_m20ControlPanel->setTerrainLabel("已加载: " + filename);
    m_m20LogWidget->addInfo("地形图路径: " + filename);
}

void MainWindow::onM20JointSliderChanged(int jointIndex, double value)
{
    if (m_m20Client && m_m20Client->isConnected() && !m_m20IsPlaybackActive) {
        m_m20Client->setJointPosition(jointIndex, value);
    }
}

// ---- DR02 回调与播放控制 ----
void MainWindow::onDR02LogReceived(const QString &level, const QString &message)
{
    if (level == "error")
        m_dr02LogWidget->addError(message);
    else if (level == "warn" || level == "warning")
        m_dr02LogWidget->addWarning(message);
    else
        m_dr02LogWidget->addInfo(message);
}

void MainWindow::onDR02StatusReceived(const QJsonObject &status)
{
    const QString state = status["status"].toString();
    if (state == "model_ready") {
        m_dr02LogWidget->addInfo(
            QString("DR02 模型就绪: %1 个可动关节").arg(status["joint_count"].toInt()));
    } else if (state == "csv_loaded") {
        m_dr02TotalFrames = status["frames"].toInt();
        const double duration = status["duration"].toDouble();
        m_dr02ControlPanel->updateInfo(m_dr02TotalFrames, duration);
        m_dr02PlayerBar->setTotalFrames(m_dr02TotalFrames, FRAME_INTERVAL);
        m_dr02PendingStartFrame = 0;
        m_dr02PendingEndFrame = m_dr02TotalFrames;
        m_dr02ControlPanel->setJointLabel("已加载: " + status["filename"].toString());
        m_dr02ControlPanel->setStatus("就绪");
        m_dr02LogWidget->addInfo(
            QString("数据就绪: %1 帧, %2 秒, %3")
                .arg(m_dr02TotalFrames).arg(duration, 0, 'f', 1)
                .arg(status["mapping"].toString()));
    } else if (state == "csv_error") {
        m_dr02TotalFrames = 0;
        m_dr02PlayerBar->resetTimeline();
        m_dr02ControlPanel->setStatus("数据错误");
    } else if (state == "progress") {
        const int frame = status["frame"].toInt();
        m_dr02PlayerBar->setCurrentPosition(frame);
        const QJsonArray values = status["joints"].toArray();
        if (!values.isEmpty()) {
            QVector<double> positions;
            positions.reserve(values.size());
            for (const auto &value : values)
                positions.append(value.toDouble());
            m_dr02JointPanel->updateJointPositions(positions);
        }
        if (m_curveViewer && m_curveViewer->isVisible())
            m_curveViewer->setPlaybackTime(frame * FRAME_INTERVAL);
    } else if (state == "finished") {
        m_dr02IsPlaybackActive = false;
        m_dr02IsPaused = false;
        m_dr02PlayerBar->setPlaying(false);
        m_dr02ControlPanel->setStatus("播放完成");
        m_dr02JointPanel->setInteractive(true);
        if (m_curveViewer) m_curveViewer->clearPlaybackCursor();
        m_dr02LogWidget->addInfo("播放完成");
    } else if (state == "error") {
        m_dr02IsPlaybackActive = false;
        m_dr02IsPaused = false;
        m_dr02PlayerBar->setPlaying(false);
        m_dr02ControlPanel->setStatus("错误");
        m_dr02JointPanel->setInteractive(true);
    } else if (state == "pong") {
        m_dr02LogWidget->addInfo("DR02 服务连接正常");
    }
}

void MainWindow::onDR02ServerReady()
{
    m_dr02LogWidget->addInfo("DR02 人形渲染引擎已连接");
    m_dr02ControlPanel->setStatus("就绪");
    m_dr02RenderView->setLoadingText("DR02 人形模型加载中...");
}

void MainWindow::onDR02ServerStopped()
{
    m_dr02ServerStarted = false;
    m_dr02LogWidget->addWarning("DR02 渲染服务已停止");
    m_dr02RenderView->clear();
    m_dr02IsPlaybackActive = false;
    m_dr02IsPaused = false;
    m_dr02PlayerBar->setPlaying(false);
    m_dr02JointPanel->setInteractive(true);
}

void MainWindow::onDR02ClientError(const QString &error)
{
    m_dr02LogWidget->addError(error);
}

void MainWindow::onDR02WindowHandleReceived(quintptr hwnd)
{
    m_dr02RenderView->embedWindow(hwnd);
    m_dr02Client->setModelOpacity(m_dr02ControlPanel->modelOpacity());
    m_dr02LogWidget->addInfo("DR02 人形三维窗口已嵌入");
}

void MainWindow::onDR02PlayClicked()
{
    if (!m_dr02Client->isConnected()) {
        m_dr02LogWidget->addError("DR02 渲染服务未连接");
        m_dr02PlayerBar->setPlaying(false);
        return;
    }

    if (m_dr02PlayerBar->isPlaying()) {
        if (m_dr02IsPlaybackActive && m_dr02IsPaused) {
            m_dr02Client->resume();
            m_dr02IsPaused = false;
            m_dr02ControlPanel->setStatus("播放中");
            m_dr02LogWidget->addInfo("恢复播放");
            return;
        }
        if (m_dr02CsvPath.isEmpty()) {
            m_dr02LogWidget->addError("请先选择 DR02 关节数据 CSV 文件");
            m_dr02PlayerBar->setPlaying(false);
            return;
        }
        if (m_dr02TotalFrames <= 0) {
            m_dr02LogWidget->addError("关节数据尚未就绪，请检查 CSV 列名");
            m_dr02PlayerBar->setPlaying(false);
            return;
        }

        const int start = qBound(0, m_dr02PendingStartFrame, m_dr02TotalFrames - 1);
        const int end = qBound(start + 1, m_dr02PendingEndFrame, m_dr02TotalFrames);
        m_dr02Client->setSpeed(m_dr02FrameSpeed);
        m_dr02Client->setLoop(m_dr02PlayerBar->isLoop());
        m_dr02Client->play(start, end);
        m_dr02IsPlaybackActive = true;
        m_dr02IsPaused = false;
        m_dr02ControlPanel->setStatus("播放中");
        m_dr02JointPanel->setInteractive(false);
        m_dr02LogWidget->addInfo(
            QString("播放: 帧 %1-%2 (%3s-%4s)")
                .arg(start).arg(end)
                .arg(start * FRAME_INTERVAL, 0, 'f', 1)
                .arg(end * FRAME_INTERVAL, 0, 'f', 1));
    } else if (m_dr02IsPlaybackActive) {
        m_dr02Client->pause();
        m_dr02IsPaused = true;
        m_dr02ControlPanel->setStatus("已暂停");
        m_dr02LogWidget->addInfo("暂停播放");
    }
}

void MainWindow::onDR02StopClicked()
{
    m_dr02Client->stop();
    m_dr02IsPlaybackActive = false;
    m_dr02IsPaused = false;
    m_dr02PlayerBar->setPlaying(false);
    m_dr02ControlPanel->setStatus("已停止");
    m_dr02JointPanel->setInteractive(true);
    m_dr02LogWidget->addInfo("停止回放");
}

void MainWindow::onDR02SpeedChanged(int idx)
{
    static const double speeds[] = {0.01, 0.005, 0.003, 0.001};
    static const char *labels[] = {"0.5x", "1x", "1.5x", "2x"};
    if (idx < 0 || idx >= 4) return;
    m_dr02FrameSpeed = speeds[idx];
    m_dr02Client->setSpeed(m_dr02FrameSpeed);
    m_dr02LogWidget->addInfo(QString("播放速度: %1").arg(labels[idx]));
}

void MainWindow::onDR02LoopToggled(bool loop)
{
    m_dr02Client->setLoop(loop);
    m_dr02LogWidget->addInfo(QString("循环播放: %1").arg(loop ? "开启" : "关闭"));
}

void MainWindow::onDR02PositionChanged(int value)
{
    if (m_dr02TotalFrames <= 0) return;
    const int target = static_cast<int>(qint64(value) * m_dr02TotalFrames / 1000);
    m_dr02PendingStartFrame = target;
    m_dr02PendingEndFrame = m_dr02TotalFrames;
    if (m_dr02IsPlaybackActive)
        m_dr02Client->seek(target);
}

void MainWindow::onDR02LoadCsv()
{
    m_dr02RenderView->setOverlayVisible(false);
    const QString path = QFileDialog::getOpenFileName(
        this, "请选择 DR02 关节数据 CSV 文件", "", "CSV文件 (*.csv)");
    m_dr02RenderView->setOverlayVisible(true);
    if (path.isEmpty()) return;

    m_dr02CsvPath = path;
    m_dr02TotalFrames = 0;
    m_dr02Client->loadCsv(path);
    const QString filename = QFileInfo(path).fileName();
    m_dr02ControlPanel->setJointLabel("正在读取: " + filename);
    m_dr02ControlPanel->setStatus("读取数据中");
    m_dr02LogWidget->addInfo("正在读取 DR02 关节数据: " + filename);
}

void MainWindow::onDR02JointSliderChanged(int jointIndex, double value)
{
    if (m_dr02Client && m_dr02Client->isConnected() && !m_dr02IsPlaybackActive)
        m_dr02Client->setJointPosition(jointIndex, value);
}

// ---- 录屏 ----
void MainWindow::onRecordToggled()
{
    if (m_recorder->isRecording()) {
        m_recorder->stop();
        return;
    }

    RenderView *view = activeRenderView();
    if (!view) return;
    auto *lw = logWidgetForPage(m_currentPage);

    if (view->grabScreenshot().isNull()) {
        lw->addError("录制失败: 3D 渲染画面尚不可用");
        return;
    }

    // 弹对话框期间必须先藏起覆盖窗口，否则文件框会被 PyBullet 窗口盖住
    view->setOverlayVisible(false);
    const QString robotName = m_currentPage == 3 ? "DR02"
                              : (m_currentPage == 2 ? "M20" : "X30");
    const QString suggested = QString("%1_%2.avi")
        .arg(robotName,
             QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    QString path = QFileDialog::getSaveFileName(
        this, "保存录像", suggested, "AVI 视频 (*.avi)");
    view->setOverlayVisible(true);
    if (path.isEmpty()) return;
    if (!path.endsWith(".avi", Qt::CaseInsensitive))
        path += ".avi";

    m_recordPage = m_currentPage;
    m_recorder->start(path, [view]() { return view->grabScreenshot(); }, 30);
}

void MainWindow::onRecordTick()
{
    if (!m_recorder->isRecording()) return;
    const qint64 ms = m_recordElapsed->elapsed();
    const QString text = QString("● REC  %1:%2")
        .arg(ms / 60000, 2, 10, QChar('0'))
        .arg((ms % 60000) / 1000, 2, 10, QChar('0'));
    if (auto *cp = activeControlPanel()) cp->setRecordingElapsed(text);
}

void MainWindow::stopRecordingIfAny()
{
    if (m_recorder && m_recorder->isRecording())
        m_recorder->stop();
}

// ---- 截图 ----
void MainWindow::onScreenshot()
{
    QPixmap px = m_renderView->grabScreenshot();
    if (px.isNull()) {
        m_logWidget->addWarning("截图失败: 渲染窗口不可用");
        return;
    }
    m_renderView->setOverlayVisible(false);
    QString path = QFileDialog::getSaveFileName(
        this, "保存截图", "screenshot.png", "PNG图片 (*.png);;JPG图片 (*.jpg)");
    m_renderView->setOverlayVisible(true);
    if (path.isEmpty()) return;

    px.save(path);
    m_logWidget->addInfo("截图已保存: " + QFileInfo(path).fileName());
}

void MainWindow::onM20Screenshot()
{
    QPixmap px = m_m20RenderView->grabScreenshot();
    if (px.isNull()) {
        m_m20LogWidget->addWarning("截图失败: 渲染窗口不可用");
        return;
    }
    m_m20RenderView->setOverlayVisible(false);
    QString path = QFileDialog::getSaveFileName(
        this, "保存截图", "screenshot.png", "PNG图片 (*.png);;JPG图片 (*.jpg)");
    m_m20RenderView->setOverlayVisible(true);
    if (path.isEmpty()) return;

    px.save(path);
    m_m20LogWidget->addInfo("截图已保存: " + QFileInfo(path).fileName());
}

void MainWindow::onDR02Screenshot()
{
    const QPixmap image = m_dr02RenderView->grabScreenshot();
    if (image.isNull()) {
        m_dr02LogWidget->addWarning("截图失败: DR02 渲染画面尚不可用");
        return;
    }
    m_dr02RenderView->setOverlayVisible(false);
    QString path = QFileDialog::getSaveFileName(
        this, "保存 DR02 截图", "DR02_screenshot.png", "PNG图片 (*.png);;JPG图片 (*.jpg)");
    m_dr02RenderView->setOverlayVisible(true);
    if (path.isEmpty()) return;
    image.save(path);
    m_dr02LogWidget->addInfo("截图已保存: " + QFileInfo(path).fileName());
}

// ---- 视角预设 ----
// X30: 基座高 ~2.0, M20: 基座高 ~0.4
static const double X30_CAM_TZ = 1.9;
static const double M20_CAM_TZ = 0.4;
static const double DR02_CAM_TZ = 1.0;

void MainWindow::onCameraPreset(int idx)
{
    if (!m_client || !m_client->isConnected()) return;
    switch (idx) {
    case 0: m_client->setCamera(1.5, 35, -30, 0, 0, X30_CAM_TZ);  break; // 默认
    case 1: m_client->setCamera(1.5, 0,   0,  0, 0, X30_CAM_TZ);  break; // 前视
    case 2: m_client->setCamera(1.5, 90,  0,  0, 0, X30_CAM_TZ);  break; // 侧视
    case 3: m_client->setCamera(2.0, 0, -89,  0, 0, X30_CAM_TZ);  break; // 俯视
    }
    m_logWidget->addInfo(QString("视角: %1").arg(
        QStringList{"默认", "前视", "侧视", "俯视"}.value(idx)));
}

void MainWindow::onM20CameraPreset(int idx)
{
    if (!m_m20Client || !m_m20Client->isConnected()) return;
    switch (idx) {
    case 0: m_m20Client->setCamera(1.0, 35, -25, 0, 0, M20_CAM_TZ); break; // 默认
    case 1: m_m20Client->setCamera(1.0, 0,   0,  0, 0, M20_CAM_TZ); break; // 前视
    case 2: m_m20Client->setCamera(1.0, 90,  0,  0, 0, M20_CAM_TZ); break; // 侧视
    case 3: m_m20Client->setCamera(1.5, 0, -89,  0, 0, M20_CAM_TZ); break; // 俯视
    }
    m_m20LogWidget->addInfo(QString("视角: %1").arg(
        QStringList{"默认", "前视", "侧视", "俯视"}.value(idx)));
}

void MainWindow::onDR02CameraPreset(int idx)
{
    if (!m_dr02Client || !m_dr02Client->isConnected()) return;
    switch (idx) {
    case 0: m_dr02Client->setCamera(2.8, 35, -12, 0, 0, DR02_CAM_TZ); break;
    case 1: m_dr02Client->setCamera(2.6, 0, 0, 0, 0, DR02_CAM_TZ); break;
    case 2: m_dr02Client->setCamera(2.6, 90, 0, 0, 0, DR02_CAM_TZ); break;
    case 3: m_dr02Client->setCamera(3.3, 0, -89, 0, 0, DR02_CAM_TZ); break;
    }
    m_dr02LogWidget->addInfo(QString("视角: %1").arg(
        QStringList{"默认", "前视", "侧视", "俯视"}.value(idx)));
}

// ---- 键盘事件 ----
void MainWindow::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Space:
        if (m_currentPage == 1) {
            m_playerBar->setPlaying(!m_playerBar->isPlaying());
            onPlayClicked();
        } else if (m_currentPage == 2) {
            m_m20PlayerBar->setPlaying(!m_m20PlayerBar->isPlaying());
            onM20PlayClicked();
        } else if (m_currentPage == 3) {
            m_dr02PlayerBar->setPlaying(!m_dr02PlayerBar->isPlaying());
            onDR02PlayClicked();
        }
        break;

    case Qt::Key_Escape:
        if (m_currentPage == 1)
            onStopClicked();
        else if (m_currentPage == 2)
            onM20StopClicked();
        else if (m_currentPage == 3)
            onDR02StopClicked();
        break;

    case Qt::Key_Right: {
        int seekStep = event->modifiers() & Qt::ShiftModifier ? 100 : 20;
        if (m_currentPage == 1 && m_isPlaybackActive && m_totalFrames > 0) {
            int cur = static_cast<int>(qint64(m_playerBar->currentSliderValue()) * m_totalFrames / 1000);
            m_client->seek(qMin(cur + seekStep, m_totalFrames - 1));
        } else if (m_currentPage == 2 && m_m20IsPlaybackActive && m_m20TotalFrames > 0) {
            int cur = static_cast<int>(qint64(m_m20PlayerBar->currentSliderValue()) * m_m20TotalFrames / 1000);
            m_m20Client->seek(qMin(cur + seekStep, m_m20TotalFrames - 1));
        } else if (m_currentPage == 3 && m_dr02IsPlaybackActive && m_dr02TotalFrames > 0) {
            int cur = static_cast<int>(qint64(m_dr02PlayerBar->currentSliderValue()) * m_dr02TotalFrames / 1000);
            m_dr02Client->seek(qMin(cur + seekStep, m_dr02TotalFrames - 1));
        }
        break;
    }

    case Qt::Key_Left: {
        int seekStep = event->modifiers() & Qt::ShiftModifier ? 100 : 20;
        if (m_currentPage == 1 && m_isPlaybackActive && m_totalFrames > 0) {
            int cur = static_cast<int>(qint64(m_playerBar->currentSliderValue()) * m_totalFrames / 1000);
            m_client->seek(qMax(cur - seekStep, 0));
        } else if (m_currentPage == 2 && m_m20IsPlaybackActive && m_m20TotalFrames > 0) {
            int cur = static_cast<int>(qint64(m_m20PlayerBar->currentSliderValue()) * m_m20TotalFrames / 1000);
            m_m20Client->seek(qMax(cur - seekStep, 0));
        } else if (m_currentPage == 3 && m_dr02IsPlaybackActive && m_dr02TotalFrames > 0) {
            int cur = static_cast<int>(qint64(m_dr02PlayerBar->currentSliderValue()) * m_dr02TotalFrames / 1000);
            m_dr02Client->seek(qMax(cur - seekStep, 0));
        }
        break;
    }

    case Qt::Key_Period: {
        if (m_currentPage == 1 && m_totalFrames > 0) {
            int cur = static_cast<int>(qint64(m_playerBar->currentSliderValue()) * m_totalFrames / 1000);
            int next = qMin(cur + 1, m_totalFrames - 1);
            m_client->seek(next);
            m_playerBar->setCurrentPosition(next);
        } else if (m_currentPage == 2 && m_m20TotalFrames > 0) {
            int cur = static_cast<int>(qint64(m_m20PlayerBar->currentSliderValue()) * m_m20TotalFrames / 1000);
            int next = qMin(cur + 1, m_m20TotalFrames - 1);
            m_m20Client->seek(next);
            m_m20PlayerBar->setCurrentPosition(next);
        } else if (m_currentPage == 3 && m_dr02TotalFrames > 0) {
            int cur = static_cast<int>(qint64(m_dr02PlayerBar->currentSliderValue()) * m_dr02TotalFrames / 1000);
            int next = qMin(cur + 1, m_dr02TotalFrames - 1);
            m_dr02Client->seek(next);
            m_dr02PlayerBar->setCurrentPosition(next);
        }
        break;
    }

    case Qt::Key_Comma: {
        if (m_currentPage == 1 && m_totalFrames > 0) {
            int cur = static_cast<int>(qint64(m_playerBar->currentSliderValue()) * m_totalFrames / 1000);
            int prev = qMax(cur - 1, 0);
            m_client->seek(prev);
            m_playerBar->setCurrentPosition(prev);
        } else if (m_currentPage == 2 && m_m20TotalFrames > 0) {
            int cur = static_cast<int>(qint64(m_m20PlayerBar->currentSliderValue()) * m_m20TotalFrames / 1000);
            int prev = qMax(cur - 1, 0);
            m_m20Client->seek(prev);
            m_m20PlayerBar->setCurrentPosition(prev);
        } else if (m_currentPage == 3 && m_dr02TotalFrames > 0) {
            int cur = static_cast<int>(qint64(m_dr02PlayerBar->currentSliderValue()) * m_dr02TotalFrames / 1000);
            int prev = qMax(cur - 1, 0);
            m_dr02Client->seek(prev);
            m_dr02PlayerBar->setCurrentPosition(prev);
        }
        break;
    }

    case Qt::Key_BracketRight: {
        int nextIdx = -1;
        if (m_currentPage == 1) {
            nextIdx = qMin(m_playerBar->speedIndex() + 1, 3);
            m_playerBar->clickSpeed(nextIdx);
        } else if (m_currentPage == 2) {
            nextIdx = qMin(m_m20PlayerBar->speedIndex() + 1, 3);
            m_m20PlayerBar->clickSpeed(nextIdx);
        } else if (m_currentPage == 3) {
            nextIdx = qMin(m_dr02PlayerBar->speedIndex() + 1, 3);
            m_dr02PlayerBar->clickSpeed(nextIdx);
        }
        break;
    }

    case Qt::Key_BracketLeft: {
        int prevIdx = -1;
        if (m_currentPage == 1) {
            prevIdx = qMax(m_playerBar->speedIndex() - 1, 0);
            m_playerBar->clickSpeed(prevIdx);
        } else if (m_currentPage == 2) {
            prevIdx = qMax(m_m20PlayerBar->speedIndex() - 1, 0);
            m_m20PlayerBar->clickSpeed(prevIdx);
        } else if (m_currentPage == 3) {
            prevIdx = qMax(m_dr02PlayerBar->speedIndex() - 1, 0);
            m_dr02PlayerBar->clickSpeed(prevIdx);
        }
        break;
    }

    default:
        QMainWindow::keyPressEvent(event);
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    stopRecordingIfAny();
    if (m_client) {
        m_client->stop();
        m_client->stopServer();
    }
    if (m_m20Client) {
        m_m20Client->stop();
        m_m20Client->stopServer();
    }
    if (m_dr02Client) {
        m_dr02Client->stop();
        m_dr02Client->stopServer();
    }
    QMainWindow::closeEvent(event);
}
