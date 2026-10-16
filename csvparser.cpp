#include "csvparser.h"

#include <QFile>
#include <charconv>

/*
 * 逐字节解析，刻意不使用 QTextStream / QString::split：
 * 128MB / 25万行 × 150列 的快照文件用原来的写法要产生近 4000 万次 QString 分配，
 * 这里改成在原始字节上切片 + std::from_chars，只有列名才真正构造 QString。
 */

namespace {

constexpr qint64 kChunkSize      = 4 << 20;          // 4MB
constexpr qint64 kMaxReserveBytes = 512LL << 20;     // 预留上限，防止行长估错时吃爆内存

struct Field { const char *b; const char *e; };
using FieldList = QVector<Field>;

inline const char *trimFront(const char *b, const char *e)
{
    while (b < e && static_cast<unsigned char>(*b) <= ' ') ++b;
    return b;
}

inline const char *trimBack(const char *b, const char *e)
{
    while (e > b && static_cast<unsigned char>(e[-1]) <= ' ') --e;
    return e;
}

/// 等价于 QString::trimmed().toDouble(&ok)（额外接受前导 '+'，from_chars 本身不接受）
inline bool fieldToDouble(const char *b, const char *e, double &out)
{
    b = trimFront(b, e);
    e = trimBack(b, e);
    if (b == e) return false;
    if (*b == '+') ++b;
    if (b == e) return false;
    const auto res = std::from_chars(b, e, out);
    return res.ec == std::errc() && res.ptr == e;
}

inline QString fieldToString(const char *b, const char *e)
{
    b = trimFront(b, e);
    e = trimBack(b, e);
    return QString::fromUtf8(b, static_cast<qsizetype>(e - b));
}

/// 行规整：去首尾空白，再去掉行尾多余逗号（对应原实现的 trimmed() + chop(',')）
inline void normalizeLine(const char *&b, const char *&e)
{
    b = trimFront(b, e);
    e = trimBack(b, e);
    while (e > b && e[-1] == ',') --e;
}

inline void splitFields(const char *b, const char *e, FieldList &fields)
{
    fields.clear();
    const char *fieldStart = b;
    for (const char *p = b; p < e; ++p) {
        if (*p == ',') {
            fields.append(Field{fieldStart, p});
            fieldStart = p + 1;
        }
    }
    fields.append(Field{fieldStart, e});
}

/// 所有字段都能转成 double（空字段视为数值），对应原来的 isNumericLine
inline bool isNumericLine(const FieldList &fields)
{
    for (const Field &f : fields) {
        const char *b = trimFront(f.b, f.e);
        const char *e = trimBack(b, f.e);
        if (b == e) continue;                 // 空字段算数值
        double v;
        if (!fieldToDouble(b, e, v)) return false;
    }
    return true;
}

} // namespace

