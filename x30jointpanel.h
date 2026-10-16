#ifndef X30JOINTPANEL_H
#define X30JOINTPANEL_H

#include <QWidget>
#include <QSlider>
#include <QLabel>
#include <QGroupBox>
#include <QVector>

class X30JointPanel : public QWidget
{
    Q_OBJECT

public:
    explicit X30JointPanel(QWidget *parent = nullptr);

    // 播放时更新所有关节显示值（不触发信号）
    void updateJointPositions(const QVector<double> &positions);

    // 播放开始/停止时设置交互状态
    void setInteractive(bool enabled);

signals:
    // jointIndex: PyBullet关节索引(0,1,2,4,5,6,8,9,10,12,13,14), value: 弧度值
    void jointValueChanged(int jointIndex, double value);

private:
    struct JointSliderInfo {
        int pybulletIndex;   // PyBullet中的关节索引
        double lower;        // 下限(rad)
        double upper;        // 上限(rad)
        double initValue;    // 初始站立姿态值(rad)
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

#endif // X30JOINTPANEL_H
