#ifndef LOGANALYSISWIDGET_H
#define LOGANALYSISWIDGET_H

#include <QWidget>
#include <QTreeWidget>
#include <QTableView>
#include <QAbstractTableModel>
#include <QCheckBox>
#include <QDateTimeEdit>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QTimer>
#include <QVector>
#include <QSet>
#include <QDateTime>
#include <QRegularExpression>

class QHBoxLayout;

// ======== 日志条目 ========
struct LogEntry {
    QDateTime timestamp;
    QString   level;
    QString   module;
    QString   message;
    QString   sourceFile;
    int       sourceLine = 0;
};

// ======== 日志表格 Model ========
class LogTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit LogTableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    /// 按值传入 + move：调用方传右值时可完全避免一次全量拷贝
    void setEntries(QVector<LogEntry> entries);
    void clear();
    const QVector<LogEntry> &entries() const { return m_entries; }

private:
    QVector<LogEntry> m_entries;
};

// ======== 日志分析主界面 ========
class LogAnalysisWidget : public QWidget
{
    Q_OBJECT
public:
    explicit LogAnalysisWidget(QWidget *parent = nullptr);
    void addFiles(const QStringList &filePaths);

private slots:
    void onAddFiles();
    void onAddFolder();
    void onDeleteSelected();
    void onClearFileList();

    void onAnalyze();
    void onExportResult();
    void onStatistics();
    void onClearResult();

    // 播放控制
    void onPlayPause();
    void onPrevFrame();
    void onNextFrame();
    void onSpeedChanged(int index);
    void onSliderMoved(int value);
    void onAddCustomLevel();
    void onSearchNext();
    void onSearchPrev();
    void onApplyFilter();
    void onPlaybackTick();

private:
    void setupUi();
    QVector<LogEntry> parseFile(const QString &filePath);
    QVector<LogEntry> mergeEntries(QVector<QVector<LogEntry>> sources);
    QVector<LogEntry> applyFilter(const QVector<LogEntry> &entries);
    void updatePlaybackLabels();

    // 合并后的全量数据（筛选基础）
    QVector<LogEntry> m_mergedEntries;

    // UI: 文件管理
    QTreeWidget   *m_fileList;
    QPushButton  *m_btnAddFile;
    QPushButton  *m_btnAddFolder;
    QPushButton  *m_btnDeleteFile;
    QPushButton  *m_btnClearFiles;

    // UI: 筛选
    QCheckBox    *m_chkInfo;
    QCheckBox    *m_chkError;
    QCheckBox    *m_chkWarn;
    QCheckBox    *m_chkDebug;
    QLineEdit    *m_customLevel;
    QPushButton  *m_btnAddLevel;
    QHBoxLayout  *m_levelLayout;
    QVector<QCheckBox*> m_customLevelChecks;
    QDateTimeEdit *m_startTime;
    QDateTimeEdit *m_endTime;
    QLineEdit    *m_keywordEdit;
    QComboBox    *m_moduleCombo;
    QPushButton  *m_btnAnalyze;
    QPushButton  *m_btnExport;
    QPushButton  *m_btnStats;
    QPushButton  *m_btnClearResult;

    // UI: 结果
    QTableView   *m_tableView;
    LogTableModel *m_model;

    // UI: 播放
    QPushButton  *m_btnPlay;
    QPushButton  *m_btnPrev;
    QPushButton  *m_btnNext;
    QComboBox    *m_speedCombo;
    QSlider      *m_slider;
    QLabel       *m_lblCurrent;
    QLabel       *m_lblTotal;

    // UI: 搜索
    QLineEdit    *m_searchEdit;
    QPushButton  *m_btnSearchPrev;
    QPushButton  *m_btnSearchNext;
    QLabel       *m_lblSearchResult;
    int           m_searchIndex = -1;

    // 播放状态
    QTimer       *m_playTimer;
    int           m_playIndex = 0;
    bool          m_playing = false;
    double        m_speedFactor = 1.0;

    // 解析正则
    QRegularExpression m_logRegex;
    // 已提取的模块名集合
    QSet<QString> m_knownModules;
    int m_parseFailedTotal = 0;
};

#endif // LOGANALYSISWIDGET_H
