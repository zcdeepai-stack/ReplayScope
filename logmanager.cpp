#include "logmanager.h"
#include "sshclient.h"
#include "logcollectwidget.h"
#include "loganalysiswidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDateTime>

static const char *TAB_STYLE = R"(
    QTabWidget::pane { border: 1px solid #21262d; border-radius: 4px; background: #0d1117; }
    QTabBar::tab { background: #161b22; border: 1px solid #21262d; border-bottom: none;
                   border-top-left-radius: 4px; border-top-right-radius: 4px;
                   padding: 6px 18px; color: #8b949e; margin-right: 2px; }
    QTabBar::tab:selected { background: #0d1117; color: #58a6ff; border-bottom: 2px solid #58a6ff; }
    QTabBar::tab:hover { color: #c9d1d9; }
)";

LogManager::LogManager(QWidget *parent)
    : QWidget(parent)
{
    setStyleSheet("background: #0d1117;");

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);

    // 顶部栏：标题 + 实时时钟
    auto *topBar = new QHBoxLayout;
    auto *titleLabel = new QLabel("日志管理系统");
    titleLabel->setStyleSheet(
        "color: #58a6ff; font-size: 16px; font-weight: bold; padding: 4px 0;");
    m_clockLabel = new QLabel;
    m_clockLabel->setStyleSheet(
        "color: #8b949e; font-size: 14px; font-family: 'Consolas', 'Courier New', monospace;"
        "padding: 4px 12px; background: #161b22; border: 1px solid #21262d; border-radius: 4px;");
    topBar->addWidget(titleLabel);
    topBar->addStretch();
    topBar->addWidget(m_clockLabel);
    layout->addLayout(topBar);

    // 实时时钟更新
    m_clockTimer = new QTimer(this);
    connect(m_clockTimer, &QTimer::timeout, this, [this]() {
        m_clockLabel->setText(QDateTime::currentDateTime().toString("yyyy-MM-dd  HH:mm:ss"));
    });
    m_clockTimer->start(1000);
    m_clockLabel->setText(QDateTime::currentDateTime().toString("yyyy-MM-dd  HH:mm:ss"));

    m_tabWidget = new QTabWidget;
    m_tabWidget->setStyleSheet(TAB_STYLE);

    // SSH 客户端 (共享)
    m_sshClient = new SshClient(this);

    // Tab 1: 日志收集
    m_collectWidget = new LogCollectWidget(m_sshClient);
    m_tabWidget->addTab(m_collectWidget, "日志收集");

    // Tab 2: 日志分析
    m_analysisWidget = new LogAnalysisWidget;
    m_tabWidget->addTab(m_analysisWidget, "日志分析");

    // 导出完成信号（保留信号但不自动切换 tab）

    layout->addWidget(m_tabWidget);
}

void LogManager::switchToAnalysisTab()
{
    m_tabWidget->setCurrentIndex(1);
}

LogManager::~LogManager()
{
    m_sshClient->stopHelper();
}
