#include "m20jointpanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QFrame>
#include <cmath>

static const char *DARK_BG     = "#0a0e14";
static const char *PANEL_BG    = "#1a1f2e";
static const char *NEON_BLUE   = "#00d4ff";
static const char *TEXT_COLOR  = "#e0e6ed";
static const char *DIM_TEXT    = "#708090";

// M20 URDF 关节定义 (4腿 × 4关节 = 16)
// PyBullet索引: FL(0-3), FR(4-7), HL(8-11), HR(12-15)
struct JointDef {
    int pybulletIdx;
    double lower, upper;
    const char *name;
};

static const JointDef FL_JOINTS[] = {
    { 0, -0.611,  0.436, "HipX"},
    { 1, -2.583,  2.286, "HipY"},
    { 2, -2.792,  2.809, "Knee"},
    { 3, -3.14159, 3.14159, "Wheel"},
};

static const JointDef FR_JOINTS[] = {
    { 4, -0.436,  0.611, "HipX"},
    { 5, -2.583,  2.286, "HipY"},
    { 6, -2.792,  2.809, "Knee"},
    { 7, -3.14159, 3.14159, "Wheel"},
};

static const JointDef HL_JOINTS[] = {
    { 8, -0.611,  0.436, "HipX"},
    { 9, -2.286,  2.583, "HipY"},
    {10, -2.809,  2.792, "Knee"},
    {11, -3.14159, 3.14159, "Wheel"},
};

static const JointDef HR_JOINTS[] = {
    {12, -0.436,  0.611, "HipX"},
    {13, -2.286,  2.583, "HipY"},
    {14, -2.809,  2.792, "Knee"},
    {15, -3.14159, 3.14159, "Wheel"},
};

static const char *SLIDER_STYLE = R"(
    QSlider::groove:horizontal {
        height: 4px;
        background: #2a3545;
        border-radius: 2px;
    }
    QSlider::handle:horizontal {
        background: #00d4ff;
        width: 12px;
        height: 12px;
        margin: -4px 0;
        border-radius: 6px;
    }
    QSlider::handle:horizontal:hover {
        background: #40e0ff;
    }
    QSlider::handle:horizontal:disabled {
        background: #4a5a6a;
    }
    QSlider::sub-page:horizontal {
        background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #0066aa,stop:1 #00d4ff);
        border-radius: 2px;
    }
)";

M20JointPanel::M20JointPanel(QWidget *parent)
    : QWidget(parent)
{
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(PANEL_BG));
    setPalette(pal);

    // 使用 ScrollArea 包裹，防止窗口小时无法显示全部
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    auto *titleLabel = new QLabel(QString::fromUtf8("M20 \u5173\u8282\u63a7\u5236"));
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet(
        QString("color: %1; font-size: 13px; font-weight: bold; padding: 6px; "
                "background: %2; border-bottom: 1px solid #2a3545;")
        .arg(NEON_BLUE, DARK_BG));
    outerLayout->addWidget(titleLabel);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(
        "QScrollArea { border: none; background: transparent; }"
        "QScrollBar:vertical { width: 6px; background: #0a0e14; }"
        "QScrollBar::handle:vertical { background: #3a4a5a; border-radius: 3px; min-height: 30px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }");

    auto *content = new QWidget;
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(8, 4, 8, 4);
    contentLayout->setSpacing(4);

    // 按腿创建 4 组，每组 4 个滑块
    struct LegInfo {
        const char *name;
        const JointDef *joints;
    };
    LegInfo legs[] = {
        {"FL (\u524d\u5de6)", FL_JOINTS},
        {"FR (\u524d\u53f3)", FR_JOINTS},
        {"HL (\u540e\u5de6)", HL_JOINTS},
        {"HR (\u540e\u53f3)", HR_JOINTS},
    };

    for (auto &leg : legs) {
        QVector<JointSliderInfo> infos;
        for (int j = 0; j < 4; ++j) {
            JointSliderInfo info;
            info.pybulletIndex = leg.joints[j].pybulletIdx;
            info.lower = leg.joints[j].lower;
            info.upper = leg.joints[j].upper;
            info.name = QString::fromUtf8(leg.joints[j].name);
            info.slider = nullptr;
            info.valueLabel = nullptr;
            infos.append(info);
        }
        createLegGroup(contentLayout, QString::fromUtf8(leg.name), infos);
    }

    contentLayout->addStretch(1);
    scroll->setWidget(content);
    outerLayout->addWidget(scroll, 1);
}

