#ifndef CSVPARSER_H
#define CSVPARSER_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

/// 四足机器狗快照 CSV / 普通 CSV 的解析结果。
/// 快照格式: 第1行=信息头, 第2行=信息值, 第3行=数据列头, 第4行起=数据
/// 普通格式: 第1行=列头(或直接就是数据), 其后=数据
struct CsvParseResult {
    QStringList          infoHeaders;   ///< 仅快照格式有值
    QVector<double>      infoValues;    ///< 仅快照格式有值
    QStringList          headers;       ///< 数据列名
    QVector<QVector<double>> columns;   ///< 按列存放，长度 == colCount
    int    rowCount   = 0;
    int    colCount   = 0;
    double sampleRate = 200.0;          ///< 快照格式下由 Alg_Period (us) 反算
    bool   isSnapshot = false;
    /// Timestamp 列是否单调不减。用于告诉绘图库跳过排序（大文件下省一次 O(n log n)）。
    bool   timeSorted = true;

    double totalTime() const {
        return rowCount > 0 ? double(rowCount) / sampleRate : 0.0;
    }
    int findColumn(const QString &name) const { return headers.indexOf(name); }
};

/// 进度回调: (已读字节, 总字节)。返回 false 表示请求中止解析。
using CsvProgressFn = std::function<bool(qint64, qint64)>;

/// 解析 CSV 文件。失败(打不开/内容为空)返回 false。
/// 语义与原先 QTextStream+split(',') 的实现完全一致：
///   - 行首尾空白去除、行尾多余逗号去除
///   - 空行与 '#' 开头的行跳过（仅数据区）
///   - 字段非数值或缺失一律记为 0.0
bool parseCsvFile(const QString &path, CsvParseResult &out,
                  const CsvProgressFn &progress = {});

#endif // CSVPARSER_H
