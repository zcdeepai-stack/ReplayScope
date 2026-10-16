#include "customanalyzer.h"
#include "scopechart.h"
#include "rangeslider.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QListWidget>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QFrame>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProgressDialog>
#include <QApplication>
#include <QListView>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QCoreApplication>
#include <QMenu>
#include <QTimer>
#include <QSet>
#include <QStyledItemDelegate>
#include <QPainter>

// 纯视觉序号代理：显示 "N. 列名"，但 item->text() 仍为原始列名
class IndexedListDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override {
        QStyledItemDelegate::initStyleOption(option, index);
        option->text = QString("%1. %2").arg(index.row() + 1, 2).arg(option->text);
    }
};

// ---- 科技风颜色 ----
static const QColor CYAN(0, 212, 255);
static const QColor GREEN(0, 255, 136);
static const QColor YELLOW(255, 200, 0);
static const QColor RED(255, 60, 80);
static const QColor PURPLE(180, 100, 255);
static const QColor ORANGE(255, 140, 40);
static const QColor PINK(255, 80, 180);
static const QColor kColorTable[] = {CYAN, GREEN, YELLOW, RED, PURPLE, ORANGE, PINK};
static const int kColorTableSize = 7;

static const char *SPLITTER_STYLE = R"(
    QSplitter::handle {
        background: qlineargradient(x1:0,y1:0,x2:1,y2:0,
            stop:0 transparent, stop:0.15 #1a4060, stop:0.85 #1a4060, stop:1 transparent);
        height: 6px; margin: 1px 10px;
    }
    QSplitter::handle:hover {
        background: qlineargradient(x1:0,y1:0,x2:1,y2:0,
            stop:0 transparent, stop:0.15 #00d4ff, stop:0.85 #00d4ff, stop:1 transparent);
    }
)";

static const char *SIDEBAR_STYLE = R"(
    QWidget#waSidePanel {
        background: #080c12;
    }
    QLabel { border: none; font-family: "Microsoft YaHei"; font-size: 12px; }
    QSpinBox, QDoubleSpinBox {
        background: #0a1828; color: #00d4ff;
        border: 1px solid #1a4060; border-radius: 3px;
        padding: 1px 4px; font-family: "Consolas"; font-size: 11px;
    }
    QSpinBox:focus, QDoubleSpinBox:focus { border-color: #00d4ff; }
)";

static const char *LIST_STYLE = R"(
    QListWidget {
        background: #0a1828; color: #b0d0e8;
        border: 1px solid #1a4060; border-radius: 3px;
        font-family: "Consolas"; font-size: 10px;
        outline: none;
    }
    QListWidget::item {
        padding: 3px 6px;
        border-bottom: 1px solid #1e3a50;
    }
    QListWidget::item:hover {
        background: #005888; color: #00ffff;
    }
    QListWidget::item:selected {
        background: #004060; color: #00ffff;
    }
)";

