#include "scopechart.h"
#include "qcustomplot.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QLabel>
#include <QFrame>
#include <QMouseEvent>
#include <QWheelEvent>

// ---- 科技风配色 ----
static const QColor BG_COLOR(10, 14, 20);
static const QColor GRID_COLOR(30, 45, 65);
static const QColor AXIS_COLOR(0, 180, 220);
static const QColor TICK_LABEL_COLOR(140, 170, 200);
static const QColor TITLE_COLOR(0, 212, 255);

ScopeChart::ScopeChart(const QString &title,
                       const QVector<CurveInfo> &curves,
                       QWidget *parent)
    : QWidget(parent)
{
    setStyleSheet("QWidget { border: none; background: #0a0e14; }");
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto *plotRow = new QHBoxLayout;
    plotRow->setSpacing(0);
    plotRow->setContentsMargins(0, 0, 0, 0);

    m_plot = new QCustomPlot(this);
    m_plot->setMinimumHeight(120);
    plotRow->addWidget(m_plot, 1);

    // 自定义图例面板（图表右侧）
    m_legendWidget = new QWidget(this);
    m_legendWidget->setFixedWidth(120);
    m_legendWidget->setStyleSheet("background: #0a0e14; border: none;");
    auto *legendLay = new QVBoxLayout(m_legendWidget);
    legendLay->setContentsMargins(6, 28, 4, 4);
    legendLay->setSpacing(2);

    int curveIdx = 0;
    for (const auto &ci : curves) {
        auto *row = new QWidget;
        row->setStyleSheet("background: transparent;");
        row->setCursor(Qt::PointingHandCursor);
        auto *rLay = new QHBoxLayout(row);
        rLay->setContentsMargins(0, 1, 0, 1);
        rLay->setSpacing(4);

        auto *colorLine = new QFrame;
        colorLine->setFixedSize(14, 2);
        colorLine->setStyleSheet(QString("background: %1; border: none;").arg(ci.color.name()));
        rLay->addWidget(colorLine);

        auto *lbl = new QLabel(ci.name);
        lbl->setStyleSheet("color: #b0d0e8; font-size: 8pt; font-family: 'Microsoft YaHei'; border: none; background: transparent;");
        rLay->addWidget(lbl);
        rLay->addStretch();

        row->setProperty("curveIndex", curveIdx);
        row->installEventFilter(this);
        m_legendItems.append({row, colorLine, lbl});

        legendLay->addWidget(row);
        curveIdx++;
    }
    legendLay->addStretch();
    plotRow->addWidget(m_legendWidget);

    lay->addLayout(plotRow, 1);

    setupPlot(title, curves);
}

