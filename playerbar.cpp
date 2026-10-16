#include "playerbar.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QSizePolicy>

static const char *DARK_BG     = "#0a0e14";
static const char *PANEL_BG    = "#1a1f2e";
static const char *NEON_BLUE   = "#00d4ff";
static const char *NEON_CYAN   = "#00ffc8";
static const char *PROGRESS_BG = "#2a3040";
static const char *TEXT_COLOR  = "#e0e6ed";

// ---- 按钮样式辅助 ----
static QString glowButtonStyle()
{
    return QString(R"(
        QPushButton {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #1e3a5f,stop:1 #0d1f33);
            border: 1px solid %1;
            border-radius: 6px;
            color: %1;
            font-size: 13px;
            font-weight: bold;
            padding: 6px 16px;
            min-width: 60px;
        }
        QPushButton:hover {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #2a4a6f,stop:1 #1a3050);
            border: 1px solid %2;
            color: %2;
        }
        QPushButton:pressed {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #0d1f33,stop:1 #1e3a5f);
        }
    )").arg(NEON_BLUE, NEON_CYAN);
}

static QString speedActiveStyle()
{
    return QString(R"(
        QPushButton {
            background: %1;
            border: none;
            border-radius: 4px;
            color: %2;
            font-size: 11px;
            font-weight: bold;
        }
        QPushButton:hover {
            background: %3;
        }
    )").arg(NEON_BLUE, DARK_BG, NEON_CYAN);
}

static QString speedInactiveStyle()
{
    return QString(R"(
        QPushButton {
            background: %1;
            border: 1px solid #3a4050;
            border-radius: 4px;
            color: %2;
            font-size: 11px;
        }
        QPushButton:hover {
            border: 1px solid %3;
            color: %3;
        }
    )").arg(PROGRESS_BG, TEXT_COLOR, NEON_BLUE);
}

static QString progressStyle()
{
    return QString(R"(
        QSlider { height: 24px; }
        QSlider::groove:horizontal {
            background: %1;
            height: 6px;
            border-radius: 3px;
        }
        QSlider::sub-page:horizontal {
            background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 %2,stop:1 %3);
            height: 6px;
            border-radius: 3px;
        }
        QSlider::handle:horizontal {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #ffffff,stop:1 %3);
            width: 14px;
            height: 14px;
            margin: -5px 0;
            border-radius: 7px;
            border: 2px solid %2;
        }
        QSlider::handle:horizontal:hover {
            background: %3;
            border: 2px solid #ffffff;
        }
    )").arg(PROGRESS_BG, NEON_BLUE, NEON_CYAN);
}

