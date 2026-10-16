#include "controlpanel.h"
#include <QVBoxLayout>
#include <QFrame>
#include <QSizePolicy>
#include <QScrollArea>

static const char *DARK_BG     = "#0a0e14";
static const char *PANEL_BG    = "#1a1f2e";
static const char *NEON_BLUE   = "#00d4ff";
static const char *NEON_CYAN   = "#00ffc8";
static const char *PROGRESS_BG = "#2a3040";
static const char *TEXT_COLOR  = "#e0e6ed";

ControlPanel::ControlPanel(QWidget *parent)
    : QWidget(parent)
{
    setMinimumWidth(280);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(PANEL_BG));
    setPalette(pal);

    setStyleSheet(QString(R"(
        QGroupBox {
            background-color: %1;
            border: 1px solid #2a3545;
            border-radius: 8px;
            margin-top: 16px;
            padding-top: 28px;
        }
        QGroupBox::title {
            color: %2;
            background: %1;
            subcontrol-origin: margin;
            subcontrol-position: top left;
            left: 12px;
            top: 4px;
            padding: 2px 10px;
            font-size: 13px;
            font-weight: bold;
            border-radius: 4px;
        }
        QLabel {
            color: %3;
            font-size: 12px;
        }
    )").arg(DARK_BG, NEON_BLUE, TEXT_COLOR));

    // 面板内容放进滚动区：按钮高度是写死的（40/40/40/32/32），三个分组叠起来最小高度
    // 约 600px，再加播放条会把整个主窗口的最小高度顶到 700px 左右。
    // 笔记本（如 1366x768，可用高度约 728）上就会压不下去、内容被切掉。
    // 有了滚动区，面板可以随意变矮，短屏上滚动查看即可。
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(
        "QScrollArea { background: transparent; border: none; }"
        "QScrollArea > QWidget > QWidget { background: transparent; }"
        "QScrollBar:vertical { width: 6px; background: transparent; }"
        "QScrollBar::handle:vertical { background: #3a4a5a; border-radius: 3px; min-height: 30px; }"
        "QScrollBar::handle:vertical:hover { background: #00d4ff; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }");

    auto *content = new QWidget;
    outerLayout->addWidget(scroll);

    auto *mainLayout = new QVBoxLayout(content);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(12);

    // ---- 文件选择组 ----
    auto *fileGroup = new QGroupBox("数据文件");
    auto *fileLayout = new QVBoxLayout;
    fileLayout->setSpacing(10);
    fileLayout->setContentsMargins(12, 15, 12, 12);

    m_jointLabel = new QLabel("未选择关节数据文件");
    m_jointLabel->setWordWrap(true);
    m_jointLabel->setStyleSheet(
        QString("color: %1; font-size: 11px; padding: 8px; background: %2; border-radius: 4px;")
        .arg(NEON_CYAN, PROGRESS_BG));
    fileLayout->addWidget(m_jointLabel);

    auto *btnJoint = createButton("选择关节 CSV 文件", NEON_BLUE);
    connect(btnJoint, &QPushButton::clicked, this, &ControlPanel::loadCsvClicked);
    fileLayout->addWidget(btnJoint);

    auto *line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("background: #2a3545;");
    m_terrainSeparator = line;
    fileLayout->addWidget(line);

    m_terrainLabel = new QLabel("未选择地形图文件");
    m_terrainLabel->setWordWrap(true);
    m_terrainLabel->setStyleSheet(
        QString("color: %1; font-size: 11px; padding: 8px; background: %2; border-radius: 4px;")
        .arg(NEON_CYAN, PROGRESS_BG));
    fileLayout->addWidget(m_terrainLabel);

    m_btnTerrain = createButton("选择地形图 CSV 文件", NEON_CYAN);
    connect(m_btnTerrain, &QPushButton::clicked, this, &ControlPanel::loadTerrainClicked);
    fileLayout->addWidget(m_btnTerrain);

    // 播放时是否加载地形。默认关闭：未选文件或文件不存在时一律不加载。
    m_terrainCheck = new QCheckBox("播放时加载地形图");
    m_terrainCheck->setChecked(false);
    m_terrainCheck->setCursor(Qt::PointingHandCursor);
    m_terrainCheck->setStyleSheet(QString(R"(
        QCheckBox { color: %1; font-size: 12px; padding: 2px 0; }
        QCheckBox::indicator { width: 14px; height: 14px;
            border: 1px solid #3a4555; border-radius: 3px; background: %2; }
        QCheckBox::indicator:hover { border-color: %1; }
        QCheckBox::indicator:checked { background: %1; border-color: %1; }
    )").arg(NEON_CYAN, PROGRESS_BG));
    connect(m_terrainCheck, &QCheckBox::toggled, this, &ControlPanel::terrainEnabledChanged);
    fileLayout->addWidget(m_terrainCheck);

    fileGroup->setLayout(fileLayout);
    mainLayout->addWidget(fileGroup);

    // ---- 信息显示组 ----
    auto *infoGroup = new QGroupBox("数据信息");
    auto *infoLayout = new QVBoxLayout;
    infoLayout->setSpacing(8);
    infoLayout->setContentsMargins(12, 15, 12, 12);

    m_infoFrames = new QLabel("总帧数: --");
    m_infoFrames->setStyleSheet(QString("color: %1; font-size: 12px;").arg(TEXT_COLOR));
    infoLayout->addWidget(m_infoFrames);

    m_infoDuration = new QLabel("总时长: --");
    m_infoDuration->setStyleSheet(QString("color: %1; font-size: 12px;").arg(TEXT_COLOR));
    infoLayout->addWidget(m_infoDuration);

    m_infoStatus = new QLabel("状态: 就绪");
    m_infoStatus->setStyleSheet(
        QString("color: %1; font-size: 12px; font-weight: bold;").arg(NEON_CYAN));
    infoLayout->addWidget(m_infoStatus);

    infoGroup->setLayout(infoLayout);
    mainLayout->addWidget(infoGroup);

    // ---- 工具组 ----
    auto *toolGroup = new QGroupBox("工具");
    auto *toolLayout = new QVBoxLayout;
    toolLayout->setSpacing(8);
    toolLayout->setContentsMargins(12, 15, 12, 12);

    auto *btnScreenshot = createButton("截图保存", "#ff9040");
    connect(btnScreenshot, &QPushButton::clicked, this, &ControlPanel::screenshotClicked);
    toolLayout->addWidget(btnScreenshot);

    m_btnRecord = createButton("● 开始录制", "#ff5060");
    connect(m_btnRecord, &QPushButton::clicked, this, &ControlPanel::recordToggled);
    toolLayout->addWidget(m_btnRecord);

    m_recordLabel = new QLabel;
    m_recordLabel->setAlignment(Qt::AlignCenter);
    m_recordLabel->setStyleSheet("color: #ff5060; font-size: 11px; font-family: 'Consolas';");
    m_recordLabel->setVisible(false);
    toolLayout->addWidget(m_recordLabel);

    // 视角预设按钮行
    auto *camRow1 = new QHBoxLayout;
    camRow1->setSpacing(6);
    auto *camRow2 = new QHBoxLayout;
    camRow2->setSpacing(6);

    auto makeCamBtn = [&](const QString &text, int idx) {
        auto *btn = new QPushButton(text);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(32);
        btn->setStyleSheet(R"(
            QPushButton {
                background: #0d1f33; border: 1px solid #1a4060; border-radius: 4px;
                color: #80c0e0; font-size: 11px; font-weight: bold; padding: 4px 8px;
            }
            QPushButton:hover { border-color: #00d4ff; color: #00d4ff; background: #152a40; }
            QPushButton:pressed { background: #0a1520; }
        )");
        connect(btn, &QPushButton::clicked, this, [this, idx]() {
            emit cameraPresetClicked(idx);
        });
        return btn;
    };

    camRow1->addWidget(makeCamBtn("默认", 0));
    camRow1->addWidget(makeCamBtn("前视", 1));
    camRow2->addWidget(makeCamBtn("侧视", 2));
    camRow2->addWidget(makeCamBtn("俯视", 3));
    toolLayout->addLayout(camRow1);
    toolLayout->addLayout(camRow2);

    auto *cameraHint = new QLabel("右键拖动平移，滚轮缩放");
    cameraHint->setAlignment(Qt::AlignCenter);
    cameraHint->setStyleSheet("color:#708090;font-size:10px;padding:2px;");
    toolLayout->addWidget(cameraHint);

    auto *opacityRow = new QHBoxLayout;
    opacityRow->setSpacing(8);
    auto *opacityLabel = new QLabel("模型透明度");
    opacityLabel->setStyleSheet("color:#e0e6ed;font-size:11px;");
    m_opacitySlider = new QSlider(Qt::Horizontal);
    m_opacitySlider->setRange(20, 100);
    m_opacitySlider->setValue(100);
    m_opacitySlider->setCursor(Qt::PointingHandCursor);
    m_opacitySlider->setStyleSheet(R"(
        QSlider::groove:horizontal { height:4px;background:#2a3545;border-radius:2px; }
        QSlider::sub-page:horizontal { background:#00d4ff;border-radius:2px; }
        QSlider::handle:horizontal { background:#00d4ff;width:12px;height:12px;
                                     margin:-4px 0;border-radius:6px; }
    )");
    m_opacityValue = new QLabel("100%");
    m_opacityValue->setFixedWidth(36);
    m_opacityValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_opacityValue->setStyleSheet("color:#00d4ff;font-size:10px;font-family:'Consolas';");
    connect(m_opacitySlider, &QSlider::valueChanged, this, [this](int value) {
        m_opacityValue->setText(QString::number(value) + "%");
        emit modelOpacityChanged(value / 100.0);
    });
    opacityRow->addWidget(opacityLabel);
    opacityRow->addWidget(m_opacitySlider, 1);
    opacityRow->addWidget(m_opacityValue);
    toolLayout->addLayout(opacityRow);

    toolGroup->setLayout(toolLayout);
    mainLayout->addWidget(toolGroup);

    mainLayout->addStretch(1);
    scroll->setWidget(content);
}

QPushButton *ControlPanel::createButton(const QString &text, const QString &color)
{
    auto *btn = new QPushButton(text);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedHeight(40);
    QColor c(color);
    QString lighter = c.lighter(120).name();
    btn->setStyleSheet(QString(R"(
        QPushButton {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #1e3a5f,stop:1 #0d1f33);
            border: 1px solid %1;
            border-radius: 6px;
            color: %1;
            font-size: 13px;
            font-weight: bold;
            padding: 8px 16px;
        }
        QPushButton:hover {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #2a4a6f,stop:1 #1a3050);
            border: 1px solid %2;
            color: %2;
        }
        QPushButton:pressed {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #0d1f33,stop:1 #1e3a5f);
        }
    )").arg(color, lighter));
    return btn;
}

void ControlPanel::updateInfo(int frames, double durationSec)
{
    m_infoFrames->setText(QString("总帧数: %1").arg(frames));
    int mins = static_cast<int>(durationSec) / 60;
    int secs = static_cast<int>(durationSec) % 60;
    m_infoDuration->setText(QString("总时长: %1:%2")
                            .arg(mins, 2, 10, QChar('0'))
                            .arg(secs, 2, 10, QChar('0')));
}

void ControlPanel::resetDataDisplay()
{
    m_infoFrames->setText("总帧数: --");
    m_infoDuration->setText("总时长: --");
    setJointLabel("未选择关节数据文件");
    setTerrainLabel("未选择地形图文件");
    setStatus(QStringLiteral("就绪"));
    setTerrainEnabled(false);
}

bool ControlPanel::isTerrainEnabled() const
{
    return m_terrainCheck->isChecked();
}

void ControlPanel::setTerrainEnabled(bool enabled)
{
    if (m_terrainCheck->isChecked() == enabled)
        return;
    m_terrainCheck->setChecked(enabled);   // 会经 toggled 发出 terrainEnabledChanged
}

void ControlPanel::setTerrainOptionVisible(bool visible)
{
    m_terrainSeparator->setVisible(visible);
    m_terrainLabel->setVisible(visible);
    m_btnTerrain->setVisible(visible);
    m_terrainCheck->setVisible(visible);
}

double ControlPanel::modelOpacity() const
{
    return m_opacitySlider ? m_opacitySlider->value() / 100.0 : 1.0;
}

void ControlPanel::setRecordingState(bool recording)
{
    m_btnRecord->setText(recording ? "■ 停止录制" : "● 开始录制");
    m_recordLabel->setVisible(recording);
    if (!recording)
        m_recordLabel->clear();
}

void ControlPanel::setRecordingElapsed(const QString &text)
{
    m_recordLabel->setText(text);
}

void ControlPanel::setStatus(const QString &status)
{
    QString color;
    if (status == "播放中")
        color = NEON_CYAN;
    else if (status == "就绪" || status == "播放完成")
        color = "#00d4ff";
    else if (status == "已暂停" || status == "已停止")
        color = "#ffc040";
    else
        color = "#ff6b6b";

    m_infoStatus->setText("状态: " + status);
    m_infoStatus->setStyleSheet(
        QString("color: %1; font-size: 12px; font-weight: bold;").arg(color));
}

void ControlPanel::setJointLabel(const QString &text)
{
    m_jointLabel->setText(text);
}

void ControlPanel::setTerrainLabel(const QString &text)
{
    m_terrainLabel->setText(text);
}