void ScopeChart::setupPlot(const QString &title,
                           const QVector<CurveInfo> &curves)
{
    m_plot->setBackground(QBrush(BG_COLOR));
    m_plot->axisRect()->setBackground(QBrush(QColor(12, 18, 28)));

    auto setupAxis = [](QCPAxis *axis) {
        axis->setBasePen(QPen(AXIS_COLOR, 1));
        axis->setTickPen(QPen(AXIS_COLOR, 1));
        axis->setSubTickPen(QPen(GRID_COLOR, 1));
        axis->setTickLabelColor(TICK_LABEL_COLOR);
        axis->setLabelColor(TICK_LABEL_COLOR);
        axis->setTickLabelFont(QFont("Consolas", 8));

        axis->grid()->setPen(QPen(GRID_COLOR, 1, Qt::DotLine));
        axis->grid()->setSubGridPen(QPen(QColor(20, 30, 45), 1, Qt::DotLine));
        axis->grid()->setSubGridVisible(true);
        axis->grid()->setZeroLinePen(QPen(QColor(0, 120, 160), 1, Qt::DashLine));
    };

    setupAxis(m_plot->xAxis);
    setupAxis(m_plot->yAxis);

    m_plot->xAxis->ticker()->setTickCount(10);
    m_plot->yAxis->ticker()->setTickCount(8);
    m_plot->xAxis->setNumberFormat("f");
    m_plot->xAxis->setNumberPrecision(1);

    m_plot->xAxis->setLabel("时间 (s)");
    m_plot->xAxis->setLabelFont(QFont("Microsoft YaHei", 8));
    m_plot->yAxis->setLabelFont(QFont("Microsoft YaHei", 8));

    m_plot->plotLayout()->insertRow(0);
    m_titleElement = new QCPTextElement(m_plot, title,
                                        QFont("Microsoft YaHei", 10, QFont::Bold));
    m_titleElement->setTextColor(TITLE_COLOR);
    m_plot->plotLayout()->addElement(0, 0, m_titleElement);

    m_plot->legend->setVisible(false);

    for (const auto &ci : curves) {
        QCPGraph *g = m_plot->addGraph();
        g->setName(ci.name);
        g->setPen(QPen(ci.color, 1.5));
        g->setAntialiased(true);
        QColor fill = ci.color;
        fill.setAlpha(15);
        g->setBrush(QBrush(fill));
        m_graphs.append(g);
    }

    // 保留滚轮缩放 + 拖拽平移
    m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    m_plot->axisRect()->setRangeDrag(Qt::Horizontal | Qt::Vertical);
    m_plot->axisRect()->setRangeZoom(Qt::Horizontal | Qt::Vertical);

    // 启用鼠标追踪用于悬停tooltip
    m_plot->setMouseTracking(true);
    connect(m_plot, &QCustomPlot::mouseMove, this, &ScopeChart::onMouseMove);
    connect(m_plot, &QCustomPlot::mousePress, this, &ScopeChart::onMousePress);
    connect(m_plot, &QCustomPlot::mouseRelease, this, &ScopeChart::onMouseRelease);

    // tooltip 文字
    m_tooltip = new QCPItemText(m_plot);
    m_tooltip->setPositionAlignment(Qt::AlignLeft | Qt::AlignBottom);
    m_tooltip->position->setType(QCPItemPosition::ptAbsolute);
    m_tooltip->setFont(QFont("Consolas", 9));
    m_tooltip->setColor(QColor(220, 240, 255));
    m_tooltip->setPadding(QMargins(6, 4, 6, 4));
    m_tooltip->setBrush(QBrush(QColor(12, 24, 40, 220)));
    m_tooltip->setPen(QPen(QColor(0, 160, 220, 180), 1));
    m_tooltip->setLayer("overlay");
    m_tooltip->setClipToAxisRect(false);
    m_tooltip->setVisible(false);

    // tooltip 竖线
    m_tooltipVLine = new QCPItemLine(m_plot);
    m_tooltipVLine->setPen(QPen(QColor(0, 180, 220, 120), 1, Qt::DashLine));
    m_tooltipVLine->setClipToAxisRect(true);
    m_tooltipVLine->setLayer("overlay");
    m_tooltipVLine->setVisible(false);

    // 区间选取左竖线
    m_selectLineL = new QCPItemLine(m_plot);
    m_selectLineL->setPen(QPen(QColor(255, 200, 0, 200), 1.5, Qt::SolidLine));
    m_selectLineL->setClipToAxisRect(true);
    m_selectLineL->setVisible(false);

    // 区间选取右竖线
    m_selectLineR = new QCPItemLine(m_plot);
    m_selectLineR->setPen(QPen(QColor(255, 200, 0, 200), 1.5, Qt::SolidLine));
    m_selectLineR->setClipToAxisRect(true);
    m_selectLineR->setVisible(false);

    // 区间选取阴影矩形
    m_selectRect = new QCPItemRect(m_plot);
    m_selectRect->setPen(QPen(QColor(255, 200, 0, 80)));
    m_selectRect->setBrush(QBrush(QColor(255, 200, 0, 30)));
    m_selectRect->setClipToAxisRect(true);
    m_selectRect->setVisible(false);

    // 固定左右 margin，确保同一页面所有图表的绘图区域宽度完全一致
    m_plot->axisRect()->setAutoMargins(QCP::msTop | QCP::msBottom);
    QMargins margins = m_plot->axisRect()->margins();
    margins.setLeft(60);
    margins.setRight(15);
    m_plot->axisRect()->setMargins(margins);

    m_plot->xAxis->setRange(0, 10);
    m_plot->yAxis->setRange(-1, 1);

    m_plot->replot();
}

