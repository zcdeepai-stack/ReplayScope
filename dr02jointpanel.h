#ifndef DR02JOINTPANEL_H
#define DR02JOINTPANEL_H

#include <QWidget>
#include <QVector>

class QLabel;
class QLayout;
class QSlider;

class DR02JointPanel : public QWidget
{
    Q_OBJECT

public:
    explicit DR02JointPanel(QWidget *parent = nullptr);

    void updateJointPositions(const QVector<double> &positions);
    void setInteractive(bool enabled);

    struct JointSliderInfo {
        int pybulletIndex = -1;
        double lower = 0.0;
        double upper = 0.0;
        QString name;
        QSlider *slider = nullptr;
        QLabel *valueLabel = nullptr;
    };

signals:
    void jointValueChanged(int jointIndex, double value);

private:
    void createJointGroup(QLayout *parent, const QString &groupName,
                          const QVector<JointSliderInfo> &joints);
    void onSliderChanged(int internalIndex, int sliderValue);

    QVector<JointSliderInfo> m_joints;
    bool m_interactive = true;
    bool m_blockSignals = false;

    static constexpr int SLIDER_RESOLUTION = 1000;
};

#endif // DR02JOINTPANEL_H
