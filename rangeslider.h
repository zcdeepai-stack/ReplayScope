#ifndef RANGESLIDER_H
#define RANGESLIDER_H

#include <QWidget>

// AB双端范围滑动条
class RangeSlider : public QWidget
{
    Q_OBJECT

public:
    explicit RangeSlider(QWidget *parent = nullptr);

    void setRange(double min, double max);
    void setValues(double lower, double upper);

    double lowerValue() const { return m_lower; }
    double upperValue() const { return m_upper; }

signals:
    void valuesChanged(double lower, double upper);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    int valToX(double v) const;
    double xToVal(int x) const;

    double m_min = 0, m_max = 100;
    double m_lower = 0, m_upper = 10;
    int m_active = -1;  // -1:无, 0:A端, 1:B端
    static constexpr int PAD = 8;
    static constexpr int HR = 7;  // handle radius
};

#endif // RANGESLIDER_H
