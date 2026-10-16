#include "x30jointpanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QFrame>

static const char *DARK_BG     = "#0a0e14";
static const char *PANEL_BG    = "#1a1f2e";
static const char *NEON_BLUE   = "#00d4ff";
static const char *TEXT_COLOR  = "#e0e6ed";
static const char *DIM_TEXT    = "#708090";

// X30 URDF 关节定义 (4腿 × 3活动关节 = 12, 跳过固定的Ankle)
// PyBullet索引: FL(0,1,2), FR(4,5,6), HL(8,9,10), HR(12,13,14)
// 站立姿态初始值来自 pybullet_server.py init_pos
struct X30JointDef {
    int pybulletIdx;
    double lower, upper;
    double initValue;  // 默认站立姿态
    const char *name;
};

static const X30JointDef FL_JOINTS[] = {
    { 0, -0.323,  0.585, -0.367596, "HipX"},
    { 1, -2.967,  0.262, -1.487190, "HipY"},
    { 2,  0.349,  2.531,  2.720850, "Knee"},
};

static const X30JointDef FR_JOINTS[] = {
    { 4, -0.585,  0.323,  0.267604, "HipX"},
    { 5, -2.967,  0.262, -1.494700, "HipY"},
    { 6,  0.349,  2.531,  2.729020, "Knee"},
};

static const X30JointDef HL_JOINTS[] = {
    { 8, -0.323,  0.585, -0.314182, "HipX"},
    { 9, -2.967,  0.262, -1.497910, "HipY"},
    {10,  0.349,  2.531,  2.756250, "Knee"},
};

static const X30JointDef HR_JOINTS[] = {
    {12, -0.585,  0.323,  0.260833, "HipX"},
    {13, -2.967,  0.262, -1.497930, "HipY"},
    {14,  0.349,  2.531,  2.725760, "Knee"},
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

X30JointPanel::X30JointPanel(QWidget *parent)
    : QWidget(parent)
{
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(PANEL_BG));
    setPalette(pal);

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    auto *titleLabel = new QLabel(QString::fromUtf8("X30 \u5173\u8282\u63a7\u5236"));
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

    struct LegInfo {
        const char *name;
        const X30JointDef *joints;
        int count;
    };
    LegInfo legs[] = {
        {"FL (\u524d\u5de6)", FL_JOINTS, 3},
        {"FR (\u524d\u53f3)", FR_JOINTS, 3},
        {"HL (\u540e\u5de6)", HL_JOINTS, 3},
        {"HR (\u540e\u53f3)", HR_JOINTS, 3},
    };

    for (auto &leg : legs) {
        QVector<JointSliderInfo> infos;
        for (int j = 0; j < leg.count; ++j) {
            JointSliderInfo info;
            info.pybulletIndex = leg.joints[j].pybulletIdx;
            info.lower = leg.joints[j].lower;
            info.upper = leg.joints[j].upper;
            info.initValue = leg.joints[j].initValue;
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

void X30JointPanel::createLegGroup(QLayout *parent, const QString &legName,
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
        int idx = m_joints.size();

        JointSliderInfo info = joints[j];

        auto *nameLabel = new QLabel(info.name);
        nameLabel->setStyleSheet(
            QString("color: %1; font-size: 10px; font-weight: bold;").arg(TEXT_COLOR));
        nameLabel->setFixedWidth(36);

        auto *slider = new QSlider(Qt::Horizontal);
        slider->setMinimum(0);
        slider->setMaximum(SLIDER_RESOLUTION);
        slider->setCursor(Qt::PointingHandCursor);
        slider->setStyleSheet(SLIDER_STYLE);
        slider->setMinimumWidth(80);

        // 设置初始位置为站立姿态值
        double range = info.upper - info.lower;
        int initVal = 0;
        if (range > 0)
            initVal = static_cast<int>((info.initValue - info.lower) / range * SLIDER_RESOLUTION);
        initVal = qBound(0, initVal, SLIDER_RESOLUTION);
        slider->setValue(initVal);

        auto *valLabel = new QLabel(QString::number(info.initValue, 'f', 3));
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

void X30JointPanel::onSliderChanged(int internalIdx, int sliderValue)
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

void X30JointPanel::updateJointPositions(const QVector<double> &positions)
{
    m_blockSignals = true;
    for (int i = 0; i < m_joints.size() && i < positions.size(); ++i) {
        auto &info = m_joints[i];
        double range = info.upper - info.lower;
        if (range <= 0) continue;

        double val = qBound(info.lower, positions[i], info.upper);
        int sliderVal = static_cast<int>((val - info.lower) / range * SLIDER_RESOLUTION);
        sliderVal = qBound(0, sliderVal, SLIDER_RESOLUTION);

        info.slider->setValue(sliderVal);
        info.valueLabel->setText(QString::number(val, 'f', 3));
    }
    m_blockSignals = false;
}

void X30JointPanel::setInteractive(bool enabled)
{
    m_interactive = enabled;
    for (auto &info : m_joints) {
        info.slider->setEnabled(enabled);
    }
}
