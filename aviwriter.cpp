#include "aviwriter.h"

#include <QDataStream>

/*
 * 文件布局（固定偏移，close() 时按这些偏移回填）：
 *   0   "RIFF" <size>  "AVI "
 *   12  "LIST" <192>   "hdrl"
 *   24  "avih" <56>    MainAVIHeader        (数据起始 32)
 *   88  "LIST" <116>   "strl"
 *   100 "strh" <56>    AVIStreamHeader      (数据起始 108)
 *   164 "strf" <40>    BITMAPINFOHEADER     (数据起始 172)
 *   212 "LIST" <size>  "movi"               ('movi' 四字符码在 220)
 *   224 第一帧数据块
 *   ... "idx1" <16*N>  索引
 */
namespace {

constexpr qint64 kAvihDataPos   = 32;
constexpr qint64 kStrhDataPos   = 108;
constexpr qint64 kMoviSizePos   = 216;   // movi LIST 的 dwSize 字段
constexpr qint64 kMoviFourccPos = 220;

inline void putU32(QDataStream &s, quint32 v) { s << v; }
inline void putU16(QDataStream &s, quint16 v) { s << v; }
inline void putFourCC(QDataStream &s, const char *c)
{
    s.writeRawData(c, 4);
}

} // namespace

AviMjpegWriter::~AviMjpegWriter()
{
    if (m_file.isOpen())
        close();
}

bool AviMjpegWriter::open(const QString &path, int width, int height, int fps)
{
    if (width <= 0 || height <= 0) {
        m_error = QStringLiteral("画面尺寸无效");
        return false;
    }
    m_width  = width  & ~1;    // 取偶，部分解码器对奇数宽高兼容不佳
    m_height = height & ~1;
    m_fps    = qBound(1, fps, 120);
    m_frameCount = 0;
    m_maxChunk   = 0;
    m_index.clear();

    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_error = QStringLiteral("无法创建文件: ") + m_file.errorString();
        return false;
    }
    if (!writeHeader()) {
        m_file.close();
        return false;
    }
    m_moviFourccPos = kMoviFourccPos;
    return true;
}

bool AviMjpegWriter::writeHeader()
{
    QDataStream s(&m_file);
    s.setByteOrder(QDataStream::LittleEndian);

    // ---- RIFF ----
    putFourCC(s, "RIFF");
    putU32(s, 0);                 // 尺寸，close() 回填
    putFourCC(s, "AVI ");

    // ---- LIST hdrl ----
    putFourCC(s, "LIST");
    putU32(s, 192);
    putFourCC(s, "hdrl");

    // ---- avih ----
    putFourCC(s, "avih");
    putU32(s, 56);
    putU32(s, static_cast<quint32>(1000000 / m_fps)); // dwMicroSecPerFrame
    putU32(s, 0);                                     // dwMaxBytesPerSec  (回填)
    putU32(s, 0);                                     // dwPaddingGranularity
    putU32(s, 0x10);                                  // dwFlags = AVIF_HASINDEX
    putU32(s, 0);                                     // dwTotalFrames     (回填)
    putU32(s, 0);                                     // dwInitialFrames
    putU32(s, 1);                                     // dwStreams
    putU32(s, 0);                                     // dwSuggestedBufferSize (回填)
    putU32(s, static_cast<quint32>(m_width));
    putU32(s, static_cast<quint32>(m_height));
    for (int i = 0; i < 4; ++i) putU32(s, 0);         // dwReserved[4]

    // ---- LIST strl ----
    putFourCC(s, "LIST");
    putU32(s, 116);
    putFourCC(s, "strl");

    // ---- strh ----
    putFourCC(s, "strh");
    putU32(s, 56);
    putFourCC(s, "vids");
    putFourCC(s, "MJPG");
    putU32(s, 0);                                     // dwFlags
    putU16(s, 0);                                     // wPriority
    putU16(s, 0);                                     // wLanguage
    putU32(s, 0);                                     // dwInitialFrames
    putU32(s, 1);                                     // dwScale
    putU32(s, static_cast<quint32>(m_fps));           // dwRate
    putU32(s, 0);                                     // dwStart
    putU32(s, 0);                                     // dwLength          (回填)
    putU32(s, 0);                                     // dwSuggestedBufferSize (回填)
    putU32(s, 0xFFFFFFFFu);                           // dwQuality = -1
    putU32(s, 0);                                     // dwSampleSize
    putU16(s, 0); putU16(s, 0);                       // rcFrame.left/top
    putU16(s, static_cast<quint16>(m_width));
    putU16(s, static_cast<quint16>(m_height));

    // ---- strf: BITMAPINFOHEADER ----
    putFourCC(s, "strf");
    putU32(s, 40);
    putU32(s, 40);                                    // biSize
    putU32(s, static_cast<quint32>(m_width));
    putU32(s, static_cast<quint32>(m_height));
    putU16(s, 1);                                     // biPlanes
    putU16(s, 24);                                    // biBitCount
    putFourCC(s, "MJPG");                             // biCompression
    putU32(s, static_cast<quint32>(m_width * m_height * 3));  // biSizeImage
    putU32(s, 0); putU32(s, 0);                       // biX/YPelsPerMeter
    putU32(s, 0); putU32(s, 0);                       // biClrUsed / biClrImportant

    // ---- LIST movi ----
    putFourCC(s, "LIST");
    putU32(s, 0);                                     // 尺寸，close() 回填
    putFourCC(s, "movi");

    if (m_file.pos() != 224) {
        m_error = QStringLiteral("AVI 头部偏移异常: %1").arg(m_file.pos());
        return false;
    }
    return true;
}