static const char *SMALL_BTN_STYLE = R"(
    QPushButton {
        background: #0c1824; color: #00d4ff;
        border: 1px solid #1a4060; border-radius: 3px;
        padding: 4px 10px; font-family: "Microsoft YaHei"; font-size: 10px;
    }
    QPushButton:hover { border-color: #00d4ff; background: #102030; }
    QPushButton:pressed { background: #081018; }
)";

// ============================================================
CustomAnalyzer::CustomAnalyzer(QWidget *parent)
    : QWidget(parent)
{
    initUi();
}

void CustomAnalyzer::initUi()
{
    auto *rootLay = new QHBoxLayout(this);
    rootLay->setContentsMargins(0, 0, 0, 0);
    rootLay->setSpacing(0);

    rootLay->addWidget(createSidePanel());

    auto *rightArea = new QWidget;
    auto *rightLay = new QVBoxLayout(rightArea);
    rightLay->setContentsMargins(0, 0, 0, 0);
    rightLay->setSpacing(0);

    // 顶部标题栏
    auto *titleBar = new QWidget;
    titleBar->setFixedHeight(36);
    titleBar->setStyleSheet("background: #0c1420; border-bottom: 1px solid #1a3050;");
    auto *titleLay = new QHBoxLayout(titleBar);
    titleLay->setContentsMargins(16, 0, 16, 0);
    auto *titleLbl = new QLabel(QStringLiteral("\u6ce2\u5f62\u5206\u6790\u5668"));
    titleLbl->setStyleSheet("color: #00d4ff; font-size: 14px; font-weight: bold; font-family: 'Microsoft YaHei'; border: none;");
    titleLay->addWidget(titleLbl);
    titleLay->addStretch();
    rightLay->addWidget(titleBar);

    // 图表区
    m_chartSplitter = new QSplitter(Qt::Vertical);
    m_chartSplitter->setHandleWidth(6);
    m_chartSplitter->setStyleSheet(QString("QSplitter { background: #0a0e14; } %1").arg(SPLITTER_STYLE));

    auto *emptyLbl = new QLabel(QStringLiteral("\u52a0\u8f7d CSV \u5e76\u914d\u7f6e\u7a97\u53e3\u540e\u70b9\u51fb\u300c\u7ed8\u5236\u300d"));
    emptyLbl->setObjectName("emptyHint");
    emptyLbl->setAlignment(Qt::AlignCenter);
    emptyLbl->setStyleSheet("QLabel#emptyHint { color: #2a4a60; font-size: 16px; font-family: 'Microsoft YaHei'; border: none; }");
    m_chartSplitter->addWidget(emptyLbl);

    rightLay->addWidget(m_chartSplitter, 1);
    rightLay->addWidget(createTimeControls());
    rootLay->addWidget(rightArea, 1);
}

// ============================================================
//  左侧控制面板
// ============================================================
QWidget *CustomAnalyzer::createSidePanel()
{
    auto *scrollArea = new QScrollArea;
    m_sideScroll = scrollArea;
    scrollArea->setFixedWidth(220);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setStyleSheet(R"(
        QScrollArea { background: #080c12; border: none; }
        QScrollBar:vertical { background: #080c12; width: 6px; }
        QScrollBar::handle:vertical { background: #1a3050; border-radius: 3px; min-height: 20px; }
        QScrollBar::handle:vertical:hover { background: #00d4ff; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }
    )");

    auto *panel = new QWidget;
    panel->setObjectName("waSidePanel");
    panel->setStyleSheet(SIDEBAR_STYLE);

    auto *vlay = new QVBoxLayout(panel);
    vlay->setContentsMargins(10, 10, 10, 10);
    vlay->setSpacing(5);

    // ---- 加载 CSV ----
    auto *loadBtn = new QPushButton(QStringLiteral("\u52a0\u8f7d CSV"));
    loadBtn->setStyleSheet(R"(
        QPushButton {
            background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #004060,stop:1 #006080);
            color: #00d4ff; border: 1px solid #00a0c0; border-radius: 4px;
            padding: 7px 0; font-family: "Microsoft YaHei"; font-size: 12px; font-weight: bold;
        }
        QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #005080,stop:1 #0080a0); }
        QPushButton:pressed { background: #003050; }
    )");
    connect(loadBtn, &QPushButton::clicked, this, &CustomAnalyzer::loadCsvFile);
    vlay->addWidget(loadBtn);

    m_fileLabel = new QLabel(QStringLiteral("\u672a\u52a0\u8f7d"));
    m_fileLabel->setStyleSheet("color: #6090b0; font-size: 10px;");
    m_fileLabel->setAlignment(Qt::AlignCenter);
    m_fileLabel->setWordWrap(true);
    vlay->addWidget(m_fileLabel);

    m_totalTimeLabel = new QLabel(QStringLiteral("\u603b\u65f6\u957f: --"));
    m_totalTimeLabel->setStyleSheet("color: #6090b0; font-size: 11px;");
    m_totalTimeLabel->setAlignment(Qt::AlignCenter);
    vlay->addWidget(m_totalTimeLabel);

    // 分隔线
    auto addSep = [&]() {
        vlay->addSpacing(4);
        auto *sep = new QFrame;
        sep->setFrameShape(QFrame::HLine);
        sep->setFixedHeight(1);
        sep->setStyleSheet("QFrame { background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 transparent, stop:0.1 #1a4060, stop:0.9 #1a4060, stop:1 transparent); border: none; }");
        vlay->addWidget(sep);
        vlay->addSpacing(4);
    };

    addSep();

    // ---- 可用数据列 ----
    auto *colTitle = new QLabel(QStringLiteral("\u53ef\u7528\u6570\u636e\u5217"));
    colTitle->setStyleSheet("color: #00d4ff; font-weight: bold; font-size: 12px;");
    vlay->addWidget(colTitle);

    m_colList = new QListWidget;
    m_colList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_colList->setStyleSheet(LIST_STYLE);
    m_colList->setMaximumHeight(160);
    m_colList->setDragDropMode(QAbstractItemView::DragOnly);
    m_colList->setItemDelegate(new IndexedListDelegate(m_colList));
    vlay->addWidget(m_colList);

    addSep();

    // ---- 窗口配置区 ----
    auto *winTitle = new QLabel(QStringLiteral("\u7a97\u53e3\u914d\u7f6e"));
    winTitle->setStyleSheet("color: #00d4ff; font-weight: bold; font-size: 12px;");
    vlay->addWidget(winTitle);

    m_windowConfigArea = new QWidget;
    m_windowConfigLay = new QVBoxLayout(m_windowConfigArea);
    m_windowConfigLay->setContentsMargins(0, 0, 0, 0);
    m_windowConfigLay->setSpacing(4);
    vlay->addWidget(m_windowConfigArea);

    // 添加窗口按钮
    m_addWindowBtn = new QPushButton(QStringLiteral("+ \u6dfb\u52a0\u7a97\u53e3"));
    m_addWindowBtn->setStyleSheet(R"(
        QPushButton {
            background: #0a1828; color: #00d4ff;
            border: 1px dashed #1a4060; border-radius: 4px;
            padding: 6px 0; font-family: "Microsoft YaHei"; font-size: 11px;
        }
        QPushButton:hover { border-color: #00d4ff; background: #102030; }
    )");
    connect(m_addWindowBtn, &QPushButton::clicked, this, &CustomAnalyzer::addWindow);
    vlay->addWidget(m_addWindowBtn);

    vlay->addSpacing(6);

    // ---- 绘制按钮 ----
    m_drawBtn = new QPushButton(QStringLiteral("\u7ed8  \u5236"));
    m_drawBtn->setStyleSheet(R"(
        QPushButton {
            background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #005020,stop:1 #008040);
            color: #00ff88; border: 1px solid #00c060; border-radius: 4px;
            padding: 8px 0; font-family: "Microsoft YaHei"; font-size: 13px; font-weight: bold;
        }
        QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #006030,stop:1 #00a050); }
        QPushButton:pressed { background: #004020; }
    )");
    connect(m_drawBtn, &QPushButton::clicked, this, &CustomAnalyzer::rebuildCharts);
    vlay->addWidget(m_drawBtn);

    addSep();

    // ---- 配置管理 ----
    auto *cfgTitle = new QLabel(QStringLiteral("\u914d\u7f6e\u7ba1\u7406"));
    cfgTitle->setStyleSheet("color: #00d4ff; font-weight: bold; font-size: 12px;");
    vlay->addWidget(cfgTitle);

    auto *cfgBtnRow = new QHBoxLayout;
    cfgBtnRow->setSpacing(6);

    auto *saveBtn = new QPushButton(QStringLiteral("\u4fdd\u5b58"));
    saveBtn->setStyleSheet(R"(
        QPushButton {
            background: #0c1824; color: #ffc040;
            border: 1px solid #a08020; border-radius: 3px;
            padding: 5px 0; font-family: "Microsoft YaHei"; font-size: 11px; font-weight: bold;
        }
        QPushButton:hover { background: #1a2838; border-color: #ffc040; }
    )");
    connect(saveBtn, &QPushButton::clicked, this, &CustomAnalyzer::saveConfig);
    cfgBtnRow->addWidget(saveBtn);

    auto *delCfgBtn = new QPushButton(QStringLiteral("\u5220\u9664"));
    delCfgBtn->setStyleSheet(R"(
        QPushButton {
            background: #0c1824; color: #ff4060;
            border: 1px solid #802030; border-radius: 3px;
            padding: 5px 0; font-family: "Microsoft YaHei"; font-size: 11px; font-weight: bold;
        }
        QPushButton:hover { background: #1a2838; border-color: #ff4060; }
    )");
    connect(delCfgBtn, &QPushButton::clicked, this, &CustomAnalyzer::deleteConfig);
    cfgBtnRow->addWidget(delCfgBtn);
    vlay->addLayout(cfgBtnRow);

    vlay->addSpacing(2);

    m_configTree = new QTreeWidget;
    m_configTree->setColumnCount(3);
    m_configTree->setHeaderLabels({QStringLiteral("\u914d\u7f6e\u540d\u79f0"), QStringLiteral("\u65f6\u95f4"), QStringLiteral("\u64cd\u4f5c")});
    m_configTree->setRootIsDecorated(false);
    m_configTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_configTree->setMaximumHeight(180);
    m_configTree->header()->setStretchLastSection(false);
    m_configTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_configTree->header()->resizeSection(1, 80);
    m_configTree->header()->resizeSection(2, 30);
    m_configTree->setStyleSheet(R"(
        QTreeWidget {
            background: #0a1828; color: #b0d0e8;
            border: 1px solid #1a4060; border-radius: 3px;
            font-family: "Microsoft YaHei"; font-size: 10px;
            outline: none;
        }
        QTreeWidget::item {
            padding: 4px 4px;
            border-bottom: 1px solid #1e3a50;
        }
        QTreeWidget::item:hover {
            background: #005888; color: #00ffff;
        }
        QTreeWidget::item:selected {
            background: #004060; color: #00ffff;
        }
        QHeaderView::section {
            background: #0c1824; color: #4a8ab0;
            border: none; border-bottom: 1px solid #1a4060; border-right: 1px solid #1a4060;
            padding: 4px 6px; font-family: "Microsoft YaHei"; font-size: 10px; font-weight: bold;
        }
    )");
    // 双击加载配置
    connect(m_configTree, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem *item, int) {
        QString path = item->data(0, Qt::UserRole).toString();
        loadConfig(path);
    });
    vlay->addWidget(m_configTree);

    vlay->addStretch();

    // 初始化默认1个窗口
    addWindow();

    // 扫描已有配置
    scanConfigs();

    scrollArea->setWidget(panel);
    return scrollArea;
}

// ============================================================
//  底部时间控制栏
// ============================================================
QWidget *CustomAnalyzer::createTimeControls()
{
    auto *bar = new QWidget;
    bar->setObjectName("waTimeBar");
    bar->setFixedHeight(72);
    bar->setStyleSheet(R"(
        QWidget#waTimeBar { background: #080c12; border-top: 1px solid #1a3050; }
        QLabel { color: #6090b0; font-family: "Microsoft YaHei"; font-size: 10px; border: none; }
        QDoubleSpinBox {
            background: #0c1824; color: #00d4ff;
            border: 1px solid #1a4060; border-radius: 3px;
            padding: 2px 6px; font-family: "Consolas"; font-size: 11px;
        }
        QDoubleSpinBox:focus { border: 1px solid #00d4ff; }
    )");

    auto *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(16, 6, 16, 6);
    lay->setSpacing(10);

    lay->addWidget(new QLabel(QStringLiteral("\u8d77\u59cb\u65f6\u95f4:")));
    m_timeStart = new QDoubleSpinBox;
    m_timeStart->setRange(0, 99999);
    m_timeStart->setValue(0);
    m_timeStart->setSuffix(" s");
    m_timeStart->setDecimals(2);
    m_timeStart->setSingleStep(1.0);
    m_timeStart->setMinimumWidth(100);
    lay->addWidget(m_timeStart);

    lay->addWidget(new QLabel(QStringLiteral("\u7a97\u53e3\u5bbd\u5ea6:")));
    m_windowSize = new QDoubleSpinBox;
    m_windowSize->setRange(0.1, 99999);
    m_windowSize->setValue(10.0);
    m_windowSize->setSuffix(" s");
    m_windowSize->setDecimals(2);
    m_windowSize->setSingleStep(1.0);
    m_windowSize->setMinimumWidth(90);
    lay->addWidget(m_windowSize);

    m_rangeSlider = new RangeSlider;
    m_rangeSlider->setRange(0, 100);
    m_rangeSlider->setValues(0, 10);
    lay->addWidget(m_rangeSlider, 1);

    auto *resetBtn = new QPushButton(QStringLiteral("\u81ea\u9002\u5e94"));
    resetBtn->setStyleSheet(SMALL_BTN_STYLE);
    connect(resetBtn, &QPushButton::clicked, this, [this]() {
        for (auto *c : m_charts)
            c->rescaleAndReplot();
    });
    lay->addWidget(resetBtn);

    connect(m_timeStart, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &CustomAnalyzer::onTimeRangeChanged);
    connect(m_windowSize, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &CustomAnalyzer::onTimeRangeChanged);
    connect(m_rangeSlider, &RangeSlider::valuesChanged,
            this, &CustomAnalyzer::onRangeSliderChanged);
    return bar;
}

// ============================================================
//  窗口动态管理
// ============================================================
void CustomAnalyzer::addWindow()
{
    int idx = m_windowConfigs.size();

    WindowConfig wc;
    wc.container = new QWidget;
    auto *wl = new QVBoxLayout(wc.container);
    wl->setContentsMargins(0, 2, 0, 2);
    wl->setSpacing(2);

    // 标题行 = 标题 + 添加列 + 删除窗口
    auto *hdr = new QHBoxLayout;
    hdr->setSpacing(4);
    wc.titleLabel = new QLabel(QStringLiteral("\u7a97\u53e3 %1").arg(idx + 1));
    wc.titleLabel->setStyleSheet("color: #00d4ff; font-weight: bold; font-size: 11px;");
    hdr->addWidget(wc.titleLabel);

    hdr->addStretch();

    wc.addBtn = new QPushButton("+");
    wc.addBtn->setFixedSize(24, 24);
    wc.addBtn->setToolTip(QStringLiteral("\u6dfb\u52a0\u9009\u4e2d\u5217\u5230\u6b64\u7a97\u53e3"));
    wc.addBtn->setStyleSheet(R"(
        QPushButton { background: #004060; color: #00d4ff; border: 1px solid #00a0c0; border-radius: 3px; font-weight: bold; font-size: 14px; }
        QPushButton:hover { background: #006080; }
    )");
    connect(wc.addBtn, &QPushButton::clicked, this, [this, idx]() {
        addSelectedColumns(idx);
    });
    hdr->addWidget(wc.addBtn);

    // 清空此窗口所有列
    wc.clearBtn = new QPushButton(QStringLiteral("\u6e05\u7a7a"));
    wc.clearBtn->setFixedHeight(24);
    wc.clearBtn->setToolTip(QStringLiteral("\u6e05\u7a7a\u6b64\u7a97\u53e3\u6240\u6709\u5217"));
    wc.clearBtn->setStyleSheet(R"(
        QPushButton { background: #1a1828; color: #a0a0c0; border: 1px solid #404060; border-radius: 3px; font-size: 10px; padding: 0 6px; }
        QPushButton:hover { background: #302848; color: #c0c0ff; }
    )");
    connect(wc.clearBtn, &QPushButton::clicked, this, [this, idx]() {
        if (idx >= 0 && idx < m_windowConfigs.size())
            m_windowConfigs[idx].columnList->clear();
    });
    hdr->addWidget(wc.clearBtn);

    wc.removeBtn = new QPushButton(QStringLiteral("\u00d7"));
    wc.removeBtn->setFixedSize(24, 24);
    wc.removeBtn->setToolTip(QStringLiteral("\u5220\u9664\u6b64\u7a97\u53e3"));
    wc.removeBtn->setStyleSheet(R"(
        QPushButton { background: #400020; color: #ff4060; border: 1px solid #802030; border-radius: 3px; font-weight: bold; font-size: 14px; }
        QPushButton:hover { background: #601030; }
    )");
    connect(wc.removeBtn, &QPushButton::clicked, this, [this, idx]() {
        removeWindow(idx);
    });
    hdr->addWidget(wc.removeBtn);
    wl->addLayout(hdr);

    wc.columnList = new QListWidget;
    wc.columnList->setStyleSheet(LIST_STYLE);
    wc.columnList->setMaximumHeight(72);
    wc.columnList->setAcceptDrops(true);
    wc.columnList->setDragDropMode(QAbstractItemView::DropOnly);
    wc.columnList->setDefaultDropAction(Qt::CopyAction);
    wc.columnList->setContextMenuPolicy(Qt::CustomContextMenu);
    // 拖拽放入后延迟去重（drop 时 text 还未填充，需等事件循环完成）
    connect(wc.columnList->model(), &QAbstractItemModel::rowsInserted,
            this, [wc]() {
        QTimer::singleShot(0, [wc]() {
            QListWidget *list = wc.columnList;
            QSet<QString> seen;
            for (int i = list->count() - 1; i >= 0; --i) {
                QString text = list->item(i)->text();
                if (text.isEmpty() || seen.contains(text))
                    delete list->takeItem(i);
                else
                    seen.insert(text);
            }
        });
    });
    // 双击移除列
    connect(wc.columnList, &QListWidget::itemDoubleClicked,
            this, [wc](QListWidgetItem *item) {
        delete wc.columnList->takeItem(wc.columnList->row(item));
    });
    // 右键删除菜单
    connect(wc.columnList, &QWidget::customContextMenuRequested,
            this, [wc](const QPoint &pos) {
        auto *item = wc.columnList->itemAt(pos);
        if (!item) return;
        QMenu menu;
        menu.setStyleSheet(R"(
            QMenu { background: #0c1824; color: #b0d0e8; border: 1px solid #1a4060; font-family: "Microsoft YaHei"; font-size: 11px; }
            QMenu::item:selected { background: #005888; color: #00ffff; }
        )");
        auto *act = menu.addAction(QStringLiteral("\u5220\u9664\u6b64\u5217"));
        if (menu.exec(wc.columnList->mapToGlobal(pos)) == act)
            delete wc.columnList->takeItem(wc.columnList->row(item));
    });
    wl->addWidget(wc.columnList);

    m_windowConfigLay->addWidget(wc.container);
    m_windowConfigs.append(wc);
}

void CustomAnalyzer::removeWindow(int idx)
{
    if (idx < 0 || idx >= m_windowConfigs.size()) return;
    if (m_windowConfigs.size() <= 1) return; // 至少保留1个

    auto &wc = m_windowConfigs[idx];
    wc.container->setParent(nullptr);
    delete wc.container;
    m_windowConfigs.remove(idx);

    renumberWindows();
}

void CustomAnalyzer::renumberWindows()
{
    for (int i = 0; i < m_windowConfigs.size(); ++i) {
        auto &wc = m_windowConfigs[i];
        wc.titleLabel->setText(QStringLiteral("\u7a97\u53e3 %1").arg(i + 1));

        disconnect(wc.addBtn, &QPushButton::clicked, nullptr, nullptr);
        connect(wc.addBtn, &QPushButton::clicked, this, [this, i]() {
            addSelectedColumns(i);
        });

        disconnect(wc.clearBtn, &QPushButton::clicked, nullptr, nullptr);
        connect(wc.clearBtn, &QPushButton::clicked, this, [this, i]() {
            if (i >= 0 && i < m_windowConfigs.size())
                m_windowConfigs[i].columnList->clear();
        });

        disconnect(wc.removeBtn, &QPushButton::clicked, nullptr, nullptr);
        connect(wc.removeBtn, &QPushButton::clicked, this, [this, i]() {
            removeWindow(i);
        });
    }
}

void CustomAnalyzer::addSelectedColumns(int windowIdx)
{
    if (windowIdx < 0 || windowIdx >= m_windowConfigs.size()) return;
    auto *target = m_windowConfigs[windowIdx].columnList;
    auto selected = m_colList->selectedItems();
    for (auto *item : selected) {
        QString colName = item->text();
        bool dup = false;
        for (int r = 0; r < target->count(); ++r)
            if (target->item(r)->text() == colName) { dup = true; break; }
        if (!dup)
            target->addItem(colName);
    }
}

// ============================================================
//  配置目录（exe 同级 wave_configs）
// ============================================================
QString CustomAnalyzer::configDir() const
{
    QString dir = QCoreApplication::applicationDirPath() + "/wave_configs";
    QDir().mkpath(dir);
    return dir;
}

// ============================================================
//  CSV 加载
// ============================================================
void CustomAnalyzer::loadCsvFile()
{
    QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("\u9009\u62e9 CSV \u6570\u636e\u6587\u4ef6"), QString(),
        "CSV/Snapshot (*.csv *.snapshot.csv);;All (*)");
    if (path.isEmpty()) return;
    loadCsvFromPath(path);
}

void CustomAnalyzer::loadCsvFromPath(const QString &path)
{
    if (!QFileInfo::exists(path)) return;

    auto *dlg = new QProgressDialog(this);
    dlg->setWindowTitle(QStringLiteral("\u52a0\u8f7d\u6570\u636e"));
    dlg->setLabelText(QStringLiteral("\u6b63\u5728\u52a0\u8f7d: %1").arg(QFileInfo(path).fileName()));
    dlg->setRange(0, 100);
    dlg->setValue(0);
    dlg->setCancelButton(nullptr);
    dlg->setMinimumDuration(0);
    dlg->setWindowModality(Qt::WindowModal);
    dlg->setStyleSheet(R"(
        QProgressDialog { background: #0a0e14; }
        QLabel { color: #00d4ff; font-size: 12px; font-family: "Microsoft YaHei"; }
        QProgressBar { background: #0c1824; border: 1px solid #1a4060; border-radius: 3px; text-align: center; color: #00d4ff; }
        QProgressBar::chunk { background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #005888, stop:1 #00d4ff); border-radius: 2px; }
    )");
    dlg->show();
    QApplication::processEvents();

    m_csvPath = path;

    // 与数据分析页共用 csvparser.cpp 的字节级解析
    int lastPct = -1;
    const bool ok = parseCsvFile(path, m_csvData,
        [&](qint64 done, qint64 total) {
            const int pct = total > 0 ? int(done * 100 / total) : 0;
            if (pct != lastPct) {
                lastPct = pct;
                dlg->setValue(pct);
                QApplication::processEvents();
            }
            return true;
        });

    if (!ok || m_csvData.colCount == 0) { dlg->close(); dlg->deleteLater(); return; }

    // 更新 UI
    double total = m_csvData.totalTime();
    int tsCol = m_csvData.findColumn("Timestamp (s)");
    double tsStart = 0.0, tsEnd = total;
    if (tsCol >= 0 && m_csvData.rowCount > 0) {
        tsStart = m_csvData.columns[tsCol].first();
        tsEnd   = m_csvData.columns[tsCol].last();
        total   = tsEnd - tsStart;
    }
    double windowW = qMin(10.0, tsEnd - tsStart);

    m_fileLabel->setText(QFileInfo(path).fileName());
    m_totalTimeLabel->setText(QStringLiteral("\u603b\u65f6\u957f: %1 s | %2\u884c\u00d7%3\u5217")
                                  .arg(total, 0, 'f', 1)
                                  .arg(m_csvData.rowCount)
                                  .arg(m_csvData.colCount));

    m_rangeSlider->setRange(tsStart, tsEnd);
    m_rangeSlider->setValues(tsStart, tsStart + windowW);
    m_timeStart->setRange(tsStart, qMax(tsStart, tsEnd));
    m_windowSize->setRange(0.1, qMax(0.1, tsEnd - tsStart));
    m_timeStart->setValue(tsStart);
    m_windowSize->setValue(windowW);

    populateColumnList();

    dlg->close();
    dlg->deleteLater();
}

void CustomAnalyzer::populateColumnList()
{
    m_colList->clear();
    for (const auto &h : m_csvData.headers) {
        if (h == "Timestamp (s)") continue;
        m_colList->addItem(h);
    }
}

// ============================================================
//  图表动态生成与刷新
// ============================================================
void CustomAnalyzer::rebuildCharts()
{
    if (m_csvData.rowCount == 0) {
        QMessageBox::information(this, QStringLiteral("\u63d0\u793a"),
            QStringLiteral("\u8bf7\u5148\u52a0\u8f7d CSV \u6570\u636e\u6587\u4ef6"));
        return;
    }

    // 清除旧图表
    for (auto *c : m_charts) { c->setParent(nullptr); delete c; }
    m_charts.clear();

    while (m_chartSplitter->count() > 0) {
        auto *w = m_chartSplitter->widget(0);
        w->setParent(nullptr);
        if (w->objectName() == "emptyHint") delete w;
    }

    bool anyChart = false;

    for (int w = 0; w < m_windowConfigs.size(); ++w) {
        auto *wList = m_windowConfigs[w].columnList;
        if (wList->count() == 0) continue;

        QVector<CurveInfo> curves;
        for (int r = 0; r < wList->count(); ++r) {
            QString colName = wList->item(r)->text();
            CurveInfo ci;
            ci.name = colName;
            ci.color = kColorTable[r % kColorTableSize];
            ci.columnIdx = -1;
            curves.append(ci);
        }

        auto *chart = new ScopeChart(
            QStringLiteral("\u7a97\u53e3 %1").arg(w + 1), curves);
        m_chartSplitter->addWidget(chart);
        m_charts.append(chart);

        connect(chart, &ScopeChart::rangeSelected,
                this, &CustomAnalyzer::onRangeSelected);
        anyChart = true;
    }

    if (!anyChart) {
        auto *lbl = new QLabel(QStringLiteral("\u8bf7\u5411\u7a97\u53e3\u6dfb\u52a0\u6570\u636e\u5217\u540e\u518d\u70b9\u51fb\u7ed8\u5236"));
        lbl->setObjectName("emptyHint");
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setStyleSheet("QLabel#emptyHint { color: #2a4a60; font-size: 16px; font-family: 'Microsoft YaHei'; border: none; }");
        m_chartSplitter->addWidget(lbl);
        return;
    }

    QList<int> sizes;
    for (int i = 0; i < m_charts.size(); ++i) sizes << 1;
    m_chartSplitter->setSizes(sizes);

    refreshCharts();
}

void CustomAnalyzer::refreshCharts()
{
    if (m_csvData.rowCount == 0 || m_charts.isEmpty()) return;

    QVector<double> timeVec;
    int tsCol = m_csvData.findColumn("Timestamp (s)");
    if (tsCol >= 0 && tsCol < m_csvData.colCount) {
        timeVec = m_csvData.columns[tsCol];
    } else {
        double dt = 1.0 / m_csvData.sampleRate;
        timeVec.resize(m_csvData.rowCount);
        for (int i = 0; i < m_csvData.rowCount; ++i)
            timeVec[i] = i * dt;
    }

    const double t0 = m_timeStart->value();
    const double win = m_windowSize->value();

    int chartIdx = 0;
    for (int w = 0; w < m_windowConfigs.size(); ++w) {
        auto *wList = m_windowConfigs[w].columnList;
        if (wList->count() == 0) continue;
        if (chartIdx >= m_charts.size()) break;

        auto *chart = m_charts[chartIdx];
        // 装载期间挂起重绘，最后统一绘制一次
        chart->setReplotSuspended(true);
        chart->clearData();

        for (int r = 0; r < wList->count(); ++r) {
            QString colName = wList->item(r)->text();
            int csvCol = m_csvData.findColumn(colName);
            if (csvCol >= 0 && csvCol < m_csvData.colCount)
                chart->setData(r, timeVec, m_csvData.columns[csvCol], m_csvData.timeSorted);
        }
        chart->rescaleAndReplot();
        chart->setTimeRange(t0, t0 + win);
        chart->setReplotSuspended(false);
        chartIdx++;
    }
}

// ============================================================
//  配置保存 / 加载 / 删除
// ============================================================
void CustomAnalyzer::saveConfig()
{
    if (m_windowConfigs.isEmpty()) return;

    bool ok;
    QString name = QInputDialog::getText(this,
        QStringLiteral("\u4fdd\u5b58\u914d\u7f6e"),
        QStringLiteral("\u914d\u7f6e\u540d\u79f0:"),
        QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    name = name.trimmed();

    QJsonObject root;
    root["windowCount"] = m_windowConfigs.size();

    QJsonArray windows;
    for (int i = 0; i < m_windowConfigs.size(); ++i) {
        QJsonObject win;
        win["title"] = QStringLiteral("\u7a97\u53e3 %1").arg(i + 1);
        QJsonArray cols;
        auto *list = m_windowConfigs[i].columnList;
        for (int r = 0; r < list->count(); ++r)
            cols.append(list->item(r)->text());
        win["columns"] = cols;
        windows.append(win);
    }
    root["windows"] = windows;

    QString filePath = configDir() + "/" + name + ".waveconfig.json";
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
        QMessageBox::information(this, QStringLiteral("\u5df2\u4fdd\u5b58"),
            QStringLiteral("\u914d\u7f6e\u5df2\u4fdd\u5b58: %1").arg(name));
        scanConfigs();
    }
}

void CustomAnalyzer::loadConfig(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();

    int count = root["windowCount"].toInt(1);
    count = qMax(1, count);

    // 调整窗口数量
    while (m_windowConfigs.size() > count)
        removeWindow(m_windowConfigs.size() - 1);
    while (m_windowConfigs.size() < count)
        addWindow();

    QJsonArray windows = root["windows"].toArray();
    for (int i = 0; i < windows.size() && i < m_windowConfigs.size(); ++i) {
        m_windowConfigs[i].columnList->clear();
        QJsonArray cols = windows[i].toObject()["columns"].toArray();
        for (const auto &c : cols)
            m_windowConfigs[i].columnList->addItem(c.toString());
    }

    // 自动绘制
    if (m_csvData.rowCount > 0)
        rebuildCharts();
}

void CustomAnalyzer::deleteConfig()
{
    auto *item = m_configTree->currentItem();
    if (!item) {
        QMessageBox::information(this, QStringLiteral("\u63d0\u793a"),
            QStringLiteral("\u8bf7\u5148\u9009\u62e9\u8981\u5220\u9664\u7684\u914d\u7f6e"));
        return;
    }

    QString name = item->text(0);
    QString path = item->data(0, Qt::UserRole).toString();

    auto ret = QMessageBox::question(this, QStringLiteral("\u786e\u8ba4\u5220\u9664"),
        QStringLiteral("\u786e\u5b9a\u5220\u9664\u914d\u7f6e\u300c%1\u300d\uff1f").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    QFile::remove(path);
    scanConfigs();
}

void CustomAnalyzer::scanConfigs()
{
    m_configTree->clear();

    QDir dir(configDir());
    QStringList filters;
    filters << "*.waveconfig.json";
    QStringList files = dir.entryList(filters, QDir::Files, QDir::Time);
    for (const auto &f : files) {
        QString name = f;
        name.remove(".waveconfig.json");

        QFileInfo fi(dir.absoluteFilePath(f));
        QString dateStr = fi.lastModified().toString("MM-dd HH:mm");
        QString fullPath = dir.absoluteFilePath(f);

        auto *item = new QTreeWidgetItem(m_configTree);
        item->setText(0, name);
        item->setText(1, dateStr);
        item->setData(0, Qt::UserRole, fullPath);
        item->setToolTip(0, QStringLiteral("\u53cc\u51fb\u52a0\u8f7d\u914d\u7f6e"));

        // 播放按钮
        auto *playBtn = new QPushButton(QStringLiteral("\u25b6"));
        playBtn->setFixedSize(22, 18);
        playBtn->setToolTip(QStringLiteral("\u52a0\u8f7d\u5e76\u7ed8\u5236"));
        playBtn->setStyleSheet(R"(
            QPushButton { background: #004060; color: #00ff88; border: 1px solid #00a060; border-radius: 3px; font-size: 10px; padding: 0; }
            QPushButton:hover { background: #006040; border-color: #00ff88; }
            QPushButton:pressed { background: #003030; }
        )");
        connect(playBtn, &QPushButton::clicked, this, [this, fullPath]() {
            loadConfig(fullPath);
        });
        m_configTree->setItemWidget(item, 2, playBtn);
    }
}

// ============================================================
//  时间控制槽
// ============================================================
void CustomAnalyzer::onTimeRangeChanged()
{
    double t0 = m_timeStart->value();
    double w  = m_windowSize->value();
    m_rangeSlider->blockSignals(true);
    m_rangeSlider->setValues(t0, t0 + w);
    m_rangeSlider->blockSignals(false);
    for (auto *c : m_charts)
        c->setTimeRange(t0, t0 + w);
}

void CustomAnalyzer::onRangeSliderChanged(double lower, double upper)
{
    m_timeStart->blockSignals(true);
    m_windowSize->blockSignals(true);
    m_timeStart->setValue(lower);
    m_windowSize->setValue(upper - lower);
    m_timeStart->blockSignals(false);
    m_windowSize->blockSignals(false);
    for (auto *c : m_charts)
        c->setTimeRange(lower, upper);
}

void CustomAnalyzer::onRangeSelected(double tMin, double tMax)
{
    double w = tMax - tMin;
    m_timeStart->blockSignals(true);
    m_windowSize->blockSignals(true);
    m_rangeSlider->blockSignals(true);

    m_timeStart->setValue(tMin);
    m_windowSize->setValue(w);
    m_rangeSlider->setValues(tMin, tMax);

    m_timeStart->blockSignals(false);
    m_windowSize->blockSignals(false);
    m_rangeSlider->blockSignals(false);

    for (auto *c : m_charts)
        c->setTimeRange(tMin, tMax);
}
