#ifndef CUSTOMANALYZER_H
#define CUSTOMANALYZER_H

#include <QWidget>
#include <QVector>
#include <QStringList>

#include "csvparser.h"

class QSplitter;
class QListWidget;
class QSpinBox;
class QDoubleSpinBox;
class QComboBox;
class QLabel;
class QPushButton;
class ScopeChart;
class RangeSlider;
class QVBoxLayout;
class QScrollArea;
class QListWidgetItem;
class QTreeWidget;
class QTreeWidgetItem;

// 波形分析器的 CSV 数据（与数据分析页共用同一套解析结果结构）
using WaveCsvData = CsvParseResult;

// 单个窗口配置信息
struct WindowConfig {
    QWidget      *container = nullptr;
    QLabel       *titleLabel = nullptr;
    QListWidget  *columnList = nullptr;
    QPushButton  *addBtn     = nullptr;
    QPushButton  *clearBtn   = nullptr;   // 清空此窗口列
    QPushButton  *removeBtn  = nullptr;
};

class CustomAnalyzer : public QWidget
{
    Q_OBJECT

public:
    explicit CustomAnalyzer(QWidget *parent = nullptr);

private:
    void initUi();
    QWidget *createSidePanel();
    QWidget *createTimeControls();

    void loadCsvFile();
    void loadCsvFromPath(const QString &path);

    void rebuildCharts();
    void refreshCharts();

    // 窗口动态管理
    void addWindow();
    void removeWindow(int idx);
    void renumberWindows();
    void addSelectedColumns(int windowIdx);

    // 配置管理
    void saveConfig();
    void loadConfig(const QString &path);
    void deleteConfig();
    void scanConfigs();

    void populateColumnList();
    QString configDir() const;

private slots:
    void onTimeRangeChanged();
    void onRangeSliderChanged(double lower, double upper);
    void onRangeSelected(double tMin, double tMax);

private:
    // 左侧面板
    QScrollArea    *m_sideScroll = nullptr;
    QListWidget    *m_colList = nullptr;          // 可用数据列
    QWidget        *m_windowConfigArea = nullptr;
    QVBoxLayout    *m_windowConfigLay = nullptr;
    QVector<WindowConfig> m_windowConfigs;         // 动态窗口配置
    QPushButton    *m_addWindowBtn = nullptr;       // 添加窗口按钮
    QPushButton    *m_drawBtn = nullptr;

    // 配置列表
    QTreeWidget    *m_configTree = nullptr;
    QLabel         *m_fileLabel = nullptr;
    QLabel         *m_totalTimeLabel = nullptr;

    // 底部时间控制
    QDoubleSpinBox *m_timeStart = nullptr;
    QDoubleSpinBox *m_windowSize = nullptr;
    RangeSlider    *m_rangeSlider = nullptr;

    // 图表区
    QSplitter      *m_chartSplitter = nullptr;
    QVector<ScopeChart*> m_charts;

    // 数据
    WaveCsvData m_csvData;
    QString m_csvPath;  // 当前 CSV 文件路径
};

#endif // CUSTOMANALYZER_H