// ---- PlayerBar ----
PlayerBar::PlayerBar(QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(80);
    setStyleSheet(QString("QWidget { background: %1; border-top: 1px solid #2a3545; }")
                  .arg(PANEL_BG));

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 8, 20, 8);
    mainLayout->setSpacing(6);

    // ---- 上层：进度条 + 时间 ----
    auto *progressLayout = new QHBoxLayout;
    progressLayout->setSpacing(12);

    m_timeCurrent = new QLabel("00:00.0");
    m_timeCurrent->setStyleSheet(
        QString("color: %1; font-size: 12px; font-family: 'Consolas', monospace; font-weight: bold;")
        .arg(NEON_CYAN));
    m_timeCurrent->setFixedWidth(62);

    m_progress = new QSlider(Qt::Horizontal);
    m_progress->setMinimum(0);
    m_progress->setMaximum(1000);
    m_progress->setValue(0);
    m_progress->setCursor(Qt::PointingHandCursor);
    m_progress->setStyleSheet(progressStyle());
    m_progress->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_timeTotal = new QLabel("00:00.0");
    m_timeTotal->setStyleSheet(
        QString("color: %1; font-size: 12px; font-family: 'Consolas', monospace;").arg(TEXT_COLOR));
    m_timeTotal->setFixedWidth(62);

    progressLayout->addWidget(m_timeCurrent);
    progressLayout->addWidget(m_progress);
    progressLayout->addWidget(m_timeTotal);

    // ---- 下层：控制按钮 ----
    auto *controlLayout = new QHBoxLayout;
    controlLayout->setSpacing(10);

    m_btnPlay = new QPushButton(QString::fromUtf8("\u25b6 播放"));
    m_btnPlay->setFixedSize(90, 36);
    m_btnPlay->setCursor(Qt::PointingHandCursor);
    m_btnPlay->setStyleSheet(glowButtonStyle());

    m_btnStop = new QPushButton(QString::fromUtf8("\u23f9 停止"));
    m_btnStop->setFixedSize(90, 36);
    m_btnStop->setCursor(Qt::PointingHandCursor);
    m_btnStop->setStyleSheet(glowButtonStyle());

    auto *sep1 = new QFrame;
    sep1->setFrameShape(QFrame::VLine);
    sep1->setFixedWidth(1);
    sep1->setFixedHeight(28);
    sep1->setStyleSheet("background: #4a5a6a;");

    auto *speedLabel = new QLabel("速度:");
    speedLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(TEXT_COLOR));

    auto *speedLayout = new QHBoxLayout;
    speedLayout->setSpacing(4);
    QStringList speeds = {"0.5x", "1x", "1.5x", "2x"};
    for (int i = 0; i < speeds.size(); ++i) {
        auto *btn = new QPushButton(speeds[i]);
        btn->setFixedSize(50, 28);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setCheckable(true);
        btn->setStyleSheet(i == 1 ? speedActiveStyle() : speedInactiveStyle());
        if (i == 1) btn->setChecked(true);
        connect(btn, &QPushButton::clicked, this, [this, i]() { onSpeedClicked(i); });
        m_speedButtons.append(btn);
        speedLayout->addWidget(btn);
    }

    auto *sep2 = new QFrame;
    sep2->setFrameShape(QFrame::VLine);
    sep2->setFixedWidth(1);
    sep2->setFixedHeight(28);
    sep2->setStyleSheet("background: #4a5a6a;");

    m_btnLoop = new QPushButton(QString::fromUtf8("\u21bb \u5faa\u73af"));
    m_btnLoop->setFixedHeight(36);
    m_btnLoop->setCursor(Qt::PointingHandCursor);
    m_btnLoop->setCheckable(true);
    m_btnLoop->setChecked(true);
    updateLoopStyle();

    m_abLabel = new QLabel(QString::fromUtf8("\u5e27: 0 / 0"));
    m_abLabel->setStyleSheet(
        QString("color: %1; font-size: 11px; padding: 4px 10px; background: %2; border-radius: 4px; font-family: 'Consolas', monospace;")
        .arg(NEON_BLUE, PROGRESS_BG));
    m_abLabel->setMinimumWidth(120);

    controlLayout->addWidget(m_btnPlay);
    controlLayout->addWidget(m_btnStop);
    controlLayout->addWidget(sep1);
    controlLayout->addWidget(speedLabel);
    controlLayout->addLayout(speedLayout);
    controlLayout->addWidget(sep2);
    controlLayout->addWidget(m_btnLoop);
    controlLayout->addStretch();
    controlLayout->addWidget(m_abLabel);

    mainLayout->addLayout(progressLayout);
    mainLayout->addLayout(controlLayout);

    // 连接信号
    connect(m_btnPlay, &QPushButton::clicked, this, &PlayerBar::onPlayClicked);
    connect(m_btnStop, &QPushButton::clicked, this, &PlayerBar::onStopClicked);
    connect(m_btnLoop, &QPushButton::clicked, this, &PlayerBar::onLoopClicked);
    connect(m_progress, &QSlider::sliderMoved, this, &PlayerBar::onProgressMoved);
    connect(m_progress, &QSlider::sliderPressed, this, &PlayerBar::onProgressPressed);
}

// ---- Slots ----
void PlayerBar::onPlayClicked()
{
    m_isPlaying = !m_isPlaying;
    updatePlayButton();
    emit playClicked();
}

void PlayerBar::onStopClicked()
{
    m_isPlaying = false;
    updatePlayButton();
    emit stopClicked();
}

