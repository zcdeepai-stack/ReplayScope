#ifndef CURVEVIEWER_H
#define CURVEVIEWER_H

#include <QWidget>
#include <QVector>
#include <QStringList>

#include "csvparser.h"

class QTabBar;
class QStackedWidget;
class QLabel;
class QDoubleSpinBox;
class QSpinBox;
class QComboBox;
class ScopeChart;
class QPushButton;
class RangeSlider;
class QShowEvent;
class QHideEvent;

/// 解析结果结构统一放在 csvparser.h，此处保留旧名以免影响既有代码
using CsvData = CsvParseResult;

class CurveViewer : public QWidget
{
    Q_OBJECT

public:
    explicit CurveViewer(QWidget *parent = nullptr);

    void loadCsvFile();
    void loadCsvFromPath(const QString &path);
    const CsvData &csvData() const { return m_csvData; }

    void exportCurrentCharts();
    void setPlaybackTime(double timeSec);
    void clearPlaybackCursor();

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

signals:
    void csvLoaded(const QString &path, int rows, int cols);
    void viewerClosed();
    /// 显示/隐藏（QWidget 无 visibleChanged，由此信号协调 3D 叠加层 Z 序）
    void analysisVisibilityChanged(bool visible);

private:
    void initUi();
    QWidget *createConfigPanel();
    QWidget *createAttitudePage();
    QWidget *createJointPage();
    QWidget *createAlgorithmPage();
    QWidget *createPerformancePage();
    QWidget *createTemperaturePage();
    QWidget *createStatusPage();
    QWidget *createTimeControls();

    void initColumnMap();
    void buildColumnMapForTab(int tabIdx);
    void refreshCurrentTab();
    void updateConfigPanel();

    QTabBar        *m_tabBar = nullptr;
    QStackedWidget *m_stack  = nullptr;
    QPushButton    *m_loadBtn = nullptr;
    QLabel         *m_fileLabel = nullptr;

    QDoubleSpinBox *m_timeStart = nullptr;
    QDoubleSpinBox *m_windowSize = nullptr;
    QLabel         *m_totalTimeLabel = nullptr;
    RangeSlider    *m_rangeSlider = nullptr;

    QVector<QVector<ScopeChart*>> m_charts;

    // Tab-dependent config panel
    QStackedWidget *m_configStack = nullptr;

    // Attitude tab config (Tab 0)
    QComboBox *m_rpy1TypeCombo = nullptr;
    QComboBox *m_rpy1AxisCombo = nullptr;
    QComboBox *m_rpy2TypeCombo = nullptr;
    QComboBox *m_rpy2AxisCombo = nullptr;

    // Joint tab config (Tab 1)
    QComboBox *m_jointLegCombo = nullptr;
    QComboBox *m_jointJointCombo = nullptr;

    // Algorithm tab config (Tab 2)
    QSpinBox *m_scopeCol[3][2] = {};

    // Temperature tab config (Tab 4)
    QComboBox *m_tempJointCombo = nullptr;

    QVector<QLabel*> m_statusValues;

    CsvData m_csvData;
    // m_colMap[tabIdx][chartIdx] = list of CSV column indices (one per curve)
    QVector<QVector<QVector<int>>> m_colMap;

private slots:
    void onTabChanged(int index);
    void onTimeRangeChanged();
    void onRangeSliderChanged(double lower, double upper);
    void onConfigChanged();
    void onRangeSelected(double tMin, double tMax);
};

#endif // CURVEVIEWER_H
