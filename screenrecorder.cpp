#include "screenrecorder.h"

#include <QBuffer>
#include <QPixmap>
#include <QFileInfo>

// ==================== RecordThread ====================

RecordThread::RecordThread(const QString &path, int width, int height,
                           int fps, int jpegQuality, QObject *parent)
    : QThread(parent)
    , m_path(path), m_width(width), m_height(height)
    , m_fps(fps), m_quality(jpegQuality), m_target(width, height)
{
}

void RecordThread::push(const QImage &frame)
{
    QMutexLocker lock(&m_mutex);
    if (m_finishRequested)
        return;
    if (m_queue.size() >= kMaxQueued) {
        // 编码跟不上采集时宁可丢帧，也不能让内存无限增长
        ++m_dropped;
        return;
    }
    m_queue.enqueue(frame);
    m_cond.wakeOne();
}

void RecordThread::requestFinish(int effectiveFps)
{
    QMutexLocker lock(&m_mutex);
    m_effectiveFps = effectiveFps;
    m_finishRequested = true;
    m_cond.wakeAll();
}

int RecordThread::droppedFrames() const
{
    QMutexLocker lock(&m_mutex);
    return m_dropped;
}

void RecordThread::run()
{
    AviMjpegWriter writer;
    if (!writer.open(m_path, m_width, m_height, m_fps)) {
        emit failed(writer.lastError());
        return;
    }

    QByteArray jpeg;
    jpeg.reserve(512 * 1024);

    for (;;) {
        QImage frame;
        {
            QMutexLocker lock(&m_mutex);
            while (m_queue.isEmpty() && !m_finishRequested)
                m_cond.wait(&m_mutex);
            if (m_queue.isEmpty() && m_finishRequested)
                break;                      // 队列排空且已请求结束
            frame = m_queue.dequeue();
        }

        // 缩放与像素格式转换都放在这里做，GUI 线程只负责抓原始帧
        if (frame.size() != m_target)
            frame = frame.scaled(m_target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        if (frame.format() != QImage::Format_RGB888)
            frame = frame.convertToFormat(QImage::Format_RGB888);

        jpeg.clear();
        QBuffer buf(&jpeg);
        buf.open(QIODevice::WriteOnly);
        if (!frame.save(&buf, "JPEG", m_quality)) {
            buf.close();
            writer.close();
            emit failed(QStringLiteral("JPEG 编码失败（缺少 imageformats/qjpeg 插件？）"));
            return;
        }
        buf.close();

        if (!writer.writeFrame(jpeg)) {
            const QString err = writer.lastError();
            writer.close();
            emit failed(err);
            return;
        }
    }

    // 用实际达成的帧率回填头部，避免丢帧后视频比真实过程放得更快
    int eff;
    { QMutexLocker lock(&m_mutex); eff = m_effectiveFps; }
    if (eff > 0)
        writer.setEffectiveFps(eff);

    const int    frames = writer.frameCount();
    const qint64 bytes  = writer.bytesWritten();
    writer.close();
    emit done(m_path, frames, bytes);
}


// ==================== ScreenRecorder ====================

ScreenRecorder::ScreenRecorder(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &ScreenRecorder::onTick);
}

ScreenRecorder::~ScreenRecorder()
{
    if (m_thread) {
        m_timer.stop();
        m_thread->requestFinish(0);
        m_thread->wait(5000);
        delete m_thread;
        m_thread = nullptr;
    }
}

bool ScreenRecorder::start(const QString &filePath, FrameSource source, int fps)
{
    if (m_thread) {
        emit errorOccurred(QStringLiteral("已经在录制中"));
        return false;
    }
    if (!source) {
        emit errorOccurred(QStringLiteral("没有可录制的画面来源"));
        return false;
    }

    // 先抓一帧确定尺寸，同时验证渲染窗口确实可用
    const QPixmap probe = source();
    if (probe.isNull() || probe.width() < 8 || probe.height() < 8) {
        emit errorOccurred(QStringLiteral("3D 渲染画面不可用，请先进入三维页面并等待模型加载完成"));
        return false;
    }

    m_fps            = qBound(1, fps, 60);
    m_baseIntervalMs = qMax<qint64>(1, 1000 / m_fps);
    m_intervalMs     = m_baseIntervalMs;
    m_frameSize      = QSize(probe.width() & ~1, probe.height() & ~1);
    m_source         = std::move(source);
    m_stopping       = false;
    m_captured       = 0;

    m_thread = new RecordThread(filePath, m_frameSize.width(), m_frameSize.height(),
                                m_fps, 85);
    connect(m_thread, &RecordThread::done,   this, &ScreenRecorder::onThreadDone);
    connect(m_thread, &RecordThread::failed, this, &ScreenRecorder::onThreadFailed);
    m_thread->start();

    m_clock.start();
    m_nextDueMs = 0;
    scheduleNext();
    emit started(filePath, m_frameSize, m_fps);
    return true;
}

void ScreenRecorder::stop()
{
    if (!m_thread || m_stopping)
        return;
    m_stopping = true;
    m_timer.stop();

    // 实际帧率 = 抓到的帧数 / 真实经过的秒数
    const double secs = qMax<qint64>(1, m_clock.elapsed()) / 1000.0;
    const int eff = qBound(1, int(qRound(m_captured / secs)), 60);
    m_thread->requestFinish(m_captured > 1 ? eff : m_fps);
}

void ScreenRecorder::scheduleNext()
{
    if (!m_thread || m_stopping)
        return;
    const qint64 now = m_clock.elapsed();
    if (m_nextDueMs <= now)
        m_nextDueMs = now + m_intervalMs;    // 落后了就从当前时刻重新排，不累积欠账
    m_timer.start(static_cast<int>(qMax<qint64>(1, m_nextDueMs - now)));
}

void ScreenRecorder::onTick()
{
    if (!m_thread || m_stopping || !m_source || m_inTick)
        return;
    m_inTick = true;

    QElapsedTimer t;
    t.start();

    const QPixmap px = m_source();
    if (!px.isNull()) {
        // 只做 toImage（浅层转换），缩放与像素格式转换交给后台线程
        m_thread->push(px.toImage());
        ++m_captured;
    }

    const qint64 cost = t.elapsed();

    // 自适应限流：抓一帧要 cost 毫秒，就把间隔拉到 cost/25% 以上，
    // 保证 GUI 线程至少有 75% 的时间处理按钮、拖动和重绘。
    const qint64 needed = cost * 100 / kMaxGuiLoadPercent;
    m_intervalMs = qMax(m_baseIntervalMs, needed);
    m_nextDueMs += m_intervalMs;

    m_inTick = false;
    scheduleNext();
}

void ScreenRecorder::onThreadDone(const QString &path, int frames, qint64 bytes)
{
    const int dropped = m_thread ? m_thread->droppedFrames() : 0;
    teardown();
    emit finished(path, frames, bytes, dropped);
}

void ScreenRecorder::onThreadFailed(const QString &message)
{
    teardown();
    emit errorOccurred(message);
}

void ScreenRecorder::teardown()
{
    m_timer.stop();
    m_stopping = false;
    m_source = nullptr;
    if (m_thread) {
        m_thread->wait(5000);
        m_thread->deleteLater();
        m_thread = nullptr;
    }
}