void M20JointPanel::createLegGroup(QLayout *parent, const QString &legName,
                                    const QVector<JointSliderInfo> &joints)
{
    auto *group = new QGroupBox(legName);
    group->setStyleSheet(QString(R"(
        QGroupBox {
            background-color: %1;
            border: 1px solid #2a3545;
            border-radius: 6px;
            margin-top: 12px;
            padding-top: 22px;
            padding-bottom: 4px;
        }
        QGroupBox::title {
            color: %2;
            background: %1;
            subcontrol-origin: margin;
            subcontrol-position: top left;
            left: 8px;
            top: 2px;
            padding: 1px 6px;
            font-size: 11px;
            font-weight: bold;
            border-radius: 3px;
        }
    )").arg(DARK_BG, NEON_BLUE));

    auto *gridLayout = new QGridLayout;
    gridLayout->setContentsMargins(6, 4, 6, 4);
    gridLayout->setHorizontalSpacing(6);
    gridLayout->setVerticalSpacing(2);

    for (int j = 0; j < joints.size(); ++j) {
        int idx = m_joints.size(); // 当前滑块内部索引

        JointSliderInfo info = joints[j];

        // 关节名称标签
        auto *nameLabel = new QLabel(info.name);
        nameLabel->setStyleSheet(
            QString("color: %1; font-size: 10px; font-weight: bold;").arg(TEXT_COLOR));
        nameLabel->setFixedWidth(36);

        // 滑块
        auto *slider = new QSlider(Qt::Horizontal);
        slider->setMinimum(0);
        slider->setMaximum(SLIDER_RESOLUTION);
        slider->setValue(SLIDER_RESOLUTION / 2); // 中间位置（约0 rad）
        slider->setCursor(Qt::PointingHandCursor);
        slider->setStyleSheet(SLIDER_STYLE);
        slider->setMinimumWidth(80);

        // 设置初始位置为0 rad
        double range = info.upper - info.lower;
        int initVal = 0;
        if (range > 0)
            initVal = static_cast<int>((0.0 - info.lower) / range * SLIDER_RESOLUTION);
        initVal = qBound(0, initVal, SLIDER_RESOLUTION);
        slider->setValue(initVal);

        // 数值标签
        auto *valLabel = new QLabel("0.000");
        valLabel->setStyleSheet(
            QString("color: %1; font-size: 9px; font-family: 'Consolas', monospace;").arg(DIM_TEXT));
        valLabel->setFixedWidth(44);
        valLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        info.slider = slider;
        info.valueLabel = valLabel;
        m_joints.append(info);

        gridLayout->addWidget(nameLabel,  j, 0);
        gridLayout->addWidget(slider,     j, 1);
        gridLayout->addWidget(valLabel,   j, 2);

        connect(slider, &QSlider::valueChanged, this, [this, idx](int val) {
            onSliderChanged(idx, val);
        });
    }

    gridLayout->setColumnStretch(1, 1);
    group->setLayout(gridLayout);
    parent->addWidget(group);
}

void M20JointPanel::onSliderChanged(int internalIdx, int sliderValue)
{
    if (internalIdx < 0 || internalIdx >= m_joints.size()) return;
    if (m_blockSignals) return;

    auto &info = m_joints[internalIdx];
    double range = info.upper - info.lower;
    double value = info.lower + (static_cast<double>(sliderValue) / SLIDER_RESOLUTION) * range;
    value = qBound(info.lower, value, info.upper);

    info.valueLabel->setText(QString::number(value, 'f', 3));

    if (m_interactive) {
        emit jointValueChanged(info.pybulletIndex, value);
    }
}

void M20JointPanel::updateJointPositions(const QVector<double> &positions)
{
    // 新实测格式为16关节，与面板及URDF一一对应；同时兼容旧的12关节格式。
    const bool fullFormat = positions.size() >= 16;
    m_blockSignals = true;
    for (int i = 0; i < positions.size(); ++i) {
        const int panelIdx = fullFormat ? i : (i / 3) * 4 + (i % 3);
        if (panelIdx >= m_joints.size()) break;

        auto &info = m_joints[panelIdx];
        double range = info.upper - info.lower;
        if (range <= 0) continue;

        double rawValue = positions[i];
        // 轮关节可连续旋转；面板显示等效的[-pi, pi]角度，模型仍使用原始记录值。
        if (panelIdx % 4 == 3)
            rawValue = std::remainder(rawValue, 2.0 * 3.14159265358979323846);
        double val = qBound(info.lower, rawValue, info.upper);
        int sliderVal = static_cast<int>((val - info.lower) / range * SLIDER_RESOLUTION);
        sliderVal = qBound(0, sliderVal, SLIDER_RESOLUTION);

        info.slider->setValue(sliderVal);
        info.valueLabel->setText(QString::number(val, 'f', 3));
    }
    m_blockSignals = false;
}

void M20JointPanel::setInteractive(bool enabled)
{
    m_interactive = enabled;
    for (auto &info : m_joints) {
        info.slider->setEnabled(enabled);
    }
}
