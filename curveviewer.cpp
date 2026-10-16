#include "curveviewer.h"
#include "scopechart.h"
#include "rangeslider.h"
#include "qcustomplot.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabBar>
#include <QStackedWidget>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QFrame>
#include <QScrollArea>
#include <QListView>
#include <QCloseEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QPainter>
#include <QPdfWriter>
#include <QPageSize>
#include <QProgressDialog>
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>

// ---- 科技风颜色 ----
static const QColor CYAN(0, 212, 255);
static const QColor GREEN(0, 255, 136);
static const QColor YELLOW(255, 200, 0);
static const QColor RED(255, 60, 80);
static const QColor PURPLE(180, 100, 255);
static const QColor ORANGE(255, 140, 40);
static const QColor PINK(255, 80, 180);

static const char *TAB_STYLE = R"(
    QTabBar { background: transparent; border: none; }
    QTabBar::tab {
        background: #0c1420; color: #6090b0;
        border: 1px solid #1a3050; border-bottom: 2px solid #0a0e14;
        padding: 8px 22px; margin-right: 2px;
        font-family: "Microsoft YaHei"; font-size: 11px; font-weight: bold;
        border-top-left-radius: 6px; border-top-right-radius: 6px;
    }
    QTabBar::tab:selected {
        background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #0a2a40,stop:1 #0a0e14);
        color: #00d4ff; border: 1px solid #00d4ff; border-bottom: 2px solid #0a0e14;
    }
    QTabBar::tab:hover:!selected { background: #0f1f30; color: #80c0e0; }
)";

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
    QWidget#sidePanel {
        background: #080c12;
    }
    QLabel { border: none; font-family: "Microsoft YaHei"; font-size: 12px; }
    QComboBox {
        background: #0a1828; color: #00d4ff;
        border: 1px solid #1a4060; border-radius: 3px;
        padding: 2px 6px; font-family: "Microsoft YaHei"; font-size: 11px;
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
        background: #0c1824; color: #b0d0e8;
        border: 1px solid #1a4060; outline: none;
    }
    QSpinBox {
        background: #0a1828; color: #00d4ff;
        border: 1px solid #1a4060; border-radius: 3px;
        padding: 1px 4px; font-family: "Consolas"; font-size: 11px;
    }
    QSpinBox:focus { border-color: #00d4ff; }
)";

static void applyComboViewStyle(QComboBox *combo)
{
    auto *view = new QListView(combo);
    view->setStyleSheet(R"(
        QListView {
            background: #0c1824;
            color: #b0d0e8;
            border: 1px solid #1a4060;
            outline: none;
        }
        QListView::item {
            padding: 5px 8px;
            border: none;
        }
        QListView::item:hover {
            background: #005888;
            color: #00ffff;
        }
        QListView::item:selected {
            background: #004060;
            color: #00ffff;
        }
    )");
    combo->setView(view);
}

// ============================================================
CurveViewer::CurveViewer(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle(QString::fromUtf8("\u6570\u636e\u5206\u6790"));

    // \u4e0d\u80fd\u5199\u6b7b 1200x960\uff1a\u7b14\u8bb0\u672c\uff081366x768 \u53ef\u7528\u9ad8\u5ea6\u7ea6 728\uff09\u4e0a\u4f1a\u76f4\u63a5\u8d85\u51fa\u5c4f\u5e55\uff0c
    // \u5e95\u90e8\u7684\u65f6\u95f4\u63a7\u5236\u6761\u548c\u8fdb\u5ea6\u6ed1\u5757\u88ab\u5207\u6389\u3002\u6309\u5f53\u524d\u5c4f\u5e55\u53ef\u7528\u533a\u57df\u53d6\u4e0a\u9650\u3002
    if (auto *scr = QGuiApplication::primaryScreen()) {
        const QRect avail = scr->availableGeometry();
        const int w = qMin(1200, int(avail.width()  * 0.92));
        const int h = qMin(960,  int(avail.height() * 0.92));
        resize(w, h);
        move(avail.x() + (avail.width()  - w) / 2,
             avail.y() + (avail.height() - h) / 2);
    } else {
        resize(1200, 760);
    }
    setAttribute(Qt::WA_DeleteOnClose, false);
    setStyleSheet("QWidget { background: #0a0e14; }");
    initColumnMap();
    initUi();
}

void CurveViewer::closeEvent(QCloseEvent *event)
{
    hide();
    emit viewerClosed();
    event->ignore();
}

void CurveViewer::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    emit analysisVisibilityChanged(true);
}

void CurveViewer::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    emit analysisVisibilityChanged(false);
}

void CurveViewer::initUi()
{
    auto *rootLay = new QHBoxLayout(this);
    rootLay->setContentsMargins(0, 0, 0, 0);
    rootLay->setSpacing(0);

    // ========== 左侧边栏 ==========
    rootLay->addWidget(createConfigPanel());

    // ========== 右侧内容区 ==========
    auto *rightArea = new QWidget;
    auto *rightLay = new QVBoxLayout(rightArea);
    rightLay->setContentsMargins(0, 0, 0, 0);
    rightLay->setSpacing(0);

    m_tabBar = new QTabBar;
    m_tabBar->setStyleSheet(TAB_STYLE);
    m_tabBar->setExpanding(false);
    m_tabBar->setDrawBase(false);
    m_tabBar->addTab("姿态");
    m_tabBar->addTab("关节");
    m_tabBar->addTab("算法");
    m_tabBar->addTab("性能");
    m_tabBar->addTab("温度");
    m_tabBar->addTab("状态");
    rightLay->addWidget(m_tabBar);

    m_stack = new QStackedWidget;
    m_stack->setStyleSheet("QStackedWidget { background: #0a0e14; border: none; }");
    m_charts.resize(6);
    m_stack->addWidget(createAttitudePage());
    m_stack->addWidget(createJointPage());
    m_stack->addWidget(createAlgorithmPage());
    m_stack->addWidget(createPerformancePage());
    m_stack->addWidget(createTemperaturePage());
    m_stack->addWidget(createStatusPage());
    rightLay->addWidget(m_stack, 1);

    rightLay->addWidget(createTimeControls());
    rootLay->addWidget(rightArea, 1);

    connect(m_tabBar, &QTabBar::currentChanged, this, &CurveViewer::onTabChanged);

    // 连接所有图表的区间选取信号
    for (auto &tabCharts : m_charts)
        for (auto *c : tabCharts)
            connect(c, &ScopeChart::rangeSelected, this, &CurveViewer::onRangeSelected);

    updateConfigPanel();
}

