#include "loganalysiswidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QProgressDialog>
#include <QApplication>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDir>
#include <QSet>
#include <QElapsedTimer>
#include <algorithm>

static const char *ANALYSIS_STYLE = R"(
    QWidget { background: #0d1117; color: #c9d1d9; font-size: 12px; }
    QGroupBox { border: 1px solid #30363d; border-radius: 6px; margin-top: 14px;
                padding-top: 22px; font-weight: bold; }
    QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 3px 14px;
                       background: #1f6feb; color: #ffffff; border-radius: 4px;
                       font-size: 13px; font-weight: bold; }
    QPushButton { background: #21262d; border: 1px solid #30363d; border-radius: 4px;
                  padding: 5px 12px; color: #c9d1d9; }
    QPushButton:hover { background: #30363d; border-color: #8b949e; }
    QPushButton:disabled { color: #484f58; }
    QTableView, QListWidget { background: #0d1117; border: 1px solid #21262d;
                              border-radius: 4px; alternate-background-color: #161b22; }
    QHeaderView::section { background: #161b22; border: none; padding: 4px; color: #8b949e; }
    QLineEdit, QComboBox, QDateTimeEdit { background: #0d1117; border: 1px solid #30363d;
                           border-radius: 3px; padding: 4px; color: #c9d1d9; }
    QSlider::groove:horizontal { background: #21262d; height: 6px; border-radius: 3px; }
    QSlider::handle:horizontal { background: #58a6ff; width: 14px; margin: -4px 0;
                                 border-radius: 7px; }
    QCheckBox { spacing: 6px; color: #c9d1d9; font-size: 13px; }
    QCheckBox::indicator { width: 16px; height: 16px; border: 2px solid #30363d;
                           border-radius: 3px; background: #161b22; }
    QCheckBox::indicator:hover { border-color: #58a6ff; }
    QCheckBox::indicator:checked { background: #238636; border-color: #3fb950; }
)";

// ====================== LogTableModel ======================

LogTableModel::LogTableModel(QObject *parent) : QAbstractTableModel(parent) {}

int LogTableModel::rowCount(const QModelIndex &) const { return m_entries.size(); }
int LogTableModel::columnCount(const QModelIndex &) const { return 6; }

QVariant LogTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};

    const LogEntry &e = m_entries[index.row()];

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case 0: return index.row() + 1;
        case 1: return e.timestamp.toString("yyyy-MM-dd HH:mm:ss.zzz");
        case 2: return e.level;
        case 3: return e.module;
        case 4: return e.sourceFile;
        case 5: return e.message;
        }
    } else if (role == Qt::ForegroundRole) {
        if (e.level.contains("ERROR", Qt::CaseInsensitive))
            return QColor("#f85149");
        if (e.level.contains("WARN", Qt::CaseInsensitive))
            return QColor("#d29922");
        if (index.column() == 2)
            return QColor("#3fb950");
        if (index.column() == 4)
            return QColor("#8b949e");
    } else if (role == Qt::BackgroundRole) {
        if (e.level.contains("ERROR", Qt::CaseInsensitive))
            return QColor(248, 81, 73, 20);
    }
    return {};
}

QVariant LogTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
        case 0: return "#";
        case 1: return "时间戳";
        case 2: return "等级";
        case 3: return "进程/模块";
        case 4: return "来源文件";
        case 5: return "日志消息";
        }
    }
    return {};
}

void LogTableModel::setEntries(QVector<LogEntry> entries)
{
    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();
}

void LogTableModel::clear()
{
    beginResetModel();
    m_entries.clear();
    endResetModel();
}

// ====================== LogAnalysisWidget ======================

static QString formatFileSize(qint64 bytes)
{
    if (bytes < 1024)
        return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024LL * 1024 * 1024)
        return QString("%1 MB").arg(bytes / (1024.0 * 1024), 0, 'f', 1);
    return QString("%1 GB").arg(bytes / (1024.0 * 1024 * 1024), 0, 'f', 2);
}

LogAnalysisWidget::LogAnalysisWidget(QWidget *parent)
    : QWidget(parent)
    , m_playTimer(new QTimer(this))
    , m_logRegex(R"(\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\.\d{3}\]\s*\[(\w+)\s*\]\s*\[([^\]]+)\]\s*(.*))")
{
    // 提前触发 PCRE JIT 编译：解析是逐行匹配，首次使用才优化会让前若干万行走解释执行
    m_logRegex.optimize();
    setupUi();
    connect(m_playTimer, &QTimer::timeout, this, &LogAnalysisWidget::onPlaybackTick);
}

void LogAnalysisWidget::setupUi()
{
    setStyleSheet(ANALYSIS_STYLE);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->setSpacing(4);

    auto *splitter = new QSplitter(Qt::Vertical);

    // ======== 顶部: 文件管理 + 筛选 ========
    auto *topPanel = new QWidget;
    auto *topLay = new QHBoxLayout(topPanel);
    topLay->setContentsMargins(0, 0, 0, 0);

    // -- 文件列表 --
    auto *fileGroup = new QGroupBox("日志文件");
    auto *fileLay = new QVBoxLayout(fileGroup);
    m_fileList = new QTreeWidget;
    m_fileList->setHeaderLabels({"文件名", "修改时间", "大小"});
    m_fileList->setColumnCount(3);
    m_fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fileList->setRootIsDecorated(false);
    m_fileList->setAlternatingRowColors(true);
    m_fileList->header()->setStretchLastSection(false);
    m_fileList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_fileList->setColumnWidth(1, 150);
    m_fileList->setColumnWidth(2, 80);
    fileLay->addWidget(m_fileList);

    auto *fileBtnRow = new QHBoxLayout;
    m_btnAddFile = new QPushButton("➕ 添加文件");
    m_btnAddFolder = new QPushButton("➕ 添加文件夹");
    m_btnDeleteFile = new QPushButton("− 删除选中");
    m_btnDeleteFile->setStyleSheet(
        "QPushButton { color: #f0883e; }"
        "QPushButton:hover { background: #f0883e; color: white; }");
    m_btnClearFiles = new QPushButton("✗ 清空列表");
    m_btnClearFiles->setStyleSheet(
        "QPushButton { color: #f85149; }"
        "QPushButton:hover { background: #f85149; color: white; }");
    fileBtnRow->addWidget(m_btnAddFile);
    fileBtnRow->addWidget(m_btnAddFolder);
    fileBtnRow->addWidget(m_btnDeleteFile);
    fileBtnRow->addWidget(m_btnClearFiles);
    fileLay->addLayout(fileBtnRow);

    connect(m_btnAddFile, &QPushButton::clicked, this, &LogAnalysisWidget::onAddFiles);
    connect(m_btnAddFolder, &QPushButton::clicked, this, &LogAnalysisWidget::onAddFolder);
    connect(m_btnDeleteFile, &QPushButton::clicked, this, &LogAnalysisWidget::onDeleteSelected);
    connect(m_btnClearFiles, &QPushButton::clicked, this, &LogAnalysisWidget::onClearFileList);

    topLay->addWidget(fileGroup, 1);

    // -- 筛选区 --
    auto *filterGroup = new QGroupBox("筛选条件");
    auto *filterLay = new QVBoxLayout(filterGroup);

    // 等级
    m_levelLayout = new QHBoxLayout;
    m_levelLayout->addWidget(new QLabel("等级:"));
    m_chkInfo = new QCheckBox("INFO"); m_chkInfo->setChecked(true);
    m_chkError = new QCheckBox("ERROR"); m_chkError->setChecked(true);
    m_chkWarn = new QCheckBox("WARN"); m_chkWarn->setChecked(true);
    m_chkDebug = new QCheckBox("DEBUG"); m_chkDebug->setChecked(true);
    m_levelLayout->addWidget(m_chkInfo);
    m_levelLayout->addWidget(m_chkError);
    m_levelLayout->addWidget(m_chkWarn);
    m_levelLayout->addWidget(m_chkDebug);
    m_levelLayout->addSpacing(12);
    m_customLevel = new QLineEdit;
    m_customLevel->setPlaceholderText("输入等级名");
    m_customLevel->setFixedWidth(90);
    m_btnAddLevel = new QPushButton("+ 添加");
    m_btnAddLevel->setFixedWidth(60);
    m_levelLayout->addWidget(m_customLevel);
    m_levelLayout->addWidget(m_btnAddLevel);
    m_levelLayout->addStretch();
    filterLay->addLayout(m_levelLayout);

    connect(m_btnAddLevel, &QPushButton::clicked, this, &LogAnalysisWidget::onAddCustomLevel);
    connect(m_customLevel, &QLineEdit::returnPressed, this, &LogAnalysisWidget::onAddCustomLevel);

    // 时间
    auto *timeRow = new QHBoxLayout;
    timeRow->addWidget(new QLabel("时间范围:"));
    m_startTime = new QDateTimeEdit;
    m_startTime->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
    m_startTime->setCalendarPopup(true);
    m_endTime = new QDateTimeEdit;
    m_endTime->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
    m_endTime->setCalendarPopup(true);
    m_endTime->setDateTime(QDateTime::currentDateTime());
    m_startTime->setDateTime(QDateTime::currentDateTime().addDays(-1));
    timeRow->addWidget(m_startTime);
    timeRow->addWidget(new QLabel("~"));
    timeRow->addWidget(m_endTime);
    timeRow->addStretch();
    filterLay->addLayout(timeRow);

    // 关键字 + 进程名
    auto *kwRow = new QHBoxLayout;
    kwRow->addWidget(new QLabel("关键字:"));
    m_keywordEdit = new QLineEdit;
    m_keywordEdit->setPlaceholderText("支持正则表达式，回车应用");
    kwRow->addWidget(m_keywordEdit, 1);
    kwRow->addWidget(new QLabel("模块/进程:"));
    m_moduleCombo = new QComboBox;
    m_moduleCombo->setEditable(true);
    m_moduleCombo->addItem("全部");
    m_moduleCombo->setMinimumWidth(120);
    kwRow->addWidget(m_moduleCombo);
    filterLay->addLayout(kwRow);

    // 操作按钮
    auto *actionRow = new QHBoxLayout;
    m_btnAnalyze = new QPushButton("⭐ 合并分析");
    m_btnAnalyze->setStyleSheet(
        "QPushButton { background: #238636; color: white; font-weight: bold; "
        "  border-radius: 4px; padding: 6px 14px; border: none; }"
        "QPushButton:hover { background: #2ea043; }");
    m_btnExport = new QPushButton("↓ 导出结果");
    m_btnExport->setStyleSheet(
        "QPushButton { background: #1f6feb; color: white; font-weight: bold; "
        "  border-radius: 4px; padding: 6px 14px; border: none; }"
        "QPushButton:hover { background: #388bfd; }");
    m_btnStats = new QPushButton("ℹ 统计信息");
    m_btnStats->setStyleSheet(
        "QPushButton { background: #6e40c9; color: white; font-weight: bold; "
        "  border-radius: 4px; padding: 6px 14px; border: none; }"
        "QPushButton:hover { background: #8957e5; }");
    m_btnClearResult = new QPushButton("✗ 清空结果");
    m_btnClearResult->setStyleSheet(
        "QPushButton { background: #21262d; color: #f85149; font-weight: bold; "
        "  border-radius: 4px; padding: 6px 14px; border: 1px solid #da3633; }"
        "QPushButton:hover { background: #da3633; color: white; }");
    actionRow->addWidget(m_btnAnalyze);
    actionRow->addWidget(m_btnExport);
    actionRow->addWidget(m_btnStats);
    actionRow->addWidget(m_btnClearResult);
    actionRow->addStretch();
    filterLay->addLayout(actionRow);

    connect(m_btnAnalyze, &QPushButton::clicked, this, &LogAnalysisWidget::onAnalyze);
    connect(m_btnExport, &QPushButton::clicked, this, &LogAnalysisWidget::onExportResult);
    connect(m_btnStats, &QPushButton::clicked, this, &LogAnalysisWidget::onStatistics);
    connect(m_btnClearResult, &QPushButton::clicked, this, &LogAnalysisWidget::onClearResult);

    // 筛选控件变更 → 自动重新筛选
    connect(m_chkInfo, &QCheckBox::toggled, this, &LogAnalysisWidget::onApplyFilter);
    connect(m_chkError, &QCheckBox::toggled, this, &LogAnalysisWidget::onApplyFilter);
    connect(m_chkWarn, &QCheckBox::toggled, this, &LogAnalysisWidget::onApplyFilter);
    connect(m_chkDebug, &QCheckBox::toggled, this, &LogAnalysisWidget::onApplyFilter);
    connect(m_startTime, &QDateTimeEdit::dateTimeChanged, this, &LogAnalysisWidget::onApplyFilter);
    connect(m_endTime, &QDateTimeEdit::dateTimeChanged, this, &LogAnalysisWidget::onApplyFilter);
    connect(m_keywordEdit, &QLineEdit::returnPressed, this, &LogAnalysisWidget::onApplyFilter);
    connect(m_moduleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LogAnalysisWidget::onApplyFilter);

    topLay->addWidget(filterGroup, 2);

    splitter->addWidget(topPanel);

    // ======== 底部: 结果表格 + 播放栏 ========
    auto *bottomPanel = new QWidget;
    auto *bottomLay = new QVBoxLayout(bottomPanel);
    bottomLay->setContentsMargins(0, 0, 0, 0);

    m_model = new LogTableModel(this);
    m_tableView = new QTableView;
    m_tableView->setModel(m_model);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->verticalHeader()->setVisible(false);
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->setColumnWidth(0, 50);
    m_tableView->setColumnWidth(1, 180);
    m_tableView->setColumnWidth(2, 60);
    m_tableView->setColumnWidth(3, 120);
    m_tableView->setColumnWidth(4, 120);
    bottomLay->addWidget(m_tableView, 1);

    // 搜索栏
    auto *searchBar = new QWidget;
    auto *searchLay = new QHBoxLayout(searchBar);
    searchLay->setContentsMargins(4, 2, 4, 2);
    searchLay->addWidget(new QLabel("搜索:"));
    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText("输入关键字搜索日志，回车跳转...");
    m_btnSearchPrev = new QPushButton("▲ 上一个");
    m_btnSearchNext = new QPushButton("▼ 下一个");
    for (auto *btn : {m_btnSearchPrev, m_btnSearchNext}) {
        btn->setFixedHeight(26);
        btn->setMinimumWidth(70);
        btn->setStyleSheet(
            "QPushButton { background: #21262d; border: 1px solid #30363d; border-radius: 3px;"
            "  color: #c9d1d9; padding: 2px 8px; }"
            "QPushButton:hover { background: #30363d; border-color: #8b949e; }");
    }
    m_lblSearchResult = new QLabel("");
    m_lblSearchResult->setMinimumWidth(100);
    searchLay->addWidget(m_searchEdit, 1);
    searchLay->addWidget(m_btnSearchPrev);
    searchLay->addWidget(m_btnSearchNext);
    searchLay->addWidget(m_lblSearchResult);

    connect(m_searchEdit, &QLineEdit::returnPressed, this, &LogAnalysisWidget::onSearchNext);
    connect(m_btnSearchNext, &QPushButton::clicked, this, &LogAnalysisWidget::onSearchNext);
    connect(m_btnSearchPrev, &QPushButton::clicked, this, &LogAnalysisWidget::onSearchPrev);

    bottomLay->addWidget(searchBar);

    // 播放控制栏
    auto *playBar = new QWidget;
    playBar->setFixedHeight(42);
    auto *playLay = new QHBoxLayout(playBar);
    playLay->setContentsMargins(4, 2, 4, 2);

    m_btnPrev = new QPushButton("◀◀ 前帧");
    m_btnPlay = new QPushButton("▶ 播放");
    m_btnNext = new QPushButton("后帧 ▶▶");
    for (auto *btn : {m_btnPrev, m_btnPlay, m_btnNext}) {
        btn->setFixedHeight(30);
        btn->setMinimumWidth(70);
        btn->setStyleSheet(
            "QPushButton { background: #21262d; border: 1px solid #58a6ff; border-radius: 4px;"
            "  color: #58a6ff; font-weight: bold; padding: 2px 8px; }"
            "QPushButton:hover { background: #30363d; }");
    }

    m_speedCombo = new QComboBox;
    m_speedCombo->addItems({"0.5x", "1x", "2x", "5x", "10x", "最快"});
    m_speedCombo->setCurrentIndex(1);
    m_speedCombo->setFixedWidth(60);

    m_slider = new QSlider(Qt::Horizontal);
    m_slider->setRange(0, 0);

    m_lblCurrent = new QLabel("00:00:00");
    m_lblTotal = new QLabel("00:00:00");
    m_lblCurrent->setFixedWidth(65);
    m_lblTotal->setFixedWidth(65);

    playLay->addWidget(m_btnPrev);
    playLay->addWidget(m_btnPlay);
    playLay->addWidget(m_btnNext);
    playLay->addSpacing(8);
    playLay->addWidget(m_speedCombo);
    playLay->addSpacing(8);
    playLay->addWidget(m_lblCurrent);
    playLay->addWidget(m_slider, 1);
    playLay->addWidget(m_lblTotal);

    connect(m_btnPlay, &QPushButton::clicked, this, &LogAnalysisWidget::onPlayPause);
    connect(m_btnPrev, &QPushButton::clicked, this, &LogAnalysisWidget::onPrevFrame);
    connect(m_btnNext, &QPushButton::clicked, this, &LogAnalysisWidget::onNextFrame);
    connect(m_speedCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LogAnalysisWidget::onSpeedChanged);
    connect(m_slider, &QSlider::sliderMoved, this, &LogAnalysisWidget::onSliderMoved);

    bottomLay->addWidget(playBar);

    splitter->addWidget(bottomPanel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    mainLayout->addWidget(splitter);
}

// ======== 文件管理 ========

void LogAnalysisWidget::onAddFiles()
{
    QStringList files = QFileDialog::getOpenFileNames(
        this, "选择日志文件", QString(),
        "日志文件 (*.log *.txt);;所有文件 (*)");
    for (const auto &f : files) {
        QFileInfo fi(f);
        auto *item = new QTreeWidgetItem(m_fileList);
        item->setText(0, fi.fileName());
        item->setText(1, fi.lastModified().toString("yyyy-MM-dd HH:mm:ss"));
        item->setText(2, formatFileSize(fi.size()));
        item->setData(0, Qt::UserRole, f);
        item->setToolTip(0, f);
    }
}

void LogAnalysisWidget::onAddFolder()
{
    QString dir = QFileDialog::getExistingDirectory(this, "选择日志目录");
    if (dir.isEmpty()) return;
    QDir d(dir);
    QStringList filters = {"*.log", "*.txt"};
    for (const auto &fi : d.entryInfoList(filters, QDir::Files)) {
        auto *item = new QTreeWidgetItem(m_fileList);
        item->setText(0, fi.fileName());
        item->setText(1, fi.lastModified().toString("yyyy-MM-dd HH:mm:ss"));
        item->setText(2, formatFileSize(fi.size()));
        item->setData(0, Qt::UserRole, fi.absoluteFilePath());
        item->setToolTip(0, fi.absoluteFilePath());
    }
}

void LogAnalysisWidget::onDeleteSelected()
{
    auto items = m_fileList->selectedItems();
    for (auto *it : items)
        delete it;
}

void LogAnalysisWidget::onClearFileList()
{
    m_fileList->clear();
}

void LogAnalysisWidget::addFiles(const QStringList &filePaths)
{
    for (const auto &f : filePaths) {
        QFileInfo fi(f);
        auto *item = new QTreeWidgetItem(m_fileList);
        item->setText(0, fi.fileName());
        item->setText(1, fi.lastModified().toString("yyyy-MM-dd HH:mm:ss"));
        item->setText(2, formatFileSize(fi.size()));
        item->setData(0, Qt::UserRole, f);
        item->setToolTip(0, f);
    }
}

// ======== 解析引擎 ========

QVector<LogEntry> LogAnalysisWidget::parseFile(const QString &filePath)
{
    QVector<LogEntry> entries;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return entries;

    QString fileName = QFileInfo(filePath).fileName();
    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);

    int lineNum = 0;
    int failedLines = 0;
    while (!in.atEnd()) {
        QString line = in.readLine();
        ++lineNum;
        if (line.trimmed().isEmpty()) continue;

        // 每5000行处理一次事件，保持UI响应
        if (lineNum % 5000 == 0)
            QApplication::processEvents();

        auto match = m_logRegex.match(line);
        if (match.hasMatch()) {
            LogEntry e;
            e.timestamp = QDateTime::fromString(match.captured(1), "yyyy-MM-dd HH:mm:ss.zzz");
            e.level = match.captured(2).trimmed();
            e.module = match.captured(3).trimmed();
            e.message = match.captured(4).trimmed();
            e.sourceFile = fileName;
            e.sourceLine = lineNum;

            entries.append(e);
            m_knownModules.insert(e.module);
        } else {
            failedLines++;
        }
    }
    m_parseFailedTotal += failedLines;
    return entries;
}

QVector<LogEntry> LogAnalysisWidget::mergeEntries(QVector<QVector<LogEntry>> sources)
{
    QVector<LogEntry> merged;
    qsizetype totalSize = 0;
    for (const auto &s : sources) totalSize += s.size();
    merged.reserve(totalSize);

    // 全部合并后排序（文件数量少时足够高效）。
    // 逐条 move 而非拷贝：LogEntry 含 4 个 QString，百万级条目下拷贝代价可观。
    for (auto &s : sources) {
        if (merged.isEmpty() && s.size() == totalSize) {
            merged = std::move(s);
        } else {
            for (auto &e : s)
                merged.append(std::move(e));
        }
        s.clear();
    }

    std::sort(merged.begin(), merged.end(), [](const LogEntry &a, const LogEntry &b) {
        return a.timestamp < b.timestamp;
    });
    return merged;
}

QVector<LogEntry> LogAnalysisWidget::applyFilter(const QVector<LogEntry> &entries)
{
    QSet<QString> allowedLevels;
    if (m_chkInfo->isChecked()) allowedLevels.insert("INFO");
    if (m_chkError->isChecked()) allowedLevels.insert("ERROR");
    if (m_chkWarn->isChecked()) allowedLevels.insert("WARN");
    if (m_chkDebug->isChecked()) allowedLevels.insert("DEBUG");
    // 自定义等级复选框
    for (auto *cb : m_customLevelChecks) {
        if (cb->isChecked())
            allowedLevels.insert(cb->text().toUpper());
    }
    // 输入框中未添加的等级也纳入
    QString custom = m_customLevel->text().trimmed();
    if (!custom.isEmpty()) allowedLevels.insert(custom.toUpper());

    QDateTime start = m_startTime->dateTime();
    QDateTime end = m_endTime->dateTime();

    QString keyword = m_keywordEdit->text().trimmed();
    QRegularExpression kwRegex;
    if (!keyword.isEmpty())
        kwRegex = QRegularExpression(keyword, QRegularExpression::CaseInsensitiveOption);

    QString moduleFilter = m_moduleCombo->currentText().trimmed();
    bool filterModule = (moduleFilter != "全部" && !moduleFilter.isEmpty());

    QVector<LogEntry> result;
    result.reserve(entries.size());

    for (const auto &e : entries) {
        // 等级过滤
        if (!allowedLevels.contains(e.level.toUpper()))
            continue;
        // 时间过滤
        if (e.timestamp < start || e.timestamp > end)
            continue;
        // 关键字过滤
        if (kwRegex.isValid() && !keyword.isEmpty()) {
            if (!kwRegex.match(e.message).hasMatch() &&
                !kwRegex.match(e.module).hasMatch())
                continue;
        }
        // 模块过滤
        if (filterModule && e.module != moduleFilter)
            continue;

        result.append(e);
    }
    return result;
}

// ======== 操作按钮 ========

void LogAnalysisWidget::onAnalyze()
{
    if (m_fileList->topLevelItemCount() == 0) {
        QMessageBox::warning(this, "提示", "请先添加日志文件");
        return;
    }

    QElapsedTimer timer;
    timer.start();

    QProgressDialog progress("正在解析日志文件...", "取消", 0, m_fileList->topLevelItemCount(), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setMinimumWidth(400);
    progress.setStyleSheet(
        "QProgressDialog { background: #161b22; }"
        "QLabel { color: #c9d1d9; font-size: 13px; }"
        "QProgressBar { background: #21262d; border: 1px solid #30363d; border-radius: 3px; "
        "  text-align: center; color: #c9d1d9; min-height: 18px; }"
        "QProgressBar::chunk { background: #238636; border-radius: 2px; }"
        "QPushButton { background: #21262d; border: 1px solid #30363d; color: #c9d1d9; "
        "  border-radius: 3px; padding: 4px 16px; }"
        "QPushButton:hover { background: #30363d; }");

    // 实时更新计时器
    int currentFileIdx = 0;
    int totalParsed = 0;
    QTimer liveTimer;
    liveTimer.setInterval(200);
    connect(&liveTimer, &QTimer::timeout, this, [&]() {
        progress.setLabelText(
            QString("正在解析 (%1/%2): %3\n"
                    "已解析: %4 条  |  已用时: %5s")
                .arg(currentFileIdx + 1).arg(m_fileList->topLevelItemCount())
                .arg(currentFileIdx < m_fileList->topLevelItemCount() ? m_fileList->topLevelItem(currentFileIdx)->text(0) : "")
                .arg(totalParsed)
                .arg(timer.elapsed() / 1000.0, 0, 'f', 1));
    });
    liveTimer.start();

    QVector<QVector<LogEntry>> allEntries;
    m_knownModules.clear();
    m_parseFailedTotal = 0;

    for (int i = 0; i < m_fileList->topLevelItemCount(); ++i) {
        if (progress.wasCanceled()) break;
        currentFileIdx = i;
        progress.setValue(i);
        QApplication::processEvents();

        QString path = m_fileList->topLevelItem(i)->data(0, Qt::UserRole).toString();
        auto entries = parseFile(path);
        totalParsed += entries.size();
        allEntries.append(std::move(entries));
    }
    progress.setValue(m_fileList->topLevelItemCount());
    liveTimer.stop();

    if (progress.wasCanceled()) return;

    // 更新模块下拉框
    m_moduleCombo->clear();
    m_moduleCombo->addItem("全部");
    QStringList moduleList(m_knownModules.begin(), m_knownModules.end());
    moduleList.sort();
    m_moduleCombo->addItems(moduleList);

    // 合并
    progress.setLabelText(QString("正在合并排序 %1 条日志...\n已用时: %2s")
                          .arg(totalParsed)
                          .arg(timer.elapsed() / 1000.0, 0, 'f', 1));
    progress.setRange(0, 0);
    progress.setCancelButton(nullptr);
    QApplication::processEvents();

    // move 进成员，避免又一次全量拷贝（原来这里 merged 和 m_mergedEntries 各占一份）
    m_mergedEntries = mergeEntries(std::move(allEntries));
    const qsizetype mergedCount = m_mergedEntries.size();

    // 更新时间范围（屏蔽信号防止触发重复筛选）
    if (!m_mergedEntries.isEmpty()) {
        m_startTime->blockSignals(true);
        m_endTime->blockSignals(true);
        m_startTime->setDateTime(m_mergedEntries.first().timestamp);
        m_endTime->setDateTime(m_mergedEntries.last().timestamp);
        m_startTime->blockSignals(false);
        m_endTime->blockSignals(false);
    }

    progress.close();

    // 应用筛选并显示
    onApplyFilter();

    double elapsed = timer.elapsed() / 1000.0;
    QString failInfo = m_parseFailedTotal > 0
        ? QString("\n未识别行: %1 条").arg(m_parseFailedTotal) : "";
    QMessageBox::information(this, "分析完成",
                             QString("共解析 %1 条日志，筛选后 %2 条\n"
                                     "来自 %3 个文件  |  耗时: %4 秒%5")
                                 .arg(mergedCount).arg(m_model->rowCount())
                                 .arg(m_fileList->topLevelItemCount())
                                 .arg(elapsed, 0, 'f', 1)
                                 .arg(failInfo));
}

void LogAnalysisWidget::onExportResult()
{
    const auto &entries = m_model->entries();
    if (entries.isEmpty()) {
        QMessageBox::warning(this, "提示", "没有可导出的结果");
        return;
    }

    QString path = QFileDialog::getSaveFileName(this, "导出合并日志",
                                                QString(), "日志文件 (*.log);;文本文件 (*.txt)");
    if (path.isEmpty()) return;

    QElapsedTimer exportTimer;
    exportTimer.start();

    QProgressDialog progress("正在导出...", "取消", 0, entries.size(), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumWidth(380);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "错误", "无法创建文件: " + path);
        return;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);

    for (int i = 0; i < entries.size(); ++i) {
        if (progress.wasCanceled()) break;
        if (i % 1000 == 0) {
            progress.setLabelText(QString("正在导出: %1/%2 条\n目标: %3\n已用时: %4s")
                                  .arg(i).arg(entries.size()).arg(QFileInfo(path).fileName())
                                  .arg(exportTimer.elapsed() / 1000.0, 0, 'f', 1));
            progress.setValue(i);
            QApplication::processEvents();
        }
        const LogEntry &e = entries[i];
        out << "[" << e.timestamp.toString("yyyy-MM-dd HH:mm:ss.zzz") << "] "
            << "[" << e.level.leftJustified(6) << "] "
            << "[" << e.module << "] "
            << "[" << e.sourceFile << "] "
            << e.message << "\n";
    }
    progress.setValue(entries.size());
    double exportElapsed = exportTimer.elapsed() / 1000.0;
    QMessageBox::information(this, "导出完成",
                             QString("已导出 %1 条日志\n路径: %2\n耗时: %3 秒")
                                 .arg(entries.size()).arg(path)
                                 .arg(exportElapsed, 0, 'f', 1));
}

void LogAnalysisWidget::onStatistics()
{
    const auto &entries = m_model->entries();
    if (entries.isEmpty()) {
        QMessageBox::warning(this, "提示", "没有数据");
        return;
    }

    // 统计
    QMap<QString, int> levelCount;
    QMap<QString, int> moduleCount;
    for (const auto &e : entries) {
        levelCount[e.level]++;
        moduleCount[e.module]++;
    }

    QString stats;
    stats += QString("总条目数: %1\n\n").arg(entries.size());

    stats += "=== 按等级统计 ===\n";
    for (auto it = levelCount.begin(); it != levelCount.end(); ++it)
        stats += QString("  %1: %2 条\n").arg(it.key()).arg(it.value());

    stats += "\n=== 按进程/模块统计 (Top 20) ===\n";
    QVector<QPair<QString, int>> moduleSorted;
    for (auto it = moduleCount.begin(); it != moduleCount.end(); ++it)
        moduleSorted.append({it.key(), it.value()});
    std::sort(moduleSorted.begin(), moduleSorted.end(),
              [](const auto &a, const auto &b) { return a.second > b.second; });
    for (int i = 0; i < qMin(20, moduleSorted.size()); ++i)
        stats += QString("  %1: %2 条\n").arg(moduleSorted[i].first).arg(moduleSorted[i].second);

    if (!entries.isEmpty()) {
        stats += QString("\n时间范围: %1 ~ %2\n")
                     .arg(entries.first().timestamp.toString("yyyy-MM-dd HH:mm:ss"))
                     .arg(entries.last().timestamp.toString("yyyy-MM-dd HH:mm:ss"));
    }

    QMessageBox::information(this, "统计信息", stats);
}

void LogAnalysisWidget::onClearResult()
{
    m_model->clear();
    m_mergedEntries.clear();
    m_playIndex = 0;
    m_searchIndex = -1;
    m_playing = false;
    m_playTimer->stop();
    m_btnPlay->setText("▶ 播放");
    m_slider->setRange(0, 0);
    m_lblSearchResult->setText("");
    updatePlaybackLabels();
}

void LogAnalysisWidget::onAddCustomLevel()
{
    QString level = m_customLevel->text().trimmed().toUpper();
    if (level.isEmpty()) return;

    // 检查是否已存在
    for (auto *cb : m_customLevelChecks) {
        if (cb->text().toUpper() == level) return;
    }
    if (level == "INFO" || level == "ERROR" || level == "WARN" || level == "DEBUG")
        return;

    auto *cb = new QCheckBox(level);
    cb->setChecked(true);
    int idx = m_levelLayout->indexOf(m_customLevel);
    m_levelLayout->insertWidget(idx, cb);
    m_customLevelChecks.append(cb);
    connect(cb, &QCheckBox::toggled, this, &LogAnalysisWidget::onApplyFilter);
    m_customLevel->clear();
}

void LogAnalysisWidget::onApplyFilter()
{
    if (m_mergedEntries.isEmpty()) return;

    QVector<LogEntry> filtered = applyFilter(m_mergedEntries);
    const qsizetype filteredCount = filtered.size();
    m_model->setEntries(std::move(filtered));
    m_slider->setRange(0, qMax<qsizetype>(0, filteredCount - 1));
    m_slider->setValue(0);
    m_playIndex = 0;
    m_searchIndex = -1;
    m_lblSearchResult->setText("");
    updatePlaybackLabels();
}

// ======== 搜索跳转 ========

void LogAnalysisWidget::onSearchNext()
{
    QString keyword = m_searchEdit->text().trimmed();
    if (keyword.isEmpty()) {
        m_lblSearchResult->setText("");
        return;
    }

    const auto &entries = m_model->entries();
    if (entries.isEmpty()) {
        m_lblSearchResult->setText("无数据");
        m_lblSearchResult->setStyleSheet("color: #f85149;");
        return;
    }

    int start = m_searchIndex + 1;
    if (start >= entries.size()) start = 0;

    for (int count = 0; count < entries.size(); ++count) {
        int i = (start + count) % entries.size();
        const auto &e = entries[i];
        if (e.message.contains(keyword, Qt::CaseInsensitive) ||
            e.module.contains(keyword, Qt::CaseInsensitive) ||
            e.level.contains(keyword, Qt::CaseInsensitive)) {
            m_searchIndex = i;
            m_tableView->selectRow(i);
            m_tableView->scrollTo(m_model->index(i, 0), QAbstractItemView::PositionAtCenter);
            m_slider->setValue(i);
            m_playIndex = i;
            updatePlaybackLabels();
            QString hint = (count >= entries.size() - start) ? " [已循环]" : "";
            m_lblSearchResult->setText(QString("第 %1 行匹配%2").arg(i + 1).arg(hint));
            m_lblSearchResult->setStyleSheet("color: #3fb950;");
            return;
        }
    }

    m_lblSearchResult->setText("未找到匹配");
    m_lblSearchResult->setStyleSheet("color: #f85149;");
}

void LogAnalysisWidget::onSearchPrev()
{
    QString keyword = m_searchEdit->text().trimmed();
    if (keyword.isEmpty()) {
        m_lblSearchResult->setText("");
        return;
    }

    const auto &entries = m_model->entries();
    if (entries.isEmpty()) {
        m_lblSearchResult->setText("无数据");
        m_lblSearchResult->setStyleSheet("color: #f85149;");
        return;
    }

    int start = m_searchIndex - 1;
    if (start < 0) start = entries.size() - 1;

    for (int count = 0; count < entries.size(); ++count) {
        int i = (start - count + entries.size()) % entries.size();
        const auto &e = entries[i];
        if (e.message.contains(keyword, Qt::CaseInsensitive) ||
            e.module.contains(keyword, Qt::CaseInsensitive) ||
            e.level.contains(keyword, Qt::CaseInsensitive)) {
            m_searchIndex = i;
            m_tableView->selectRow(i);
            m_tableView->scrollTo(m_model->index(i, 0), QAbstractItemView::PositionAtCenter);
            m_slider->setValue(i);
            m_playIndex = i;
            updatePlaybackLabels();
            QString hint = (count > start) ? " [已循环]" : "";
            m_lblSearchResult->setText(QString("第 %1 行匹配%2").arg(i + 1).arg(hint));
            m_lblSearchResult->setStyleSheet("color: #3fb950;");
            return;
        }
    }

    m_lblSearchResult->setText("未找到匹配");
    m_lblSearchResult->setStyleSheet("color: #f85149;");
}

// ======== 播放控制 ========

void LogAnalysisWidget::onPlayPause()
{
    if (m_model->rowCount() == 0) return;

    m_playing = !m_playing;
    if (m_playing) {
        m_btnPlay->setText("⏸ 暂停");
        m_playTimer->start(50); // 初始间隔，会在 tick 中动态调整
    } else {
        m_btnPlay->setText("▶ 播放");
        m_playTimer->stop();
    }
}

void LogAnalysisWidget::onPrevFrame()
{
    if (m_playIndex > 0) {
        m_playIndex--;
        m_slider->setValue(m_playIndex);
        m_tableView->selectRow(m_playIndex);
        m_tableView->scrollTo(m_model->index(m_playIndex, 0));
        updatePlaybackLabels();
    }
}

void LogAnalysisWidget::onNextFrame()
{
    if (m_playIndex < m_model->rowCount() - 1) {
        m_playIndex++;
        m_slider->setValue(m_playIndex);
        m_tableView->selectRow(m_playIndex);
        m_tableView->scrollTo(m_model->index(m_playIndex, 0));
        updatePlaybackLabels();
    }
}

void LogAnalysisWidget::onSpeedChanged(int index)
{
    static const double speeds[] = {0.5, 1.0, 2.0, 5.0, 10.0, 100.0};
    if (index >= 0 && index < 6)
        m_speedFactor = speeds[index];
}

void LogAnalysisWidget::onSliderMoved(int value)
{
    m_playIndex = value;
    m_tableView->selectRow(m_playIndex);
    m_tableView->scrollTo(m_model->index(m_playIndex, 0));
    updatePlaybackLabels();
}

void LogAnalysisWidget::onPlaybackTick()
{
    if (!m_playing || m_playIndex >= m_model->rowCount() - 1) {
        m_playing = false;
        m_btnPlay->setText("▶ 播放");
        m_playTimer->stop();
        return;
    }

    m_playIndex++;
    m_slider->setValue(m_playIndex);
    m_tableView->selectRow(m_playIndex);
    m_tableView->scrollTo(m_model->index(m_playIndex, 0));
    updatePlaybackLabels();

    // 根据下一条日志的时间差计算等待
    if (m_playIndex < m_model->rowCount() - 1) {
        const auto &current = m_model->entries()[m_playIndex];
        const auto &next = m_model->entries()[m_playIndex + 1];
        qint64 diffMs = current.timestamp.msecsTo(next.timestamp);
        int interval = qMax(1, static_cast<int>(diffMs / m_speedFactor));
        interval = qMin(interval, 2000); // 最多等 2 秒
        m_playTimer->setInterval(interval);
    }
}

void LogAnalysisWidget::updatePlaybackLabels()
{
    const auto &entries = m_model->entries();
    if (entries.isEmpty()) {
        m_lblCurrent->setText("00:00:00");
        m_lblTotal->setText("00:00:00");
        return;
    }

    auto formatTime = [](const QDateTime &dt) {
        return dt.toString("HH:mm:ss");
    };

    if (m_playIndex < entries.size())
        m_lblCurrent->setText(formatTime(entries[m_playIndex].timestamp));
    m_lblTotal->setText(formatTime(entries.last().timestamp));
}