void PlayerBar::onSpeedClicked(int idx)
{
    m_speedIdx = idx;
    for (int i = 0; i < m_speedButtons.size(); ++i) {
        m_speedButtons[i]->setChecked(i == idx);
        m_speedButtons[i]->setStyleSheet(i == idx ? speedActiveStyle() : speedInactiveStyle());
    }
    emit speedChanged(idx);
}

void PlayerBar::onLoopClicked()
{
    m_isLoop = m_btnLoop->isChecked();
    updateLoopStyle();
    emit loopToggled(m_isLoop);
}

void PlayerBar::onProgressMoved(int value)
{
    emit positionChanged(value);
    updateTimeDisplay(value);
}

void PlayerBar::onProgressPressed()
{
    emit positionChanged(m_progress->value());
}

// ---- UI更新 ----
void PlayerBar::updatePlayButton()
{
    m_btnPlay->setText(m_isPlaying
        ? QString::fromUtf8("\u23f8 暂停")
        : QString::fromUtf8("\u25b6 播放"));
}

void PlayerBar::updateLoopStyle()
{
    if (m_isLoop) {
        m_btnLoop->setStyleSheet(QString(R"(
            QPushButton {
                background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #1e5a3f,stop:1 #0d3320);
                border: 1px solid %1;
                border-radius: 6px;
                color: %1;
                font-size: 13px;
                font-weight: bold;
                padding: 6px 16px;
            }
        )").arg(NEON_CYAN));
    } else {
        m_btnLoop->setStyleSheet(QString(R"(
            QPushButton {
                background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #1e3a5f,stop:1 #0d1f33);
                border: 1px solid #3a4555;
                border-radius: 6px;
                color: %1;
                font-size: 13px;
                padding: 6px 16px;
            }
        )").arg(TEXT_COLOR));
    }
}

void PlayerBar::updateTimeDisplay(int sliderValue)
{
    if (m_totalFrames > 0) {
        // 用 64 位中间量：帧数超过约 210 万（200Hz 下约 3 小时录制）时 int 会溢出
        int frame = static_cast<int>(qint64(sliderValue) * m_totalFrames / 1000);
        double sec = frame * m_frameInterval;
        m_timeCurrent->setText(formatTime(sec));
    }
}

QString PlayerBar::formatTime(double seconds)
{
    int totalTenths = static_cast<int>(seconds * 10 + 0.5);
    int mins  = totalTenths / 600;
    int secs  = (totalTenths / 10) % 60;
    int tenth = totalTenths % 10;
    return QString("%1:%2.%3")
        .arg(mins, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0'))
        .arg(tenth);
}

// ---- 公共接口 ----
void PlayerBar::setPlaying(bool playing)
{
    m_isPlaying = playing;
    updatePlayButton();
}

void PlayerBar::setTotalFrames(int frames, double interval)
{
    m_totalFrames = frames;
    m_frameInterval = interval;
    double totalSec = frames * interval;
    m_timeTotal->setText(formatTime(totalSec));
    m_abLabel->setText(QString::fromUtf8("\u5e27: 0 / %1").arg(frames));
}

void PlayerBar::setCurrentPosition(int frame)
{
    if (m_totalFrames > 0) {
        int value = static_cast<int>(qint64(frame) * 1000 / m_totalFrames);
        m_progress->blockSignals(true);
        m_progress->setValue(value);
        m_progress->blockSignals(false);
        double sec = frame * m_frameInterval;
        m_timeCurrent->setText(formatTime(sec));
        m_abLabel->setText(QString::fromUtf8("\u5e27: %1 / %2").arg(frame).arg(m_totalFrames));
    }
}

void PlayerBar::clickSpeed(int idx)
{
    if (idx >= 0 && idx < m_speedButtons.size())
        onSpeedClicked(idx);
}

void PlayerBar::resetTimeline()
{
    m_totalFrames = 0;
    m_progress->blockSignals(true);
    m_progress->setValue(0);
    m_progress->blockSignals(false);
    m_timeCurrent->setText("00:00.0");
    m_timeTotal->setText("00:00.0");
    m_abLabel->setText(QString::fromUtf8("\u5e27: 0 / 0"));
    m_isPlaying = false;
    updatePlayButton();
}