// ============================================================
//  左侧边栏：加载按钮 + 状态信息 + 3组选择器
// ============================================================
QWidget *CurveViewer::createConfigPanel()
{
    auto *scrollArea = new QScrollArea;
    scrollArea->setFixedWidth(210);
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
    panel->setObjectName("sidePanel");
    panel->setStyleSheet(SIDEBAR_STYLE);

    auto *vlay = new QVBoxLayout(panel);
    vlay->setContentsMargins(12, 12, 12, 12);
    vlay->setSpacing(6);

    // ---- 加载CSV按钮 ----
    m_loadBtn = new QPushButton("加载 CSV 数据");
    m_loadBtn->setStyleSheet(R"(
        QPushButton {
            background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #004060,stop:1 #006080);
            color: #00d4ff; border: 1px solid #00a0c0; border-radius: 4px;
            padding: 8px 0; font-family: "Microsoft YaHei"; font-size: 12px; font-weight: bold;
        }
        QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #005080,stop:1 #0080a0); }
        QPushButton:pressed { background: #003050; }
    )");
    connect(m_loadBtn, &QPushButton::clicked, this, &CurveViewer::loadCsvFile);
    vlay->addWidget(m_loadBtn);
    vlay->addSpacing(4);

    auto *exportBtn = new QPushButton("导出当前图表");
    exportBtn->setStyleSheet(R"(
        QPushButton {
            background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #403000,stop:1 #605000);
            color: #ffc040; border: 1px solid #a08020; border-radius: 4px;
            padding: 8px 0; font-family: "Microsoft YaHei"; font-size: 12px; font-weight: bold;
        }
        QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #504000,stop:1 #706000); }
        QPushButton:pressed { background: #302000; }
    )");
    connect(exportBtn, &QPushButton::clicked, this, &CurveViewer::exportCurrentCharts);
    vlay->addWidget(exportBtn);

    vlay->addSpacing(6);
    m_fileLabel = new QLabel("未加载文件");
    m_fileLabel->setStyleSheet("color: #6090b0; font-size: 10px;");
    m_fileLabel->setAlignment(Qt::AlignCenter);
    m_fileLabel->setWordWrap(true);
    vlay->addWidget(m_fileLabel);

    m_totalTimeLabel = new QLabel("总时长: --");
    m_totalTimeLabel->setStyleSheet("color: #6090b0; font-size: 11px;");
    m_totalTimeLabel->setAlignment(Qt::AlignCenter);
    vlay->addWidget(m_totalTimeLabel);

    // ---- 状态信息 ----
    const QStringList infoNames = {
        "运行总时间", "算法周期", "通讯周期",
        "运行总次数", "算法版本", "记录周期"
    };
    m_statusValues.clear();

    auto addSep = [&]() {
        vlay->addSpacing(8);
        auto *sep = new QFrame;
        sep->setFrameShape(QFrame::HLine);
        sep->setFixedHeight(1);
        sep->setStyleSheet("QFrame { background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 transparent, stop:0.1 #1a4060, stop:0.9 #1a4060, stop:1 transparent); border: none; }");
        vlay->addWidget(sep);
        vlay->addSpacing(8);
    };

    addSep();

    for (int i = 0; i < infoNames.size(); ++i) {
        auto *row = new QHBoxLayout;
        row->setSpacing(6);
        auto *name = new QLabel(infoNames[i]);
        name->setStyleSheet("color: #6090b0;");
        auto *val = new QLabel("0");
        val->setStyleSheet("color: #00d4ff; font-family: Consolas; font-weight: bold;");
        val->setAlignment(Qt::AlignRight);
        row->addWidget(name);
        row->addStretch();
        row->addWidget(val);
        vlay->addLayout(row);
        m_statusValues.append(val);
    }

    // ---- 选项设置区域（随Tab切换） ----
    addSep();
    m_configStack = new QStackedWidget;
    m_configStack->setStyleSheet("QStackedWidget { background: transparent; border: none; }");

    // Page 0 — 姿态 (下拉选择数据类型 + 轴)
    {
        auto *w = new QWidget;
        auto *l = new QVBoxLayout(w);
        l->setContentsMargins(0,4,0,0);
        l->setSpacing(8);
        auto *title = new QLabel("姿态设置");
        title->setStyleSheet("color: #00d4ff; font-weight: bold; font-size: 13px;");
        l->addWidget(title);

        // --- RPY1 ---
        auto *lbl1 = new QLabel("RPY1");
        lbl1->setStyleSheet("color: #6090b0; font-size: 12px; font-weight: bold;");
        l->addWidget(lbl1);

        auto *r1TypeRow = new QHBoxLayout;
        r1TypeRow->setSpacing(6);
        auto *r1tLbl = new QLabel("类型:");
        r1tLbl->setStyleSheet("color: #6090b0; font-size: 11px;");
        r1tLbl->setFixedWidth(36);
        r1TypeRow->addWidget(r1tLbl);
        m_rpy1TypeCombo = new QComboBox;
        m_rpy1TypeCombo->addItems({"Angle", "A_vel", "Acc"});
        m_rpy1TypeCombo->setMinimumHeight(26);
        applyComboViewStyle(m_rpy1TypeCombo);
        r1TypeRow->addWidget(m_rpy1TypeCombo);
        l->addLayout(r1TypeRow);

        auto *r1AxisRow = new QHBoxLayout;
        r1AxisRow->setSpacing(6);
        auto *r1aLbl = new QLabel("轴:");
        r1aLbl->setStyleSheet("color: #6090b0; font-size: 11px;");
        r1aLbl->setFixedWidth(36);
        r1AxisRow->addWidget(r1aLbl);
        m_rpy1AxisCombo = new QComboBox;
        m_rpy1AxisCombo->addItems({"Roll", "Pitch", "Yaw"});
        m_rpy1AxisCombo->setMinimumHeight(26);
        applyComboViewStyle(m_rpy1AxisCombo);
        r1AxisRow->addWidget(m_rpy1AxisCombo);
        l->addLayout(r1AxisRow);

        l->addSpacing(6);

        // --- RPY2 ---
        auto *lbl2 = new QLabel("RPY2");
        lbl2->setStyleSheet("color: #6090b0; font-size: 12px; font-weight: bold;");
        l->addWidget(lbl2);

        auto *r2TypeRow = new QHBoxLayout;
        r2TypeRow->setSpacing(6);
        auto *r2tLbl = new QLabel("类型:");
        r2tLbl->setStyleSheet("color: #6090b0; font-size: 11px;");
        r2tLbl->setFixedWidth(36);
        r2TypeRow->addWidget(r2tLbl);
        m_rpy2TypeCombo = new QComboBox;
        m_rpy2TypeCombo->addItems({"Angle", "A_vel", "Acc"});
        m_rpy2TypeCombo->setCurrentIndex(1);
        m_rpy2TypeCombo->setMinimumHeight(26);
        applyComboViewStyle(m_rpy2TypeCombo);
        r2TypeRow->addWidget(m_rpy2TypeCombo);
        l->addLayout(r2TypeRow);

        auto *r2AxisRow = new QHBoxLayout;
        r2AxisRow->setSpacing(6);
        auto *r2aLbl = new QLabel("轴:");
        r2aLbl->setStyleSheet("color: #6090b0; font-size: 11px;");
        r2aLbl->setFixedWidth(36);
        r2AxisRow->addWidget(r2aLbl);
        m_rpy2AxisCombo = new QComboBox;
        m_rpy2AxisCombo->addItems({"Roll", "Pitch", "Yaw"});
        m_rpy2AxisCombo->setMinimumHeight(26);
        applyComboViewStyle(m_rpy2AxisCombo);
        r2AxisRow->addWidget(m_rpy2AxisCombo);
        l->addLayout(r2AxisRow);

        l->addSpacing(6);
        auto *footLbl = new QLabel("足底力: 暂无数据");
        footLbl->setStyleSheet("color: #4a6a80; font-size: 11px;");
        l->addWidget(footLbl);

        l->addStretch();
        m_configStack->addWidget(w);

        // 类型切换时更新轴选项
        auto updateAxisCombo = [](QComboBox *typeCb, QComboBox *axisCb) {
            int ti = typeCb->currentIndex();
            axisCb->blockSignals(true);
            axisCb->clear();
            if (ti == 2) // Acc
                axisCb->addItems({"X", "Y", "Z"});
            else
                axisCb->addItems({"Roll", "Pitch", "Yaw"});
            axisCb->blockSignals(false);
        };

        connect(m_rpy1TypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, updateAxisCombo]() {
            updateAxisCombo(m_rpy1TypeCombo, m_rpy1AxisCombo);
            onConfigChanged();
        });
        connect(m_rpy1AxisCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &CurveViewer::onConfigChanged);
        connect(m_rpy2TypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, updateAxisCombo]() {
            updateAxisCombo(m_rpy2TypeCombo, m_rpy2AxisCombo);
            onConfigChanged();
        });
        connect(m_rpy2AxisCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &CurveViewer::onConfigChanged);
    }

    // Page 1 — 关节
    {
        auto *w = new QWidget;
        auto *l = new QVBoxLayout(w);
        l->setContentsMargins(0,4,0,0);
        l->setSpacing(8);
        auto *title = new QLabel("关节设置");
        title->setStyleSheet("color: #00d4ff; font-weight: bold; font-size: 13px;");
        l->addWidget(title);

        auto *legRow = new QHBoxLayout;
        legRow->setSpacing(6);
        auto *legLbl = new QLabel("腿:");
        legLbl->setStyleSheet("color: #6090b0; font-size: 12px;");
        legLbl->setFixedWidth(40);
        legRow->addWidget(legLbl);
        m_jointLegCombo = new QComboBox;
        m_jointLegCombo->addItems({"FL (前左)", "FR (前右)", "HL (后左)", "HR (后右)"});
        m_jointLegCombo->setMinimumHeight(26);
        applyComboViewStyle(m_jointLegCombo);
        legRow->addWidget(m_jointLegCombo);
        l->addLayout(legRow);

        auto *jRow = new QHBoxLayout;
        jRow->setSpacing(6);
        auto *jLbl = new QLabel("关节:");
        jLbl->setStyleSheet("color: #6090b0; font-size: 12px;");
        jLbl->setFixedWidth(40);
        jRow->addWidget(jLbl);
        m_jointJointCombo = new QComboBox;
        m_jointJointCombo->addItems({"HipX", "HipY", "Knee"});
        m_jointJointCombo->setMinimumHeight(26);
        applyComboViewStyle(m_jointJointCombo);
        jRow->addWidget(m_jointJointCombo);
        l->addLayout(jRow);

        l->addStretch();
        m_configStack->addWidget(w);

        connect(m_jointLegCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &CurveViewer::onConfigChanged);
        connect(m_jointJointCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &CurveViewer::onConfigChanged);
    }

    // Page 2 — 算法 (3 组 Scope 列号设置)
    {
        auto *w = new QWidget;
        auto *l = new QVBoxLayout(w);
        l->setContentsMargins(0,4,0,0);
        l->setSpacing(6);

        for (int i = 0; i < 3; ++i) {
            auto *stitle = new QLabel(QString("Scope %1 设置").arg(i + 1));
            stitle->setStyleSheet("color: #00d4ff; font-weight: bold; font-size: 12px;");
            l->addWidget(stitle);

            auto *r1 = new QHBoxLayout;
            r1->setSpacing(6);
            auto *l1 = new QLabel("曲线1:");
            l1->setStyleSheet("color: #ff6050; font-size: 11px;");
            l1->setFixedWidth(40);
            r1->addWidget(l1);
            m_scopeCol[i][0] = new QSpinBox;
            m_scopeCol[i][0]->setRange(0, 149);
            m_scopeCol[i][0]->setValue(i * 2);
            m_scopeCol[i][0]->setMinimumHeight(24);
            r1->addWidget(m_scopeCol[i][0]);
            l->addLayout(r1);

            auto *r2 = new QHBoxLayout;
            r2->setSpacing(6);
            auto *l2 = new QLabel("曲线2:");
            l2->setStyleSheet("color: #ff6050; font-size: 11px;");
            l2->setFixedWidth(40);
            r2->addWidget(l2);
            m_scopeCol[i][1] = new QSpinBox;
            m_scopeCol[i][1]->setRange(0, 149);
            m_scopeCol[i][1]->setValue(i * 2 + 1);
            m_scopeCol[i][1]->setMinimumHeight(24);
            r2->addWidget(m_scopeCol[i][1]);
            l->addLayout(r2);

            if (i < 2) l->addSpacing(8);

            connect(m_scopeCol[i][0], QOverload<int>::of(&QSpinBox::valueChanged),
                    this, &CurveViewer::onConfigChanged);
            connect(m_scopeCol[i][1], QOverload<int>::of(&QSpinBox::valueChanged),
                    this, &CurveViewer::onConfigChanged);
        }

        l->addStretch();
        m_configStack->addWidget(w);
    }

    // Page 3 — 性能 (固定映射)
    {
        auto *w = new QWidget;
        auto *l = new QVBoxLayout(w);
        l->setContentsMargins(0,0,0,0);
        auto *lbl = new QLabel("固定映射:\n执行时间 · 周期时间 · CPU占用");
        lbl->setStyleSheet("color: #4a6a80; font-size: 11px;");
        lbl->setWordWrap(true);
        l->addWidget(lbl);
        l->addStretch();
        m_configStack->addWidget(w);
    }

    // Page 4 — 温度
    {
        auto *w = new QWidget;
        auto *l = new QVBoxLayout(w);
        l->setContentsMargins(0,4,0,0);
        l->setSpacing(8);
        auto *title = new QLabel("温度设置");
        title->setStyleSheet("color: #00d4ff; font-weight: bold; font-size: 13px;");
        l->addWidget(title);

        auto *jRow = new QHBoxLayout;
        jRow->setSpacing(6);
        auto *jLbl = new QLabel("关节:");
        jLbl->setStyleSheet("color: #6090b0; font-size: 12px;");
        jLbl->setFixedWidth(40);
        jRow->addWidget(jLbl);
        m_tempJointCombo = new QComboBox;
        m_tempJointCombo->addItems({"HipX", "HipY", "Knee"});
        m_tempJointCombo->setMinimumHeight(26);
        applyComboViewStyle(m_tempJointCombo);
        jRow->addWidget(m_tempJointCombo);
        l->addLayout(jRow);

        l->addStretch();
        m_configStack->addWidget(w);

        connect(m_tempJointCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &CurveViewer::onConfigChanged);
    }

    // Page 5 — 状态 (固定映射)
    {
        auto *w = new QWidget;
        auto *l = new QVBoxLayout(w);
        l->setContentsMargins(0,0,0,0);
        auto *lbl = new QLabel("固定映射:\nECAT周期 · 设备周期");
        lbl->setStyleSheet("color: #4a6a80; font-size: 11px;");
        lbl->setWordWrap(true);
        l->addWidget(lbl);
        l->addStretch();
        m_configStack->addWidget(w);
    }

    vlay->addWidget(m_configStack);

    vlay->addStretch();
    scrollArea->setWidget(panel);
    return scrollArea;
}