void ScopeChart::requestReplot(bool queued)
{
    if (m_replotSuspended)
        return;
    m_plot->replot(queued ? QCustomPlot::rpQueuedReplot
                          : QCustomPlot::rpRefreshHint);
}

void ScopeChart::setReplotSuspended(bool suspended)
{
    if (m_replotSuspended == suspended)
        return;
    m_replotSuspended = suspended;
    if (suspended)
        return;

    // 切 Tab 时 QStackedWidget::setCurrentIndex 之后布局尚未生效，
    // 此刻图表还是隐藏期间的旧几何。QCustomPlot 只在 resizeEvent 里同步视口，
    // 若尺寸恰好没变就不会重算，画出来的是过期布局（表现为整块空白）。
    // 这里主动对齐一次，等价于 resizeEvent 做的事，代价约 1ms。
    if (m_plot->viewport() != m_plot->rect())
        m_plot->setViewport(m_plot->rect());

    // 必须立即重绘，不能用 rpQueuedReplot：排队要等下一轮事件循环，
    // 装载大数据时会让图表长时间空白。
    m_plot->replot();
}

void ScopeChart::setTimeRange(double tMin, double tMax)
{
    m_plot->xAxis->setRange(tMin, tMax);

    // Y 轴自动适配可见 X 范围内的数据
    double yMin = 1e18, yMax = -1e18;
    bool found = false;
    for (auto *g : m_graphs) {
        if (!g->visible() || g->data()->isEmpty()) continue;
        auto begin = g->data()->findBegin(tMin, false);
        auto end   = g->data()->findEnd(tMax, false);
        for (auto it = begin; it != end; ++it) {
            if (it->value < yMin) yMin = it->value;
            if (it->value > yMax) yMax = it->value;
            found = true;
        }
    }
    if (found) {
        double range = yMax - yMin;
        double margin = range * 0.08;
        if (range < 1e-10) margin = qMax(qAbs(yMin) * 0.1, 0.5);
        m_plot->yAxis->setRange(yMin - margin, yMax + margin);
    }

    requestReplot();
}

void ScopeChart::appendData(int curveIndex, double t, double value)
{
    if (curveIndex < 0 || curveIndex >= m_graphs.size()) return;
    m_graphs[curveIndex]->addData(t, value);
}

void ScopeChart::setData(int curveIndex,
                         const QVector<double> &t,
                         const QVector<double> &v,
                         bool alreadySorted)
{
    if (curveIndex < 0 || curveIndex >= m_graphs.size()) return;
    m_graphs[curveIndex]->setData(t, v, alreadySorted);
}

void ScopeChart::rescaleAndReplot()
{
    m_plot->rescaleAxes();
    requestReplot();
}

void ScopeChart::clearData()
{
    for (auto *g : m_graphs)
        g->data()->clear();
    requestReplot();
}

