#ifndef PLAYERBAR_H
#define PLAYERBAR_H

#include <QWidget>
#include <QPushButton>
#include <QSlider>
#include <QLabel>

class PlayerBar : public QWidget
{
    Q_OBJECT

public:
    explicit PlayerBar(QWidget *parent = nullptr);

    bool isPlaying() const { return m_isPlaying; }
    bool isLoop() const { return m_isLoop; }
    int speedIndex() const { return m_speedIdx; }
    int currentSliderValue() const { return m_progress->value(); }

    void setPlaying(bool playing);
    void setTotalFrames(int frames, double interval = 0.005);
    void setCurrentPosition(int frame);
    void clickSpeed(int idx);
    void resetTimeline();

signals:
    void playClicked();
    void stopClicked();
    void positionChanged(int sliderValue);   // 0-1000
    void speedChanged(int index);            // 0-3
    void loopToggled(bool enabled);

private slots:
    void onPlayClicked();
    void onStopClicked();
    void onSpeedClicked(int idx);
    void onLoopClicked();
    void onProgressMoved(int value);
    void onProgressPressed();

private:
    void updatePlayButton();
    void updateLoopStyle();
    void updateTimeDisplay(int sliderValue);
    QString formatTime(double seconds);

    QPushButton *m_btnPlay;
    QPushButton *m_btnStop;
    QPushButton *m_btnLoop;
    QSlider *m_progress;
    QLabel *m_timeCurrent;
    QLabel *m_timeTotal;
    QLabel *m_abLabel;
    QList<QPushButton*> m_speedButtons;

    bool m_isPlaying = false;
    bool m_isLoop = true;
    int m_speedIdx = 1;
    int m_totalFrames = 0;
    double m_frameInterval = 0.005;
};

#endif // PLAYERBAR_H