// ---- 图表页面辅助 ----
static QWidget *makeChartPage(ScopeChart *c1, ScopeChart *c2, ScopeChart *c3)
{
    auto *page = new QWidget;
    page->setStyleSheet("QWidget { background: #0a0e14; border: none; }");
    auto *splitter = new QSplitter(Qt::Vertical, page);
    splitter->setHandleWidth(6);
    splitter->setStyleSheet(SPLITTER_STYLE);
    splitter->addWidget(c1);
    splitter->addWidget(c2);
    splitter->addWidget(c3);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 1);
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(4, 2, 4, 2);
    lay->setSpacing(0);
    lay->addWidget(splitter);
    return page;
}

static QWidget *makeChartPage2(ScopeChart *c1, ScopeChart *c2)
{
    auto *page = new QWidget;
    page->setStyleSheet("QWidget { background: #0a0e14; border: none; }");
    auto *splitter = new QSplitter(Qt::Vertical, page);
    splitter->setHandleWidth(6);
    splitter->setStyleSheet(SPLITTER_STYLE);
    splitter->addWidget(c1);
    splitter->addWidget(c2);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(4, 2, 4, 2);
    lay->setSpacing(0);
    lay->addWidget(splitter);
    return page;
}

// ============================================================
//  各Tab页
// ============================================================
QWidget *CurveViewer::createAttitudePage()
{
    auto *c1 = new ScopeChart("RPY1", {{"RPY1", CYAN, -1}});
    auto *c2 = new ScopeChart("RPY2", {{"RPY2", GREEN, -1}});
    auto *c3 = new ScopeChart("足底力",  {{"F1", CYAN, -1}, {"F2", GREEN, -1}, {"F3", YELLOW, -1}});
    m_charts[0] = {c1, c2, c3};
    return makeChartPage(c1, c2, c3);
}

