#ifndef CONTROLPANEL_H
#define CONTROLPANEL_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QCheckBox>
#include <QSlider>

class ControlPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ControlPanel(QWidget *parent = nullptr);

    void updateInfo(int frames, double durationSec);
    void resetDataDisplay();
    void setStatus(const QString &status);
    void setJointLabel(const QString &text);
    void setTerrainLabel(const QString &text);

    /// 播放时是否加载地形图（默认关闭）
    bool isTerrainEnabled() const;
    void setTerrainEnabled(bool enabled);
    /// 仅在支持地形的页面（X30）显示该开关
    void setTerrainOptionVisible(bool visible);
    double modelOpacity() const;

    /// 录制按钮外观（录制中变红并显示已录时长）
    void setRecordingState(bool recording);
    void setRecordingElapsed(const QString &text);

signals:
    void loadCsvClicked();
    void loadTerrainClicked();
    void terrainEnabledChanged(bool enabled);
    void screenshotClicked();
    void recordToggled();          ///< 点击录制按钮（开始/停止由 MainWindow 判断）
    void cameraPresetClicked(int presetIndex);  // 0=默认 1=前视 2=侧视 3=俯视
    void modelOpacityChanged(double opacity);

private:
    QPushButton *createButton(const QString &text, const QString &color);

    QLabel *m_jointLabel;
    QLabel *m_terrainLabel;
    QCheckBox *m_terrainCheck;
    QPushButton *m_btnTerrain;
    QWidget *m_terrainSeparator;
    QPushButton *m_btnRecord;
    QLabel *m_recordLabel;
    QSlider *m_opacitySlider;
    QLabel *m_opacityValue;
    QLabel *m_infoFrames;
    QLabel *m_infoDuration;
    QLabel *m_infoStatus;
};

#endif // CONTROLPANEL_H
