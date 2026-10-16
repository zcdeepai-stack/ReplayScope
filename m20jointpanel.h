#ifndef M20JOINTPANEL_H
#define M20JOINTPANEL_H

#include <QWidget>
#include <QSlider>
#include <QLabel>
#include <QGroupBox>
#include <QVector>

class M20JointPanel : public QWidget
{
    Q_OBJECT

public:
    explicit M20JointPanel(QWidget *parent = nullptr);

    // 播放时更新所有关节显示值（不触发信号）
    void updateJointPositions(const QVector<double> &positions);

    // 播放开始/停止时设置交互状态
    void setInteractive(bool enabled);

signals:
    // jointIndex: PyBullet关节索引(0-15), value: 弧度值
    void jointValueChanged(int jointIndex, double value);

private:
    struct JointSliderInfo {
        int pybulletIndex;   // PyBullet中的关节索引
        double lower;        // 下限(rad)
        double upper;        // 上限(rad)
        QString name;
        QSlider *slider;
        QLabel  *valueLabel;
    };

    void createLegGroup(QLayout *parent, const QString &legName,
                        const QVector<JointSliderInfo> &joints);
    void onSliderChanged(int internalIdx, int sliderValue);

    QVector<JointSliderInfo> m_joints;
    bool m_interactive = true;
    bool m_blockSignals = false;

    static constexpr int SLIDER_RESOLUTION = 1000;
};

#endif // M20JOINTPANEL_H