QWidget *CurveViewer::createJointPage()
{
    auto *c1 = new ScopeChart("关节角度", {{"实际", ORANGE, -1}, {"期望", CYAN, -1}});
    auto *c2 = new ScopeChart("关节速度", {{"实际", ORANGE, -1}, {"期望", CYAN, -1}});
    auto *c3 = new ScopeChart("关节力矩", {{"实际", ORANGE, -1}, {"期望", CYAN, -1}});
    m_charts[1] = {c1, c2, c3};
    return makeChartPage(c1, c2, c3);
}

QWidget *CurveViewer::createAlgorithmPage()
{
    auto *c1 = new ScopeChart("Scope 1", {{"CH 1", CYAN, -1}, {"CH 2", GREEN, -1}});
    auto *c2 = new ScopeChart("Scope 2", {{"CH 1", YELLOW, -1}, {"CH 2", ORANGE, -1}});
    auto *c3 = new ScopeChart("Scope 3", {{"CH 1", PURPLE, -1}, {"CH 2", PINK, -1}});
    m_charts[2] = {c1, c2, c3};
    return makeChartPage(c1, c2, c3);
}

QWidget *CurveViewer::createPerformancePage()
{
    auto *c1 = new ScopeChart("执行时间",   {{"周期", CYAN, -1}});
    auto *c2 = new ScopeChart("周期时间",   {{"周期", GREEN, -1}});
    auto *c3 = new ScopeChart("CPU 占用率", {{"CPU%", YELLOW, -1}});
    m_charts[3] = {c1, c2, c3};
    return makeChartPage(c1, c2, c3);
}