bool AviMjpegWriter::writeFrame(const QByteArray &jpeg)
{
    if (!m_file.isOpen() || jpeg.isEmpty())
        return false;

    if (m_file.size() + jpeg.size() + 64 > kMaxFileBytes) {
        m_error = QStringLiteral("已达到 AVI 单文件上限(约1.9GB)");
        return false;
    }

    const qint64 chunkPos = m_file.pos();
    QDataStream s(&m_file);
    s.setByteOrder(QDataStream::LittleEndian);

    putFourCC(s, "00dc");
    putU32(s, static_cast<quint32>(jpeg.size()));
    if (s.writeRawData(jpeg.constData(), static_cast<int>(jpeg.size())) != jpeg.size()) {
        m_error = QStringLiteral("写入帧数据失败: ") + m_file.errorString();
        return false;
    }
    if (jpeg.size() & 1) {          // RIFF 要求块按偶数字节对齐
        const char pad = 0;
        s.writeRawData(&pad, 1);
    }

    m_index.append({ static_cast<quint32>(chunkPos - m_moviFourccPos),
                     static_cast<quint32>(jpeg.size()) });
    m_maxChunk = qMax(m_maxChunk, static_cast<quint32>(jpeg.size()));
    ++m_frameCount;
    return true;
}

bool AviMjpegWriter::close()
{
    if (!m_file.isOpen())
        return false;

    QDataStream s(&m_file);
    s.setByteOrder(QDataStream::LittleEndian);

    // ---- idx1 ----
    const qint64 moviEnd = m_file.pos();
    putFourCC(s, "idx1");
    putU32(s, static_cast<quint32>(m_index.size() * 16));
    for (const auto &e : m_index) {
        putFourCC(s, "00dc");
        putU32(s, 0x10);            // AVIIF_KEYFRAME，MJPEG 每帧都是关键帧
        putU32(s, e.first);
        putU32(s, e.second);
    }

    const qint64 fileEnd = m_file.pos();
    const quint32 avgBytesPerSec =
        m_frameCount > 0
            ? static_cast<quint32>(double(moviEnd - 224) * m_fps / m_frameCount)
            : 0;

    // ---- 回填 ----
    auto patch = [&](qint64 pos, quint32 v) {
        m_file.seek(pos);
        QDataStream p(&m_file);
        p.setByteOrder(QDataStream::LittleEndian);
        p << v;
    };
    patch(4, static_cast<quint32>(fileEnd - 8));                 // RIFF size
    // 帧率可能被 setEffectiveFps 修正过，这两处必须一并回填
    patch(kAvihDataPos + 0,  static_cast<quint32>(1000000 / m_fps)); // dwMicroSecPerFrame
    patch(kStrhDataPos + 24, static_cast<quint32>(m_fps));           // strh.dwRate
    patch(kAvihDataPos + 4,  avgBytesPerSec);                    // dwMaxBytesPerSec
    patch(kAvihDataPos + 16, static_cast<quint32>(m_frameCount));// dwTotalFrames
    patch(kAvihDataPos + 28, m_maxChunk);                        // dwSuggestedBufferSize
    patch(kStrhDataPos + 32, static_cast<quint32>(m_frameCount));// dwLength
    patch(kStrhDataPos + 36, m_maxChunk);                        // dwSuggestedBufferSize
    patch(kMoviSizePos, static_cast<quint32>(moviEnd - kMoviFourccPos));

    m_file.flush();
    m_file.close();
    m_index.clear();
    return true;
}
