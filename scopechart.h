#ifndef SCOPECHART_H
#define SCOPECHART_H

#include <QWidget>
#include <QVector>
#include <QColor>

class QCustomPlot;
class QCPGraph;
class QComboBox;
class QHBoxLayout;
class QCPTextElement;
class QCPItemLine;
class QCPItemText;
class QCPItemRect;
class QFrame;
class QLabel;

struct CurveInfo {
    QString name;
    QColor  color;
    int     columnIdx;
};

class ScopeChart : public QWidget
{
    Q_OBJECT

public:
    explicit ScopeChart(const QString &title,
                        const QVector<CurveInfo> &curves,
                        QWidget *parent = nullptr);

    QCustomPlot *plot() const { return m_plot; }

    void addSelector(const QString &label, const QStringList &options);
    QString selectorValue(int selectorIdx) const;
    int selectorCurrentIndex(int selectorIdx) const;

    void setTitle(const QString &title);
    void setTimeRange(double tMin, double tMax);
    void appendData(int curveIndex, double t, double value);
    /// alreadySorted=true 可跳过 QCustomPlot 内部按 x 排序（大数据量下省一次 O(n log n)）
    void setData(int curveIndex,
                 const QVector<double> &t,
                 const QVector<double> &v,
                 bool alreadySorted = false);
    void clearData();
    void rescaleAndReplot();
    /// 批量装载数据期间挂起重绘，结束时只重绘一次
    void setReplotSuspended(bool suspended);

    void setPlaybackCursor(double time);
    void clearPlaybackCursor();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

signals:
    void selectorChanged();
    void wheelScrolled(int angleDelta, Qt::KeyboardModifiers modifiers);
    void rangeSelected(double tMin, double tMax);

private slots:
    void onMouseMove(QMouseEvent *event);
    void onMouseLeave();
    void onMousePress(QMouseEvent *event);
    void onMouseRelease(QMouseEvent *event);

private:
    void setupPlot(const QString &title, const QVector<CurveInfo> &curves);
    /// 统一的重绘入口：挂起期间跳过。默认立即重绘，与改造前的行为一致；
    /// 只有回放光标这类高频调用才传 queued=true（对应 rpQueuedReplot）。
    /// 这里用 bool 而不是 QCustomPlot::RefreshPriority，因为本头文件只前置声明 QCustomPlot。
    void requestReplot(bool queued = false);

    bool m_replotSuspended = false;

    QWidget        *m_selectorBar = nullptr;
    QHBoxLayout    *m_selectorLay = nullptr;
    QVector<QComboBox*> m_selectors;

    QWidget        *m_legendWidget = nullptr;
    QCustomPlot    *m_plot = nullptr;
    QCPTextElement *m_titleElement = nullptr;
    QVector<QCPGraph*> m_graphs;

    struct LegendItem {
        QWidget *row = nullptr;
        QFrame  *colorLine = nullptr;
        QLabel  *label = nullptr;
    };
    QVector<LegendItem> m_legendItems;
    QCPItemLine *m_cursorLine = nullptr;
    QCPItemText *m_tooltip = nullptr;
    QCPItemLine *m_tooltipVLine = nullptr;

    // 区间选取
    bool m_selecting = false;
    double m_selectStartX = 0;
    QCPItemLine *m_selectLineL = nullptr;
    QCPItemLine *m_selectLineR = nullptr;
    QCPItemRect *m_selectRect = nullptr;
};

#endif // SCOPECHART_H