QWidget *CurveViewer::createTemperaturePage()
{
    auto *c1 = new ScopeChart("驱动电压", {
        {"FL", CYAN, -1}, {"FR", RED, -1}, {"HL", GREEN, -1}, {"HR", YELLOW, -1}
    });
    auto *c2 = new ScopeChart("驱动器温度", {
        {"FL", CYAN, -1}, {"FR", RED, -1}, {"HL", GREEN, -1}, {"HR", YELLOW, -1}
    });
    auto *c3 = new ScopeChart("电机温度", {
        {"FL", CYAN, -1}, {"FR", RED, -1}, {"HL", GREEN, -1}, {"HR", YELLOW, -1}
    });
    m_charts[4] = {c1, c2, c3};
    return makeChartPage(c1, c2, c3);
}

QWidget *CurveViewer::createStatusPage()
{
    auto *c1 = new ScopeChart("ECAT 周期", {
        {"平均", CYAN, -1}, {"最大", RED, -1}, {"最小", GREEN, -1}
    });
    auto *c2 = new ScopeChart("设备周期", {
        {"Gyro", CYAN, -1}, {"Heat", RED, -1}, {"Foot", GREEN, -1}
    });
    m_charts[5] = {c1, c2};
    return makeChartPage2(c1, c2);
}

