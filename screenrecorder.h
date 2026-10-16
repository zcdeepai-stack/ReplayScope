#ifndef SCREENRECORDER_H
#define SCREENRECORDER_H

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>
#include <QImage>
#include <QTimer>
#include <QSize>
#include <QElapsedTimer>
#include <functional>

#include "aviwriter.h"

/// 后台编码线程：GUI 线程只管抓帧塞进队列，JPEG 编码与写盘都在这里做，
/// 否则 30fps 的编码会直接卡住回放和界面。
class RecordThread : public QThread
{
    Q_OBJECT
public:
    RecordThread(const QString &path, int width, int height,
                 int fps, int jpegQuality, QObject *parent = nullptr);

    /// 由 GUI 线程调用。队列积压时丢帧而不是无限吃内存。
    void push(const QImage &frame);
    /// 通知结束，之后线程会把队列里剩余的帧写完再退出。
    /// effectiveFps 为实际达成的帧率，写进 AVI 头，保证视频时长与真实时长一致。
    void requestFinish(int effectiveFps);

    int droppedFrames() const;

signals:
    void failed(const QString &message);
    void done(const QString &path, int frames, qint64 bytes);

protected:
    void run() override;

private:
    static constexpr int kMaxQueued = 12;

    QString m_path;
    int     m_width;
    int     m_height;
    int     m_fps;
    int     m_quality;

    QSize           m_target;
    mutable QMutex  m_mutex;
    QWaitCondition  m_cond;
    QQueue<QImage>  m_queue;
    bool            m_finishRequested = false;
    int             m_effectiveFps = 0;
    int             m_dropped = 0;
};


/// 录制 3D 渲染画面。帧来源由外部以回调提供（通常是 RenderView::grabScreenshot）。
class ScreenRecorder : public QObject
{
    Q_OBJECT
public:
    explicit ScreenRecorder(QObject *parent = nullptr);
    ~ScreenRecorder() override;

    using FrameSource = std::function<QPixmap()>;

    /// 开始录制。失败会发出 errorOccurred 并返回 false。
    bool start(const QString &filePath, FrameSource source, int fps = 30);
    /// 停止并完成文件写入（异步，完成后发出 finished）
    void stop();
    bool isRecording() const { return m_thread != nullptr; }

    int fps() const { return m_fps; }

signals:
    void started(const QString &filePath, const QSize &frameSize, int fps);
    void finished(const QString &filePath, int frames, qint64 bytes, int dropped);
    void errorOccurred(const QString &message);

private slots:
    void onTick();
    void onThreadDone(const QString &path, int frames, qint64 bytes);
    void onThreadFailed(const QString &message);

private:
    void teardown();
    void scheduleNext();

    /// 抓帧最多只允许占用 GUI 线程这么多比例，超了就自动降帧率。
    /// 抓 GPU 正在渲染的窗口可能很慢，若不设限会把事件循环饿死，
    /// 表现就是播放/暂停/拖动全部失灵。
    static constexpr int kMaxGuiLoadPercent = 25;

    QTimer        m_timer;              // 单次触发，做完再排下一次，杜绝堆积
    QElapsedTimer m_clock;
    RecordThread *m_thread = nullptr;
    FrameSource   m_source;
    QSize         m_frameSize;
    int           m_fps = 30;
    qint64        m_baseIntervalMs = 33;
    qint64        m_intervalMs = 33;
    qint64        m_nextDueMs = 0;
    int           m_captured = 0;
    bool          m_stopping = false;
    bool          m_inTick = false;
};

#endif // SCREENRECORDER_H
