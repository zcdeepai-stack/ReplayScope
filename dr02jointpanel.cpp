#include "dr02jointpanel.h"

#include <QColor>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPalette>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>

namespace {

struct JointDef {
    int index;
    double lower;
    double upper;
    const char *label;
};

const JointDef TORSO[] = {
    {0, -1.7453, 3.6652, "转腰"},
    {1, -0.6981, 0.6981, "侧弯"},
    {2, -0.5236, 1.5708, "俯仰"},
};
const JointDef HEAD[] = {
    {19, -1.745329, 3.665191, "转头"},
    {20, -0.349066, 0.349066, "抬头"},
};
const JointDef LEFT_ARM[] = {
    {3, -3.8397, 1.5708, "肩前后"},
    {4, -0.349066, 3.49066, "肩侧抬"},
    {5, -2.9671, 2.9671, "上臂旋转"},
    {6, -0.767945, 1.74533, "肘部"},
    {7, -2.9671, 2.9671, "腕旋转"},
    {8, -1.5708, 1.5708, "腕俯仰"},
    {9, -1.5708, 1.5708, "腕侧摆"},
};
const JointDef RIGHT_ARM[] = {
    {11, -3.8397, 1.5708, "肩前后"},
    {12, -3.49066, 0.349066, "肩侧抬"},
    {13, -2.9671, 2.9671, "上臂旋转"},
    {14, -0.767945, 1.74533, "肘部"},
    {15, -2.9671, 2.9671, "腕旋转"},
    {16, -1.5708, 1.5708, "腕俯仰"},
    {17, -1.5708, 1.5708, "腕侧摆"},
};
const JointDef LEFT_LEG[] = {
    {27, -2.7925, 1.5708, "髋前后"},
    {28, -0.349066, 2.618, "髋侧摆"},
    {29, -0.5236, 3.6652, "大腿旋转"},
    {30, -0.17453, 2.4958, "膝部"},
    {31, -1.0472, 0.7854, "踝俯仰"},
    {32, -0.61087, 0.61087, "踝侧摆"},
};
const JointDef RIGHT_LEG[] = {
    {33, -2.7925, 1.5708, "髋前后"},
    {34, -2.618, 0.349066, "髋侧摆"},
    {35, -3.6652, 0.5236, "大腿旋转"},
    {36, -0.17453, 2.4958, "膝部"},
    {37, -1.0472, 0.7854, "踝俯仰"},
    {38, -0.61087, 0.61087, "踝侧摆"},
};

const char *SLIDER_STYLE = R"(
    QSlider::groove:horizontal { height: 4px; background: #2a3545; border-radius: 2px; }
    QSlider::handle:horizontal { background: #00d4ff; width: 12px; height: 12px;
                                 margin: -4px 0; border-radius: 6px; }
    QSlider::handle:horizontal:hover { background: #40e0ff; }
    QSlider::handle:horizontal:disabled { background: #4a5a6a; }
    QSlider::sub-page:horizontal { background: #0088cc; border-radius: 2px; }
)";

template <size_t N>
QVector<DR02JointPanel::JointSliderInfo> makeJoints(const JointDef (&defs)[N])
{
    QVector<DR02JointPanel::JointSliderInfo> result;
    result.reserve(static_cast<int>(N));
    for (const auto &def : defs) {
        DR02JointPanel::JointSliderInfo info;
        info.pybulletIndex = def.index;
        info.lower = def.lower;
        info.upper = def.upper;
        info.name = QString::fromUtf8(def.label);
        result.append(info);
    }
    return result;
}

} // namespace

DR02JointPanel::DR02JointPanel(QWidget *parent)
    : QWidget(parent)
{
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor("#1a1f2e"));
    setPalette(pal);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto *title = new QLabel("DR02 人形关节控制");
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("color:#00d4ff;font-size:13px;font-weight:bold;padding:6px;"
                         "background:#0a0e14;border-bottom:1px solid #2a3545;");
    outer->addWidget(title);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(
        "QScrollArea{border:none;background:transparent;}"
        "QScrollBar:vertical{width:6px;background:#0a0e14;}"
        "QScrollBar::handle:vertical{background:#3a4a5a;border-radius:3px;min-height:30px;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0px;}");

    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(4);

    createJointGroup(layout, "躯干", makeJoints(TORSO));
    createJointGroup(layout, "左臂", makeJoints(LEFT_ARM));
    createJointGroup(layout, "右臂", makeJoints(RIGHT_ARM));
    createJointGroup(layout, "头部", makeJoints(HEAD));
    createJointGroup(layout, "左腿", makeJoints(LEFT_LEG));
    createJointGroup(layout, "右腿", makeJoints(RIGHT_LEG));
    layout->addStretch(1);

    scroll->setWidget(content);
    outer->addWidget(scroll, 1);
}

void DR02JointPanel::createJointGroup(QLayout *parent, const QString &groupName,
                                      const QVector<JointSliderInfo> &joints)
{
    auto *group = new QGroupBox(groupName);
    group->setStyleSheet(R"(
        QGroupBox { background:#0a0e14; border:1px solid #2a3545; border-radius:6px;
                    margin-top:12px; padding-top:22px; padding-bottom:4px; }
        QGroupBox::title { color:#00d4ff; background:#0a0e14; subcontrol-origin:margin;
                          subcontrol-position:top left; left:8px; top:2px; padding:1px 6px;
                          font-size:11px; font-weight:bold; border-radius:3px; }
    )");

    auto *grid = new QGridLayout;
    grid->setContentsMargins(6, 4, 6, 4);
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(2);

    for (int row = 0; row < joints.size(); ++row) {
        const int internalIndex = m_joints.size();
        JointSliderInfo info = joints[row];

        auto *name = new QLabel(info.name);
        name->setFixedWidth(52);
        name->setStyleSheet("color:#e0e6ed;font-size:10px;font-weight:bold;");

        auto *slider = new QSlider(Qt::Horizontal);
        slider->setRange(0, SLIDER_RESOLUTION);
        slider->setMinimumWidth(72);
        slider->setCursor(Qt::PointingHandCursor);
        slider->setStyleSheet(SLIDER_STYLE);
        const double range = info.upper - info.lower;
        slider->setValue(range > 0.0
                         ? qBound(0, static_cast<int>((-info.lower / range) * SLIDER_RESOLUTION),
                                  SLIDER_RESOLUTION)
                         : 0);

        auto *value = new QLabel("0.000");
        value->setFixedWidth(44);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        value->setStyleSheet("color:#708090;font-size:9px;font-family:'Consolas';");

        info.slider = slider;
        info.valueLabel = value;
        m_joints.append(info);

        grid->addWidget(name, row, 0);
        grid->addWidget(slider, row, 1);
        grid->addWidget(value, row, 2);
        connect(slider, &QSlider::valueChanged, this,
                [this, internalIndex](int v) { onSliderChanged(internalIndex, v); });
    }
    grid->setColumnStretch(1, 1);
    group->setLayout(grid);
    parent->addWidget(group);
}

void DR02JointPanel::onSliderChanged(int internalIndex, int sliderValue)
{
    if (m_blockSignals || internalIndex < 0 || internalIndex >= m_joints.size())
        return;

    auto &info = m_joints[internalIndex];
    const double value = info.lower
        + (static_cast<double>(sliderValue) / SLIDER_RESOLUTION) * (info.upper - info.lower);
    info.valueLabel->setText(QString::number(value, 'f', 3));
    if (m_interactive)
        emit jointValueChanged(info.pybulletIndex, value);
}

void DR02JointPanel::updateJointPositions(const QVector<double> &positions)
{
    m_blockSignals = true;
    const int count = qMin(positions.size(), m_joints.size());
    for (int i = 0; i < count; ++i) {
        auto &info = m_joints[i];
        const double range = info.upper - info.lower;
        if (range <= 0.0)
            continue;
        const double value = qBound(info.lower, positions[i], info.upper);
        const int sliderValue = qBound(
            0, static_cast<int>(((value - info.lower) / range) * SLIDER_RESOLUTION),
            SLIDER_RESOLUTION);
        info.slider->setValue(sliderValue);
        info.valueLabel->setText(QString::number(value, 'f', 3));
    }
    m_blockSignals = false;
}

void DR02JointPanel::setInteractive(bool enabled)
{
    m_interactive = enabled;
    for (auto &joint : m_joints)
        joint.slider->setEnabled(enabled);
}