// ============================================================
//  底部时间控制栏
// ============================================================
QWidget *CurveViewer::createTimeControls()
{
    auto *bar = new QWidget;
    bar->setObjectName("timeBar");
    bar->setFixedHeight(72);
    bar->setStyleSheet(R"(
        QWidget#timeBar { background: #080c12; border-top: 1px solid #1a3050; }
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

    lay->addWidget(new QLabel("起始时间:"));
    m_timeStart = new QDoubleSpinBox;
    m_timeStart->setRange(0, 99999);
    m_timeStart->setValue(0);
    m_timeStart->setSuffix(" s");
    m_timeStart->setDecimals(2);
    m_timeStart->setSingleStep(1.0);
    m_timeStart->setMinimumWidth(100);
    lay->addWidget(m_timeStart);

    lay->addWidget(new QLabel("窗口宽度:"));
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

    auto *resetBtn = new QPushButton("自适应");
    resetBtn->setStyleSheet(R"(
        QPushButton {
            background: #0c1824; color: #00d4ff;
            border: 1px solid #1a4060; border-radius: 3px;
            padding: 4px 14px; font-family: "Microsoft YaHei"; font-size: 10px;
        }
        QPushButton:hover { border-color: #00d4ff; background: #102030; }
    )");
    connect(resetBtn, &QPushButton::clicked, this, [this]() {
        int idx = m_tabBar->currentIndex();
        if (idx >= 0 && idx < m_charts.size()) {
            for (auto *c : m_charts[idx])
                c->rescaleAndReplot();
        }
    });
    lay->addWidget(resetBtn);

    connect(m_timeStart, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &CurveViewer::onTimeRangeChanged);
    connect(m_windowSize, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &CurveViewer::onTimeRangeChanged);
    connect(m_rangeSlider, &RangeSlider::valuesChanged,
            this, &CurveViewer::onRangeSliderChanged);
    return bar;
}

// ============================================================
//  列映射初始化（默认值，用户加载真实CSV后可通过侧栏调整）
// ============================================================
void CurveViewer::initColumnMap()
{
    m_colMap.resize(6);
    // 姿态: RPY1(1曲线) + RPY2(1曲线) + 足底力(3曲线)
    m_colMap[0] = {{-1}, {-1}, {-1,-1,-1}};
    // 关节: 3 charts × 2 curves (IN/OUT)
    m_colMap[1] = {{-1,-1}, {-1,-1}, {-1,-1}};
    // 算法: 3 charts × 2 curves
    m_colMap[2] = {{-1,-1}, {-1,-1}, {-1,-1}};
    // 性能: 3 charts × 1 curve
    m_colMap[3] = {{-1}, {-1}, {-1}};
    // 温度: 3 charts × 4 curves
    m_colMap[4] = {{-1,-1,-1,-1}, {-1,-1,-1,-1}, {-1,-1,-1,-1}};
    // 状态: 2 charts × 3 curves
    m_colMap[5] = {{-1,-1,-1}, {-1,-1,-1}};
}

void CurveViewer::buildColumnMapForTab(int tabIdx)
{
    if (m_csvData.colCount == 0) return;
    if (tabIdx < 0 || tabIdx >= m_colMap.size()) return;

    auto fc = [this](const QString &name) { return m_csvData.findColumn(name); };

    switch (tabIdx) {
    case 0: { // 姿态 — 根据2组下拉框确定单列
        // 列名表: [type][axis]
        static const char *colNames[3][3] = {
            {"Roll (deg)",       "Pitch (deg)",       "Yaw (deg)"},
            {"Roll_Vel (rad/s)", "Pitch_Vel (rad/s)", "Yaw_Vel (rad/s)"},
            {"X_Acc (m/s^2)",    "Y_Acc (m/s^2)",     "Z_Acc (m/s^2)"}
        };
        int t1 = m_rpy1TypeCombo ? m_rpy1TypeCombo->currentIndex() : 0;
        int a1 = m_rpy1AxisCombo ? m_rpy1AxisCombo->currentIndex() : 0;
        int t2 = m_rpy2TypeCombo ? m_rpy2TypeCombo->currentIndex() : 1;
        int a2 = m_rpy2AxisCombo ? m_rpy2AxisCombo->currentIndex() : 0;
        t1 = qBound(0, t1, 2); a1 = qBound(0, a1, 2);
        t2 = qBound(0, t2, 2); a2 = qBound(0, a2, 2);
        m_colMap[0] = { {fc(colNames[t1][a1])}, {fc(colNames[t2][a2])}, {-1, -1, -1} };
        break;
    }

    case 1: { // 关节
        static const char *legs[]   = {"FL", "FR", "HL", "HR"};
        static const char *joints[] = {"HipX", "HipY", "Knee"};
        int leg = m_jointLegCombo   ? m_jointLegCombo->currentIndex()   : 0;
        int jnt = m_jointJointCombo ? m_jointJointCombo->currentIndex() : 0;
        QString L = legs[leg];
        QString J = joints[jnt];

        m_colMap[1] = {
            {fc(QString("IN_%1_%2_Ang (rad)").arg(L, J)),
             fc(QString("OUT_%1_%2_Ang (rad)").arg(L, J))},
            {fc(QString("IN_%1_%2_Vel (rad/s)").arg(L, J)),
             fc(QString("OUT_%1_%2_Vel (rad/s)").arg(L, J))},
            {fc(QString("IN_%1_%2_Tor (Nm)").arg(L, J)),
             fc(QString("OUT_%1_%2_Tor (Nm)").arg(L, J))}
        };
        break;
    }

    case 2: { // 算法 — Scope 列号
        int scopeBase = fc("Scope_0");
        if (scopeBase < 0) break;
        for (int i = 0; i < 3; ++i) {
            int c1 = m_scopeCol[i][0] ? m_scopeCol[i][0]->value() : (i * 2);
            int c2 = m_scopeCol[i][1] ? m_scopeCol[i][1]->value() : (i * 2 + 1);
            m_colMap[2][i] = {scopeBase + c1, scopeBase + c2};
        }
        break;
    }

    case 3: // 性能
        m_colMap[3] = {
            {fc("Cost_Time (us)")},
            {fc("Alg_Cycle_Time (us)")},
            {fc("CPU_Rate (%)")}
        };
        break;

    case 4: { // 温度
        static const char *legs[]   = {"FL", "FR", "HL", "HR"};
        static const char *joints[] = {"HipX", "HipY", "Knee"};
        int jnt = m_tempJointCombo ? m_tempJointCombo->currentIndex() : 0;
        QString J = joints[jnt];

        QVector<int> voltage, driveTemp, motorTemp;
        for (int l = 0; l < 4; ++l) {
            voltage.append(fc(QString("%1_%2_Drive_Voltage (V)").arg(legs[l], J)));
            driveTemp.append(fc(QString("%1_%2_Drive_Temp (C)").arg(legs[l], J)));
            motorTemp.append(fc(QString("%1_%2_Motor_Temp (C)").arg(legs[l], J)));
        }
        m_colMap[4] = {voltage, driveTemp, motorTemp};
        break;
    }

    case 5: // 状态
        m_colMap[5] = {
            {fc("Ecat_Avg_Time (us)"), fc("Ecat_Max_Time (us)"), fc("Ecat_Min_Time (us)")},
            {fc("Contact_1"), fc("Contact_2"), fc("Contact_3")}
        };
        break;
    }
}

// ============================================================
//  CSV 加载
// ============================================================
void CurveViewer::loadCsvFile()
{
    QString path = QFileDialog::getOpenFileName(
        this, "选择CSV数据文件", QString(), "CSV/Snapshot (*.csv *.snapshot.csv);;所有文件 (*)");
    if (path.isEmpty()) return;
    loadCsvFromPath(path);
}

void CurveViewer::loadCsvFromPath(const QString &path)
{
    if (!QFileInfo::exists(path)) return;

    // 加载提示弹窗
    auto *dlg = new QProgressDialog(this);
    dlg->setWindowTitle("加载数据");
    dlg->setLabelText(QString("正在加载: %1").arg(QFileInfo(path).fileName()));
    dlg->setRange(0, 100);
    dlg->setValue(0);
    dlg->setCancelButton(nullptr);
    dlg->setMinimumDuration(0);
    dlg->setWindowModality(Qt::WindowModal);
    dlg->setStyleSheet(R"(
        QProgressDialog { background: #0a0e14; }
        QLabel { color: #00d4ff; font-size: 12px; font-family: "Microsoft YaHei"; }
        QProgressBar {
            background: #0c1824; border: 1px solid #1a4060; border-radius: 3px;
            text-align: center; color: #00d4ff; font-size: 10px;
        }
        QProgressBar::chunk { background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #005888, stop:1 #00d4ff); border-radius: 2px; }
    )");
    dlg->show();
    QApplication::processEvents();

    // 字节级解析（见 csvparser.cpp），语义与旧实现完全一致但快数倍
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

    if (!ok || m_csvData.colCount == 0) {
        // 必须先关掉进度框，否则空文件会留下一个永不消失的模态窗口，整个界面卡死
        dlg->close();
        dlg->deleteLater();
        return;
    }

    const bool isSnapshot = m_csvData.isSnapshot;

    // ---- 更新 UI ----
    double total = m_csvData.totalTime();
    // 如果有 Timestamp 列，用其最后值作为总时长
    int tsCol = m_csvData.findColumn("Timestamp (s)");
    if (tsCol >= 0 && m_csvData.rowCount > 0) {
        double lastTs = m_csvData.columns[tsCol].last();
        double firstTs = m_csvData.columns[tsCol].first();
        total = lastTs - firstTs;
    }

    // 确定时间轴的真实起止范围
    double tsStart = 0.0, tsEnd = total;
    if (tsCol >= 0 && m_csvData.rowCount > 0) {
        tsStart = m_csvData.columns[tsCol].first();
        tsEnd   = m_csvData.columns[tsCol].last();
    }
    double windowW = qMin(10.0, tsEnd - tsStart);

    m_fileLabel->setText(QFileInfo(path).fileName());
    m_totalTimeLabel->setText(QString("总时长: %1 s | %2行×%3列")
                                  .arg(total, 0, 'f', 1)
                                  .arg(m_csvData.rowCount)
                                  .arg(m_csvData.colCount));

    m_rangeSlider->setRange(tsStart, tsEnd);
    m_rangeSlider->setValues(tsStart, tsStart + windowW);
    m_timeStart->setRange(tsStart, qMax(tsStart, tsEnd));
    m_windowSize->setRange(0.1, qMax(0.1, tsEnd - tsStart));
    m_timeStart->setValue(tsStart);
    m_windowSize->setValue(windowW);

    // ---- 状态信息（从snapshot info行获取） ----
    if (m_statusValues.size() >= 6) {
        if (isSnapshot && m_csvData.infoValues.size() >= 6) {
            m_statusValues[0]->setText(QString("%1 s").arg(m_csvData.infoValues[0], 0, 'f', 1));
            m_statusValues[1]->setText(QString("%1 us").arg(m_csvData.infoValues[1], 0, 'f', 0));
            m_statusValues[2]->setText(QString("%1 us").arg(m_csvData.infoValues[2], 0, 'f', 0));
            m_statusValues[3]->setText(QString::number(qRound(m_csvData.infoValues[3])));
            m_statusValues[4]->setText(QString::number(qRound(m_csvData.infoValues[4])));
            m_statusValues[5]->setText(QString::number(qRound(m_csvData.infoValues[5])));
        } else {
            m_statusValues[0]->setText(QString("%1 s").arg(total, 0, 'f', 1));
            m_statusValues[1]->setText(QString("%1 ms").arg(1000.0 / m_csvData.sampleRate, 0, 'f', 1));
            m_statusValues[2]->setText("--");
            m_statusValues[3]->setText(QString::number(m_csvData.rowCount));
            m_statusValues[4]->setText("--");
            m_statusValues[5]->setText(QString("%1 Hz").arg(m_csvData.sampleRate));
        }
    }

    // 构建所有Tab的列映射
    for (int t = 0; t < 6; ++t)
        buildColumnMapForTab(t);

    updateConfigPanel();
    refreshCurrentTab();

    dlg->close();
    dlg->deleteLater();

    emit csvLoaded(path, m_csvData.rowCount, m_csvData.colCount);
}

// ============================================================
//  图表刷新
// ============================================================
void CurveViewer::refreshCurrentTab()
{
    if (m_csvData.rowCount == 0) return;

    int tabIdx = m_tabBar->currentIndex();
    if (tabIdx < 0 || tabIdx >= m_charts.size()) return;
    if (tabIdx >= m_colMap.size()) return;

    auto &charts = m_charts[tabIdx];
    auto &tabMap = m_colMap[tabIdx];

    // 使用 Timestamp 列作为 X 轴，否则计算
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
    const double w  = m_windowSize->value();

    // 整段装载期间挂起重绘：原来每张图会经历 clearData/rescale/setTimeRange 三次
    // 全量 replot，大数据量下这是切 Tab 卡顿的主因，现在只在最后重绘一次。
    for (int ci = 0; ci < charts.size() && ci < tabMap.size(); ++ci) {
        ScopeChart *chart = charts[ci];
        chart->setReplotSuspended(true);
        chart->clearData();
        const auto &colIndices = tabMap[ci];
        for (int curveIdx = 0; curveIdx < colIndices.size(); ++curveIdx) {
            int csvCol = colIndices[curveIdx];
            if (csvCol >= 0 && csvCol < m_csvData.colCount)
                chart->setData(curveIdx, timeVec, m_csvData.columns[csvCol],
                               m_csvData.timeSorted);
        }
        chart->rescaleAndReplot();
        chart->setTimeRange(t0, t0 + w);
        chart->setReplotSuspended(false);
    }
}

// ============================================================
//  侧栏配置 <-> 列映射 同步
// ============================================================
void CurveViewer::updateConfigPanel()
{
    int tabIdx = m_tabBar->currentIndex();
    if (m_configStack && tabIdx >= 0 && tabIdx < m_configStack->count())
        m_configStack->setCurrentIndex(tabIdx);
}

void CurveViewer::onConfigChanged()
{
    int tabIdx = m_tabBar->currentIndex();
    if (tabIdx < 0 || tabIdx >= m_colMap.size()) return;

    // 保存旧映射，比较差异后只刷新变化的图表
    auto oldMap = m_colMap[tabIdx];
    buildColumnMapForTab(tabIdx);

    if (m_csvData.rowCount == 0) return;
    auto &charts = m_charts[tabIdx];
    auto &tabMap = m_colMap[tabIdx];

    // 构建时间向量
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

    double t0 = m_timeStart->value();
    double w  = m_windowSize->value();

    for (int ci = 0; ci < charts.size() && ci < tabMap.size(); ++ci) {
        // 只刷新映射发生变化的图表
        if (ci < oldMap.size() && oldMap[ci] == tabMap[ci])
            continue;
        ScopeChart *chart = charts[ci];
        chart->setReplotSuspended(true);
        chart->clearData();
        const auto &colIndices = tabMap[ci];
        for (int curveIdx = 0; curveIdx < colIndices.size(); ++curveIdx) {
            int csvCol = colIndices[curveIdx];
            if (csvCol >= 0 && csvCol < m_csvData.colCount)
                chart->setData(curveIdx, timeVec, m_csvData.columns[csvCol],
                               m_csvData.timeSorted);
        }
        chart->rescaleAndReplot();
        chart->setTimeRange(t0, t0 + w);
        chart->setReplotSuspended(false);
    }
}

// ============================================================
//  Tab / 时间 槽
// ============================================================
void CurveViewer::onTabChanged(int index)
{
    m_stack->setCurrentIndex(index);
    updateConfigPanel();
    buildColumnMapForTab(index);
    refreshCurrentTab();
}

void CurveViewer::onTimeRangeChanged()
{
    double t0 = m_timeStart->value();
    double w  = m_windowSize->value();
    m_rangeSlider->blockSignals(true);
    m_rangeSlider->setValues(t0, t0 + w);
    m_rangeSlider->blockSignals(false);
    int idx = m_tabBar->currentIndex();
    if (idx >= 0 && idx < m_charts.size())
        for (auto *c : m_charts[idx])
            c->setTimeRange(t0, t0 + w);
}

void CurveViewer::onRangeSliderChanged(double lower, double upper)
{
    m_timeStart->blockSignals(true);
    m_windowSize->blockSignals(true);
    m_timeStart->setValue(lower);
    m_windowSize->setValue(upper - lower);
    m_timeStart->blockSignals(false);
    m_windowSize->blockSignals(false);
    int idx = m_tabBar->currentIndex();
    if (idx >= 0 && idx < m_charts.size())
        for (auto *c : m_charts[idx])
            c->setTimeRange(lower, upper);
}

// ============================================================
//  图表导出
// ============================================================
void CurveViewer::exportCurrentCharts()
{
    int tabIdx = m_tabBar->currentIndex();
    if (tabIdx < 0 || tabIdx >= m_charts.size() || m_charts[tabIdx].isEmpty()) {
        QMessageBox::information(this, "导出", "当前无可导出的图表");
        return;
    }

    QString path = QFileDialog::getSaveFileName(
        this, "导出图表为图片", QString(), "PNG图片 (*.png);;JPG图片 (*.jpg);;PDF文档 (*.pdf)");
    if (path.isEmpty()) return;

    auto &charts = m_charts[tabIdx];

    // 导出前隐藏tooltip等浮动元素
    for (auto *c : charts) {
        c->plot()->replot(QCustomPlot::rpImmediateRefresh);
    }

    // 用每个 ScopeChart 的 grab() 截取完整显示内容（含图例）
    QVector<QPixmap> pixmaps;
    int totalW = 0, totalH = 0;
    for (auto *c : charts) {
        QPixmap px = c->grab();
        pixmaps.append(px);
        totalW = qMax(totalW, px.width());
        totalH += px.height();
    }

    QPixmap combined(totalW, totalH);
    combined.fill(QColor(10, 14, 20));

    QPainter painter(&combined);
    int yOff = 0;
    for (const auto &px : pixmaps) {
        painter.drawPixmap(0, yOff, px);
        yOff += px.height();
    }
    painter.end();

    if (path.endsWith(".pdf", Qt::CaseInsensitive)) {
        // 原实现调用 charts[0]->plot()->savePdf(...)，只会导出第一个图表并拉伸到
        // 拼接后的尺寸。改为把与 PNG 完全一致的拼接图写入 PDF，保证内容正确。
        QPdfWriter writer(path);
        writer.setResolution(96);
        writer.setPageSize(QPageSize(QSizeF(combined.width() / 96.0,
                                            combined.height() / 96.0),
                                     QPageSize::Inch));
        writer.setPageMargins(QMarginsF(0, 0, 0, 0));
        QPainter pdfPainter(&writer);
        pdfPainter.drawPixmap(0, 0, combined);
        pdfPainter.end();
    } else if (path.endsWith(".jpg", Qt::CaseInsensitive) || path.endsWith(".jpeg", Qt::CaseInsensitive)) {
        combined.save(path, "JPG", 95);
    } else {
        combined.save(path, "PNG");
    }

    QMessageBox::information(this, "导出完成",
        QString("图表已保存至:\n%1").arg(path));
}

// ============================================================
//  播放光标联动
// ============================================================
void CurveViewer::setPlaybackTime(double timeSec)
{
    if (!isVisible()) return;
    for (auto &tabCharts : m_charts)
        for (auto *c : tabCharts)
            c->setPlaybackCursor(timeSec);
}

void CurveViewer::clearPlaybackCursor()
{
    for (auto &tabCharts : m_charts)
        for (auto *c : tabCharts)
            c->clearPlaybackCursor();
}

// ============================================================
//  区间选取 — 右键拖拽快速缩放
// ============================================================
void CurveViewer::onRangeSelected(double tMin, double tMax)
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

    // 更新当前Tab所有图表的显示范围
    int idx = m_tabBar->currentIndex();
    if (idx >= 0 && idx < m_charts.size())
        for (auto *c : m_charts[idx])
            c->setTimeRange(tMin, tMax);
}