bool parseCsvFile(const QString &path, CsvParseResult &out, const CsvProgressFn &progress)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const qint64 totalBytes = file.size();
    out = CsvParseResult();

    QVector<QByteArray> headerLines;   // 头部阶段先攒 3 个非空行再判格式
    bool  headerDone   = false;
    int   dataStartIdx = 0;
    bool  reserved     = false;

    FieldList fields;
    fields.reserve(256);

    int    tsCol      = -1;
    double lastTs     = 0.0;
    bool   haveLastTs = false;

    // ---- 头部处理：决定是快照格式还是普通 CSV ----
    auto finishHeader = [&]() {
        FieldList t0, t1, t2;
        const auto split = [&](int i, FieldList &dst) {
            const QByteArray &l = headerLines[i];
            splitFields(l.constData(), l.constData() + l.size(), dst);
        };
        split(0, t0);

        if (headerLines.size() >= 3) {
            split(1, t1);
            split(2, t2);
            if (!isNumericLine(t0) && isNumericLine(t1) && !isNumericLine(t2)
                && t2.size() > t0.size() * 2) {
                out.isSnapshot = true;
                for (const Field &f : t0) out.infoHeaders.append(fieldToString(f.b, f.e));
                for (const Field &f : t1) {
                    double v = 0.0;
                    fieldToDouble(f.b, f.e, v);
                    out.infoValues.append(v);
                }
                out.colCount = t2.size();
                for (const Field &f : t2) out.headers.append(fieldToString(f.b, f.e));
                dataStartIdx = 3;
            }
        }

        if (!out.isSnapshot) {
            out.colCount = t0.size();
            if (!isNumericLine(t0)) {
                for (const Field &f : t0) out.headers.append(fieldToString(f.b, f.e));
                dataStartIdx = 1;
            } else {
                for (int i = 0; i < out.colCount; ++i)
                    out.headers.append(QString("Col %1").arg(i));
                dataStartIdx = 0;
            }
        }

        out.columns.resize(out.colCount);

        if (out.isSnapshot) {
            const int idx = out.infoHeaders.indexOf(QStringLiteral("Alg_Period (us)"));
            if (idx >= 0 && idx < out.infoValues.size() && out.infoValues[idx] > 0)
                out.sampleRate = 1e6 / out.infoValues[idx];
        }

        tsCol      = out.headers.indexOf(QStringLiteral("Timestamp (s)"));
        headerDone = true;
    };

    // 用真实数据行长度估算总行数并一次性预留，避免 150 个 vector 各自反复 realloc
    auto reserveColumns = [&](qsizetype dataLineLen) {
        reserved = true;
        if (out.colCount <= 0 || dataLineLen <= 0) return;
        const qint64 maxRows = kMaxReserveBytes / (qint64(out.colCount) * qint64(sizeof(double)));
        const qint64 est = qBound<qint64>(1024, totalBytes / dataLineLen + 64, qMax<qint64>(1024, maxRows));
        for (auto &col : out.columns)
            col.reserve(static_cast<qsizetype>(est));
    };

    auto appendDataLine = [&](const char *b, const char *e) {
        if (!reserved) reserveColumns(e - b + 1);

        splitFields(b, e, fields);
        const int n = qMin<int>(fields.size(), out.colCount);
        for (int i = 0; i < n; ++i) {
            double v = 0.0;
            if (!fieldToDouble(fields[i].b, fields[i].e, v))
                v = 0.0;
            out.columns[i].append(v);
        }
        for (int i = fields.size(); i < out.colCount; ++i)
            out.columns[i].append(0.0);
        ++out.rowCount;

        if (tsCol >= 0 && tsCol < out.colCount) {
            const double ts = out.columns[tsCol].constLast();
            if (haveLastTs && ts < lastTs) out.timeSorted = false;
            lastTs     = ts;
            haveLastTs = true;
        }
    };

    auto flushHeaderAsData = [&]() {
        for (int i = dataStartIdx; i < headerLines.size(); ++i) {
            const QByteArray &hl = headerLines[i];
            appendDataLine(hl.constData(), hl.constData() + hl.size());
        }
    };

    auto handleLine = [&](const char *b, const char *e) {
        normalizeLine(b, e);

        if (!headerDone) {
            if (b == e) return;                        // 头部阶段跳过空行
            headerLines.append(QByteArray(b, static_cast<qsizetype>(e - b)));
            if (headerLines.size() == 3) {
                finishHeader();
                flushHeaderAsData();
            }
            return;
        }

        if (b == e || *b == '#') return;               // 数据区跳过空行与注释
        appendDataLine(b, e);
    };

    // ---- 分块读取，按 '\n' 切行 ----
    QByteArray carry;
    qint64 bytesRead = 0;
    bool   aborted   = false;

    while (true) {
        QByteArray chunk = file.read(kChunkSize);
        if (chunk.isEmpty()) break;
        bytesRead += chunk.size();

        if (!carry.isEmpty()) {
            chunk.prepend(carry);
            carry.clear();
        }

        const char *p    = chunk.constData();
        const char *end  = p + chunk.size();
        const char *line = p;
        for (const char *q = p; q < end; ++q) {
            if (*q == '\n') {
                handleLine(line, q);
                line = q + 1;
            }
        }
        if (line < end)
            carry = QByteArray(line, static_cast<qsizetype>(end - line));

        if (progress && !progress(bytesRead, totalBytes)) {
            aborted = true;
            break;
        }
    }

    if (!aborted && !carry.isEmpty())
        handleLine(carry.constData(), carry.constData() + carry.size());

    file.close();

    // 全文不足 3 个非空行：按普通 CSV 处理（原实现同样只看已读到的行）
    if (!headerDone) {
        if (headerLines.isEmpty())
            return false;
        finishHeader();
        flushHeaderAsData();
    }

    if (progress) progress(totalBytes, totalBytes);
    return out.colCount > 0;
}
