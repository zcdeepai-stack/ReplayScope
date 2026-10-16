#ifndef AVIWRITER_H
#define AVIWRITER_H

#include <QString>
#include <QByteArray>
#include <QVector>
#include <QFile>
#include <QtGlobal>

/// 最小实现的 MJPEG AVI 写入器（RIFF/AVI + idx1 索引）。
/// 选择 MJPEG 是因为 Qt 自带 JPEG 编码，不需要引入任何外部编解码依赖，
/// 且 MJPEG AVI 几乎所有播放器都能直接播放。
class AviMjpegWriter
{
public:
    ~AviMjpegWriter();

    /// 宽高必须为偶数且 > 0；fps 建议 10~60
    bool open(const QString &path, int width, int height, int fps);
    /// 追加一帧已编码好的 JPEG 数据
    bool writeFrame(const QByteArray &jpeg);
    /// 用实际达成的帧率覆盖头部（在 close() 之前调用）。
    /// 抓帧被限流时真实帧率低于目标值，不修正的话视频会比真实过程放得更快。
    void setEffectiveFps(int fps) { if (fps > 0) m_fps = qBound(1, fps, 120); }
    /// 回填帧数等头部字段并写入索引。close 之后文件才是完整可播放的。
    bool close();

    bool    isOpen() const { return m_file.isOpen(); }
    int     frameCount() const { return m_frameCount; }
    qint64  bytesWritten() const { return m_file.size(); }
    QString lastError() const { return m_error; }

    /// RIFF 的 dwSize 是 32 位，超过 2GB 的 AVI 兼容性很差，这里主动设上限
    static constexpr qint64 kMaxFileBytes = 1900LL * 1024 * 1024;

private:
    bool writeHeader();

    QFile   m_file;
    QString m_error;
    int     m_width = 0;
    int     m_height = 0;
    int     m_fps = 30;
    int     m_frameCount = 0;
    quint32 m_maxChunk = 0;
    qint64  m_moviFourccPos = 0;   // 'movi' 四字符码所在偏移，索引以此为基准
    QVector<QPair<quint32, quint32>> m_index;   // (相对偏移, 数据长度)
};

#endif // AVIWRITER_H
