#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QJsonObject>

class PyBulletClient;
class RenderView;
class ControlPanel;
class PlayerBar;
class LogWidget;
class CurveViewer;
class CustomAnalyzer;
class LogManager;
class DefaultPage;
class M20JointPanel;
class X30JointPanel;
class DR02JointPanel;
class QPushButton;
class QStackedWidget;
class QButtonGroup;

class ScreenRecorder;
class QElapsedTimer;
class QTimer;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    // X30 slots
    void onPlayClicked();
    void onStopClicked();
    void onSpeedChanged(int idx);
    void onLoopToggled(bool loop);
    void onPositionChanged(int value);
    void onLoadCsv();
    void onLoadTerrain();
    void onTerrainEnabledChanged(bool enabled);

    void onLogReceived(const QString &level, const QString &message);
    void onStatusReceived(const QJsonObject &status);
    void onServerReady();
    void onServerStopped();
    void onClientError(const QString &error);
    void onWindowHandleReceived(quintptr hwnd);
    void onX30JointSliderChanged(int jointIndex, double value);
    void onScreenshot();
    void onCameraPreset(int presetIndex);
    void onRecordToggled();          // X30/M20 共用
    void onRecordTick();

    // M20 slots
    void onM20PlayClicked();
    void onM20StopClicked();
    void onM20SpeedChanged(int idx);
    void onM20LoopToggled(bool loop);
    void onM20PositionChanged(int value);
    void onM20LoadCsv();
    void onM20LoadTerrain();
    void onM20LogReceived(const QString &level, const QString &message);
    void onM20StatusReceived(const QJsonObject &status);
    void onM20ServerReady();
    void onM20ServerStopped();
    void onM20ClientError(const QString &error);
    void onM20WindowHandleReceived(quintptr hwnd);
    void onM20JointSliderChanged(int jointIndex, double value);
    void onM20Screenshot();
    void onM20CameraPreset(int presetIndex);

    // DR02 slots
    void onDR02PlayClicked();
    void onDR02StopClicked();
    void onDR02SpeedChanged(int idx);
    void onDR02LoopToggled(bool loop);
    void onDR02PositionChanged(int value);
    void onDR02LoadCsv();
    void onDR02LogReceived(const QString &level, const QString &message);
    void onDR02StatusReceived(const QJsonObject &status);
    void onDR02ServerReady();
    void onDR02ServerStopped();
    void onDR02ClientError(const QString &error);
    void onDR02WindowHandleReceived(quintptr hwnd);
    void onDR02JointSliderChanged(int jointIndex, double value);
    void onDR02Screenshot();
    void onDR02CameraPreset(int presetIndex);

private:
    void initUi();
    void connectSignals();
    /// 把「是否加载地形」的最终结论（勾选 + 文件存在）同步给 X30 服务端
    void syncX30Terrain();
    /// 当前页对应的渲染视图与控制面板（1=X30, 2=M20, 3=DR02）
    RenderView   *activeRenderView() const;
    ControlPanel *activeControlPanel() const;
    LogWidget    *logWidgetForPage(int page) const;
    void stopRecordingIfAny();
    void startX30Server();
    void startM20Server();
    void startDR02Server();
    void shutdownX30Resources();
    void shutdownM20Resources();
    void shutdownDR02Resources();
    QWidget *createNavBar();
    QWidget *createX30Page();
    QWidget *createM20Page();
    QWidget *createDR02Page();
    void switchToPage(int index);

    // 导航
    QStackedWidget *m_stackedWidget = nullptr;
    QButtonGroup   *m_navGroup = nullptr;
    QPushButton    *m_navHome  = nullptr;
    QPushButton    *m_navX30   = nullptr;
    QPushButton    *m_navM20   = nullptr;
    QPushButton    *m_navDR02  = nullptr;
    QPushButton    *m_navCurve = nullptr;
    QPushButton    *m_navWave  = nullptr;
    QPushButton    *m_navLog   = nullptr;
    int             m_currentPage = 0;

    // X30 3D复现组件
    PyBulletClient *m_client = nullptr;
    RenderView     *m_renderView = nullptr;
    ControlPanel   *m_controlPanel = nullptr;
    PlayerBar      *m_playerBar = nullptr;
    LogWidget      *m_logWidget = nullptr;
    X30JointPanel  *m_x30JointPanel = nullptr;
    bool            m_x30ServerStarted = false;

    // M20 3D展示组件（独立）
    PyBulletClient *m_m20Client = nullptr;
    RenderView     *m_m20RenderView = nullptr;
    ControlPanel   *m_m20ControlPanel = nullptr;
    PlayerBar      *m_m20PlayerBar = nullptr;
    LogWidget      *m_m20LogWidget = nullptr;
    M20JointPanel  *m_m20JointPanel = nullptr;
    bool            m_m20ServerStarted = false;

    // DR02 人形三维复现组件（独立）
    PyBulletClient *m_dr02Client = nullptr;
    RenderView     *m_dr02RenderView = nullptr;
    ControlPanel   *m_dr02ControlPanel = nullptr;
    PlayerBar      *m_dr02PlayerBar = nullptr;
    LogWidget      *m_dr02LogWidget = nullptr;
    DR02JointPanel *m_dr02JointPanel = nullptr;
    bool            m_dr02ServerStarted = false;

    // 数据分析组件（非模态独立浮动窗口）
    CurveViewer    *m_curveViewer = nullptr;

    // 波形分析页面（顶层导航页）
    CustomAnalyzer *m_customAnalyzer = nullptr;

    // 日志管理页面
    LogManager     *m_logManager = nullptr;

    // 三维画面录制（三个复现页面共用，同时只有一个页面在渲染）
    ScreenRecorder *m_recorder = nullptr;
    QTimer         *m_recordUiTimer = nullptr;
    QElapsedTimer  *m_recordElapsed = nullptr;
    int             m_recordPage = 0;   // 录制发起时所在页，切页要自动停止

    // X30 播放状态
    int    m_totalFrames = 0;
    double m_frameSpeed = 0.005;
    int    m_pendingStartFrame = 0;
    int    m_pendingEndFrame = 0;
    QString m_csvPath;
    QString m_terrainPath;
    bool   m_isPlaybackActive = false;
    bool   m_isPaused = false;

    // M20 播放状态
    int    m_m20TotalFrames = 0;
    double m_m20FrameInterval = 0.004;
    double m_m20FrameSpeed = 0.004;
    int    m_m20PendingStartFrame = 0;
    int    m_m20PendingEndFrame = 0;
    QString m_m20CsvPath;
    bool   m_m20IsPlaybackActive = false;
    bool   m_m20IsPaused = false;

    // DR02 播放状态
    int    m_dr02TotalFrames = 0;
    double m_dr02FrameSpeed = 0.005;
    int    m_dr02PendingStartFrame = 0;
    int    m_dr02PendingEndFrame = 0;
    QString m_dr02CsvPath;
    bool   m_dr02IsPlaybackActive = false;
    bool   m_dr02IsPaused = false;
};

#endif // MAINWINDOW_H