// ---- 图例点击切换曲线可见性 ----
bool ScopeChart::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        int idx = obj->property("curveIndex").toInt();
        if (idx >= 0 && idx < m_graphs.size() && idx < m_legendItems.size()) {
            bool wasVisible = m_graphs[idx]->visible();
            m_graphs[idx]->setVisible(!wasVisible);
            if (wasVisible) {
                m_legendItems[idx].colorLine->setStyleSheet("background: #333; border: none;");
                m_legendItems[idx].label->setStyleSheet(
                    "color: #555; font-size: 8pt; font-family: 'Microsoft YaHei'; "
                    "border: none; background: transparent;");
            } else {
                m_legendItems[idx].colorLine->setStyleSheet(
                    QString("background: %1; border: none;")
                        .arg(m_graphs[idx]->pen().color().name()));
                m_legendItems[idx].label->setStyleSheet(
                    "color: #b0d0e8; font-size: 8pt; font-family: 'Microsoft YaHei'; "
                    "border: none; background: transparent;");
            }
            m_plot->replot();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

// ---- 选择器支持 ----
void ScopeChart::addSelector(const QString &label, const QStringList &options)
{
    if (!m_selectorBar) {
        m_selectorBar = new QWidget;
        m_selectorBar->setFixedHeight(30);
        m_selectorBar->setStyleSheet(R"(
            QWidget { background: #0c1420; border: none; }
            QLabel { color: #6090b0; font-size: 10px; font-family: "Microsoft YaHei"; border: none; }
            QComboBox {
                background: #0a1828; color: #00d4ff;
                border: 1px solid #1a4060; border-radius: 3px;
                padding: 2px 8px; font-family: "Microsoft YaHei"; font-size: 10px;
                min-width: 70px;
            }
            QComboBox:hover { border-color: #00d4ff; }
            QComboBox::drop-down { border: none; width: 16px; }
            QComboBox::down-arrow {
                image: none;
                border-left: 4px solid transparent;
                border-right: 4px solid transparent;
                border-top: 5px solid #00d4ff;
            }
            QComboBox QAbstractItemView {
                background: #0c1824; color: #00d4ff;
                border: 1px solid #1a4060; selection-background-color: #1a3050;
            }
        )");
        m_selectorLay = new QHBoxLayout(m_selectorBar);
        m_selectorLay->setContentsMargins(8, 2, 8, 2);
        m_selectorLay->setSpacing(6);
        m_selectorLay->addStretch();

        static_cast<QVBoxLayout*>(layout())->insertWidget(0, m_selectorBar);
    }

    int insertPos = m_selectorLay->count() - 1;

    auto *lbl = new QLabel(label + ":");
    m_selectorLay->insertWidget(insertPos, lbl);

    auto *combo = new QComboBox;
    combo->addItems(options);
    m_selectorLay->insertWidget(insertPos + 1, combo);

    m_selectors.append(combo);
    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ScopeChart::selectorChanged);
}

QString ScopeChart::selectorValue(int idx) const
{
    if (idx >= 0 && idx < m_selectors.size())
        return m_selectors[idx]->currentText();
    return {};
}

int ScopeChart::selectorCurrentIndex(int idx) const
{
    if (idx >= 0 && idx < m_selectors.size())
        return m_selectors[idx]->currentIndex();
    return -1;
}

void ScopeChart::setTitle(const QString &title)
{
    if (m_titleElement) {
        m_titleElement->setText(title);
        m_plot->replot();
    }
}

void ScopeChart::setPlaybackCursor(double time)
{
    if (!m_cursorLine) {
        m_cursorLine = new QCPItemLine(m_plot);
        m_cursorLine->setPen(QPen(QColor(255, 80, 80, 200), 1.5, Qt::DashLine));
        m_cursorLine->setClipToAxisRect(true);
    }
    double yLo = m_plot->yAxis->range().lower;
    double yHi = m_plot->yAxis->range().upper;
    m_cursorLine->start->setCoords(time, yLo);
    m_cursorLine->end->setCoords(time, yHi);
    m_cursorLine->setVisible(true);
    // 坐标始终更新，但只有当前可见的图表才重绘：回放时上层会对全部 6 个 Tab
    // 共 17 张图表调用本函数，隐藏页的 replot 纯属浪费（切回该页时
    // CurveViewer::onTabChanged -> refreshCurrentTab 会重绘并带上正确的光标）。
    if (isVisible())
        requestReplot(true);
}

void ScopeChart::clearPlaybackCursor()
{
    if (m_cursorLine) {
        m_cursorLine->setVisible(false);
        requestReplot(true);
    }
}

// ---- 鼠标悬停显示数据点坐标 ----
void ScopeChart::onMouseMove(QMouseEvent *event)
{
    // 区间选取拖拽中（右键）
    if (m_selecting) {
        double curX = m_plot->xAxis->pixelToCoord(event->pos().x());
        double yLo = m_plot->yAxis->range().lower;
        double yHi = m_plot->yAxis->range().upper;
        double xL = qMin(m_selectStartX, curX);
        double xR = qMax(m_selectStartX, curX);

        m_selectLineL->start->setCoords(xL, yLo);
        m_selectLineL->end->setCoords(xL, yHi);
        m_selectLineL->setVisible(true);

        m_selectLineR->start->setCoords(xR, yLo);
        m_selectLineR->end->setCoords(xR, yHi);
        m_selectLineR->setVisible(true);

        m_selectRect->topLeft->setCoords(xL, yHi);
        m_selectRect->bottomRight->setCoords(xR, yLo);
        m_selectRect->setVisible(true);

        m_plot->replot(QCustomPlot::rpQueuedReplot);
        return;
    }

    if (!m_plot->axisRect()->rect().contains(event->pos())) {
        onMouseLeave();
        return;
    }

    double mouseX = m_plot->xAxis->pixelToCoord(event->pos().x());

    // 找到最近的可见曲线上最近的数据点
    double bestDist = 1e18;
    int bestGraph = -1;
    double bestT = 0, bestV = 0;
    QString bestName;

    for (int gi = 0; gi < m_graphs.size(); ++gi) {
        QCPGraph *g = m_graphs[gi];
        if (!g->visible() || g->data()->isEmpty()) continue;

        // 二分查找最近的 x
        auto it = g->data()->findBegin(mouseX, false);
        // 检查 it 和前一个点
        QVector<QCPGraphDataContainer::const_iterator> candidates;
        if (it != g->data()->constEnd()) candidates.append(it);
        if (it != g->data()->constBegin()) { --it; candidates.append(it); }

        for (auto &cand : candidates) {
            double px = m_plot->xAxis->coordToPixel(cand->key);
            double py = m_plot->yAxis->coordToPixel(cand->value);
            double dx = px - event->pos().x();
            double dy = py - event->pos().y();
            double dist = dx * dx + dy * dy;
            if (dist < bestDist) {
                bestDist = dist;
                bestGraph = gi;
                bestT = cand->key;
                bestV = cand->value;
                bestName = g->name();
            }
        }
    }

    // 距离阈值: 50像素以内
    if (bestGraph < 0 || bestDist > 50.0 * 50.0) {
        onMouseLeave();
        return;
    }

    // 更新 tooltip 文字
    QString text = QString("%1\nt = %2 s\nv = %3")
                       .arg(bestName)
                       .arg(bestT, 0, 'f', 3)
                       .arg(bestV, 0, 'f', 4);
    m_tooltip->setText(text);

    // 定位tooltip（像素坐标），避免超出绘图区
    double tipX = event->pos().x() + 14;
    double tipY = event->pos().y() - 14;
    QRect axRect = m_plot->axisRect()->rect();
    if (tipX + 120 > axRect.right()) tipX = event->pos().x() - 130;
    if (tipY - 50 < axRect.top()) tipY = event->pos().y() + 20;
    m_tooltip->position->setPixelPosition(QPointF(tipX, tipY));
    m_tooltip->setVisible(true);

    // 竖线
    double yLo = m_plot->yAxis->range().lower;
    double yHi = m_plot->yAxis->range().upper;
    m_tooltipVLine->start->setCoords(bestT, yLo);
    m_tooltipVLine->end->setCoords(bestT, yHi);
    m_tooltipVLine->setVisible(true);

    m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void ScopeChart::onMouseLeave()
{
    if (m_tooltip && m_tooltip->visible()) {
        m_tooltip->setVisible(false);
        m_tooltipVLine->setVisible(false);
        m_plot->replot(QCustomPlot::rpQueuedReplot);
    }
}

// ---- 右键拖拽区间选取 ----
void ScopeChart::onMousePress(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton
        && m_plot->axisRect()->rect().contains(event->pos())) {
        m_selecting = true;
        m_selectStartX = m_plot->xAxis->pixelToCoord(event->pos().x());
        onMouseLeave(); // 隐藏tooltip
    } else if (event->button() == Qt::LeftButton) {
        onMouseLeave(); // 左键拖拽时也隐藏tooltip
    }
}

void ScopeChart::onMouseRelease(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton && m_selecting) {
        m_selecting = false;
        double endX = m_plot->xAxis->pixelToCoord(event->pos().x());
        double tMin = qMin(m_selectStartX, endX);
        double tMax = qMax(m_selectStartX, endX);

        // 隐藏选取标记
        m_selectLineL->setVisible(false);
        m_selectLineR->setVisible(false);
        m_selectRect->setVisible(false);
        m_plot->replot(QCustomPlot::rpQueuedReplot);

        // 最小区间 0.01s，避免误点击
        if (tMax - tMin > 0.01) {
            emit rangeSelected(tMin, tMax);
        }
    }
}
