#include "rangeslider.h"
#include <QPainter>
#include <QMouseEvent>

RangeSlider::RangeSlider(QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(48);
    setMinimumWidth(180);
    setCursor(Qt::PointingHandCursor);
}

void RangeSlider::setRange(double min, double max)
{
    m_min = min;
    m_max = qMax(min + 0.01, max);
    m_lower = qBound(m_min, m_lower, m_max);
    m_upper = qBound(m_lower, m_upper, m_max);
    update();
}

void RangeSlider::setValues(double lower, double upper)
{
    m_lower = qBound(m_min, lower, m_max);
    m_upper = qBound(m_lower, upper, m_max);
    update();
}

int RangeSlider::valToX(double v) const
{
    if (m_max <= m_min) return PAD + HR;
    double ratio = (v - m_min) / (m_max - m_min);
    int usable = width() - 2 * (PAD + HR);
    return PAD + HR + int(ratio * usable);
}

double RangeSlider::xToVal(int x) const
{
    int usable = width() - 2 * (PAD + HR);
    if (usable <= 0) return m_min;
    double ratio = double(x - PAD - HR) / usable;
    ratio = qBound(0.0, ratio, 1.0);
    return m_min + ratio * (m_max - m_min);
}

static QString fmtTime(double t)
{
    if (t >= 100.0)
        return QString::number(t, 'f', 1);
    return QString::number(t, 'f', 2);
}

void RangeSlider::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int trackY = 28;       // 轨道垂直中心（上方留给时间标签）
    const int trackH = 4;
    const int x0 = PAD + HR;
    const int x1 = width() - PAD - HR;

    // ---- 轨道背景 ----
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(26, 48, 80));
    p.drawRoundedRect(x0, trackY - trackH / 2, x1 - x0, trackH, 2, 2);

    // ---- 刻度 ----
    {
        double span = m_max - m_min;
        if (span > 0) {
            // 自动选择刻度间距
            double rawStep = span / 8.0;
            double mag = 1.0;
            while (rawStep > 10.0) { rawStep /= 10.0; mag *= 10.0; }
            while (rawStep < 1.0)  { rawStep *= 10.0; mag /= 10.0; }
            double step;
            if      (rawStep <= 2.0) step = 2.0 * mag;
            else if (rawStep <= 5.0) step = 5.0 * mag;
            else                     step = 10.0 * mag;

            QFont tickFont("Consolas", 7);
            p.setFont(tickFont);
            p.setPen(QColor(80, 120, 150));

            double first = qCeil(m_min / step) * step;
            for (double tv = first; tv <= m_max; tv += step) {
                int tx = valToX(tv);
                // 小刻度线
                p.drawLine(tx, trackY + trackH / 2 + 1, tx, trackY + trackH / 2 + 5);
                // 刻度文字
                QString label = fmtTime(tv);
                QRect textRect(tx - 28, trackY + 7, 56, 12);
                p.drawText(textRect, Qt::AlignCenter, label);
            }
        }
    }

    // ---- 活动范围高亮 ----
    int lx = valToX(m_lower);
    int ux = valToX(m_upper);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 96, 140));
    if (ux > lx)
        p.drawRoundedRect(lx, trackY - trackH / 2, ux - lx, trackH, 2, 2);

    // ---- A端手柄 ----
    p.setBrush(QColor(0, 212, 255));
    p.setPen(QPen(QColor(0, 140, 180), 1.5));
    p.drawEllipse(QPoint(lx, trackY), HR, HR);

    // ---- B端手柄 ----
    p.drawEllipse(QPoint(ux, trackY), HR, HR);

    // ---- 手柄字母标签 ----
    p.setPen(QColor(10, 14, 20));
    QFont boldF("Consolas", 7, QFont::Bold);
    p.setFont(boldF);
    p.drawText(QRect(lx - HR, trackY - HR, 2 * HR, 2 * HR), Qt::AlignCenter, "A");
    p.drawText(QRect(ux - HR, trackY - HR, 2 * HR, 2 * HR), Qt::AlignCenter, "B");

    // ---- A/B 时间值标签（手柄上方） ----
    QFont timeFont("Consolas", 8, QFont::Bold);
    p.setFont(timeFont);

    QString aText = fmtTime(m_lower) + "s";
    QString bText = fmtTime(m_upper) + "s";

    // A标签
    p.setPen(QColor(0, 212, 255));
    QRect aRect(lx - 36, trackY - HR - 16, 72, 14);
    p.drawText(aRect, Qt::AlignCenter, aText);

    // B标签（避免与A重叠：如果太近则右移）
    p.setPen(QColor(0, 255, 136));
    QRect bRect(ux - 36, trackY - HR - 16, 72, 14);
    if (qAbs(ux - lx) < 60) {
        bRect.moveTop(trackY - HR - 2);  // 如果太近，B标签下移
    }
    p.drawText(bRect, Qt::AlignCenter, bText);
}

void RangeSlider::mousePressEvent(QMouseEvent *e)
{
    int mx = e->pos().x();
    int lx = valToX(m_lower);
    int ux = valToX(m_upper);
    int distL = qAbs(mx - lx);
    int distU = qAbs(mx - ux);

    if (distL <= distU && distL < HR * 3)
        m_active = 0;
    else if (distU < HR * 3)
        m_active = 1;
    else
        m_active = -1;
}

void RangeSlider::mouseMoveEvent(QMouseEvent *e)
{
    if (m_active < 0) return;

    double val = xToVal(e->pos().x());
    if (m_active == 0) {
        m_lower = qBound(m_min, val, m_upper - 0.01);
    } else {
        m_upper = qBound(m_lower + 0.01, val, m_max);
    }
    update();
    emit valuesChanged(m_lower, m_upper);
}

void RangeSlider::mouseReleaseEvent(QMouseEvent *)
{
    m_active = -1;
}
