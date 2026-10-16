#include "logcollectwidget.h"
#include "sshclient.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QHeaderView>
#include <QFileDialog>
#include <QDialog>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QCheckBox>
#include <QTextCursor>
#include <QTextBlock>
#include <QRegularExpression>
#include <QMenu>
#include <QPlainTextEdit>
#include <QApplication>
#include <QScreen>

static const char *DARK_STYLE = R"(
    QWidget { background: #0d1117; color: #c9d1d9; font-size: 13px; }
    QGroupBox { border: 1px solid #30363d; border-radius: 8px; margin-top: 16px;
                padding-top: 24px; font-weight: bold; background: #161b22; }
    QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 4px 16px;
                       background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #1f6feb,stop:1 #388bfd);
                       color: #ffffff; border-radius: 4px;
                       font-size: 13px; font-weight: bold; }
    QPushButton { background: #21262d; border: 1px solid #30363d; border-radius: 5px;
                  padding: 6px 14px; color: #c9d1d9; font-size: 12px; }
    QPushButton:hover { background: #30363d; border-color: #58a6ff; }
    QPushButton:pressed { background: #1c2128; }
    QPushButton:disabled { color: #484f58; background: #161b22; }
    QTreeWidget, QTableWidget, QTextEdit, QListWidget {
        background: transparent; border: 1px solid #21262d; border-radius: 6px;
        alternate-background-color: rgba(22,27,34,0.5); }
    QHeaderView::section { background: #161b22; border: none; padding: 6px 8px;
                           font-size: 13px; color: #8b949e; font-weight: bold; }
    QLineEdit, QComboBox { background: #0d1117; border: 1px solid #30363d;
                           border-radius: 4px; padding: 5px 8px; color: #c9d1d9; }
    QLineEdit:focus, QComboBox:focus { border-color: #58a6ff; }
    QProgressBar { background: #21262d; border: 1px solid #30363d; border-radius: 4px;
                   text-align: center; color: #c9d1d9; min-height: 20px; }
    QProgressBar::chunk { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #238636,stop:1 #3fb950);
                          border-radius: 3px; }
    QCheckBox::indicator { width: 16px; height: 16px; border: 2px solid #30363d;
                           border-radius: 3px; background: #161b22; }
    QCheckBox::indicator:hover { border-color: #58a6ff; }
    QCheckBox::indicator:checked { background: #238636; border-color: #3fb950; }
    QTreeWidget::item { min-height: 30px; padding: 4px 0; }
    QTreeWidget::item:selected { background: rgba(56,139,253,0.25); color: #58a6ff; }
    QTableWidget::item:selected { background: rgba(56,139,253,0.2); }
    QSplitter::handle { background: #21262d; width: 2px; }
    QLabel { font-size: 13px; }
)";

// 把字符串安全地放进远程 shell 的单引号里：文件名或搜索关键字带 ' 时，
// 原来的裸拼接会把命令截断（搜不到结果甚至执行到多余命令）。
static QString shellQuote(const QString &s)
{
    QString escaped = s;
    escaped.replace("'", "'\\''");
    return "'" + escaped + "'";
}

LogCollectWidget::LogCollectWidget(SshClient *client, QWidget *parent)
    : QWidget(parent), m_ssh(client)
{
    setupUi();
    loadHostConfigs();

    // 连接 SshClient 信号
    connect(m_ssh, &SshClient::connected, this, &LogCollectWidget::onSshConnected);
    connect(m_ssh, &SshClient::connectFailed, this, &LogCollectWidget::onSshConnectFailed);
    connect(m_ssh, &SshClient::disconnected, this, &LogCollectWidget::onSshDisconnected);
    connect(m_ssh, &SshClient::fileListReceived, this, &LogCollectWidget::onFileListReceived);
    connect(m_ssh, &SshClient::robotInfoReceived, this, &LogCollectWidget::onRobotInfoReceived);
    connect(m_ssh, &SshClient::downloadProgress, this, &LogCollectWidget::onDownloadProgress);
    connect(m_ssh, &SshClient::downloadComplete, this, &LogCollectWidget::onDownloadComplete);
    connect(m_ssh, &SshClient::logMessage, this, &LogCollectWidget::onSshLog);
    connect(m_ssh, &SshClient::downloadError, this, &LogCollectWidget::onDownloadError);
    connect(m_ssh, &SshClient::errorOccurred, this, &LogCollectWidget::onSshError);
}

void LogCollectWidget::setupUi()
{
    setStyleSheet(DARK_STYLE);

    auto *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(6, 6, 6, 6);

    auto *splitter = new QSplitter(Qt::Horizontal);

    // ======== 左侧: 主机管理 ========
    auto *leftPanel = new QWidget;
    auto *leftLay = new QVBoxLayout(leftPanel);
    leftLay->setContentsMargins(0, 0, 0, 0);

    auto *hostGroup = new QGroupBox("  主机管理");
    auto *hostLay = new QVBoxLayout(hostGroup);

    m_hostTree = new QTreeWidget;
    m_hostTree->setHeaderLabels({"名称", "地址", "状态"});
    m_hostTree->setColumnWidth(0, 100);
    m_hostTree->setColumnWidth(1, 130);
    m_hostTree->setColumnWidth(2, 50);
    m_hostTree->setRootIsDecorated(false);
    m_hostTree->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_hostTree, &QTreeWidget::itemSelectionChanged,
            this, &LogCollectWidget::onHostSelectionChanged);
    hostLay->addWidget(m_hostTree);

    auto *btnRow = new QHBoxLayout;
    m_btnAddHost = new QPushButton("+ 添加");
    m_btnAddHost->setStyleSheet(
        "QPushButton { background: #238636; color: white; font-weight: bold; border: none; border-radius: 4px; }"
        "QPushButton:hover { background: #2ea043; }");
    m_btnDeleteHost = new QPushButton("- 删除");
    m_btnDeleteHost->setStyleSheet(
        "QPushButton { background: #da3633; color: white; font-weight: bold; border: none; border-radius: 4px; }"
        "QPushButton:hover { background: #f85149; }");
    m_btnEditHost = new QPushButton("编辑");
    m_btnEditHost->setStyleSheet(
        "QPushButton { background: #1f6feb; color: white; font-weight: bold; border: none; border-radius: 4px; }"
        "QPushButton:hover { background: #388bfd; }");
    btnRow->addWidget(m_btnAddHost);
    btnRow->addWidget(m_btnDeleteHost);
    btnRow->addWidget(m_btnEditHost);
    hostLay->addLayout(btnRow);

    connect(m_btnAddHost, &QPushButton::clicked, this, &LogCollectWidget::onAddHost);
    connect(m_btnDeleteHost, &QPushButton::clicked, this, &LogCollectWidget::onDeleteHost);
    connect(m_btnEditHost, &QPushButton::clicked, this, &LogCollectWidget::onEditHost);

    leftLay->addWidget(hostGroup);

    // ======== 左侧: 导出设置 ========
    auto *exportGroup = new QGroupBox("  导出设置");
    auto *exportLay = new QVBoxLayout(exportGroup);
    exportLay->setSpacing(8);
    exportLay->setContentsMargins(10, 20, 10, 10);

    auto *localRow = new QHBoxLayout;
    localRow->addWidget(new QLabel("本地保存:"));
    m_localPathEdit = new QLineEdit;
    m_localPathEdit->setPlaceholderText("选择本地保存路径...");
    m_btnBrowse = new QPushButton("浏览...");
    m_btnBrowse->setStyleSheet(
        "QPushButton { background: #1f6feb; color: white; border: none; border-radius: 4px; padding: 5px 10px; }"
        "QPushButton:hover { background: #388bfd; }");
    localRow->addWidget(m_localPathEdit, 1);
    localRow->addWidget(m_btnBrowse);
    exportLay->addLayout(localRow);

    connect(m_btnBrowse, &QPushButton::clicked, this, &LogCollectWidget::onBrowseLocalPath);

    m_btnExport = new QPushButton("⬇ 开始导出");
    m_btnExport->setStyleSheet(
        "QPushButton { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #238636,stop:1 #2ea043);"
        "  color: white; font-weight: bold; font-size: 13px; border: none; border-radius: 5px; padding: 8px 0; }"
        "QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #2ea043,stop:1 #3fb950); }"
        "QPushButton:disabled { background: #21262d; color: #484f58; border: 1px solid #30363d; }");
    m_btnExport->setEnabled(false);
    exportLay->addWidget(m_btnExport);

    m_progressBar = new QProgressBar;
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    exportLay->addWidget(m_progressBar);

    connect(m_btnExport, &QPushButton::clicked, this, &LogCollectWidget::onStartExport);

    leftLay->addWidget(exportGroup);
    leftPanel->setFixedWidth(300);

    // ======== 右侧: 操作区 ========
    auto *rightPanel = new QWidget;
    auto *rightLay = new QVBoxLayout(rightPanel);
    rightLay->setContentsMargins(4, 0, 0, 0);
    rightLay->setSpacing(8);

    // 连接栏
    auto *connRow = new QHBoxLayout;
    m_statusLabel = new QLabel("● 未连接");
    m_statusLabel->setStyleSheet("color: #f85149; font-weight: bold; font-size: 14px;");
    m_btnConnect = new QPushButton("  连接  ");
    m_btnConnect->setStyleSheet(
        "QPushButton { background: #238636; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 6px 20px; border: none; font-size: 13px; }"
        "QPushButton:hover { background: #2ea043; }"
        "QPushButton:disabled { background: #21262d; color: #484f58; border: 1px solid #30363d; }");
    m_btnDisconnect = new QPushButton("  断开  ");
    m_btnDisconnect->setStyleSheet(
        "QPushButton { background: #da3633; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 6px 20px; border: none; font-size: 13px; }"
        "QPushButton:hover { background: #f85149; }"
        "QPushButton:disabled { background: #21262d; color: #484f58; border: 1px solid #30363d; }");
    m_btnDisconnect->setEnabled(false);
    connRow->addWidget(m_statusLabel);
    connRow->addStretch();
    connRow->addWidget(m_btnConnect);
    connRow->addWidget(m_btnDisconnect);
    rightLay->addLayout(connRow);

    connect(m_btnConnect, &QPushButton::clicked, this, &LogCollectWidget::onConnect);
    connect(m_btnDisconnect, &QPushButton::clicked, this, &LogCollectWidget::onDisconnect);

    // 路径切换
    auto *pathRow = new QHBoxLayout;
    pathRow->addWidget(new QLabel("远程路径:"));
    m_pathCombo = new QComboBox;
    m_pathCombo->setEditable(true);
    connect(m_pathCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LogCollectWidget::onPathChanged);
    pathRow->addWidget(m_pathCombo, 1);
    rightLay->addLayout(pathRow);

    // 远程文件列表
    m_fileTable = new QTableWidget;
    m_fileTable->setColumnCount(4);
    m_fileTable->setHorizontalHeaderLabels({"选择", "文件名", "大小", "修改时间"});
    m_fileTable->horizontalHeader()->setStretchLastSection(true);
    m_fileTable->setColumnWidth(0, 40);
    m_fileTable->setColumnWidth(1, 260);
    m_fileTable->setColumnWidth(2, 80);
    m_fileTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fileTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_fileTable->viewport()->setAutoFillBackground(false);
    m_fileTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_fileTable, &QTableWidget::customContextMenuRequested,
            this, &LogCollectWidget::onFileTableContextMenu);
    rightLay->addWidget(m_fileTable, 1);

    // 文件操作按钮
    auto *fileActRow = new QHBoxLayout;
    m_btnRefresh = new QPushButton("↻ 刷新");
    m_btnRefresh->setStyleSheet(
        "QPushButton { background: #1f6feb; color: white; font-weight: bold; border: none; border-radius: 4px; }"
        "QPushButton:hover { background: #388bfd; }");
    m_btnSelectAll = new QPushButton("☑ 全选");
    m_btnSelectAll->setStyleSheet(
        "QPushButton { background: #238636; color: white; border: none; border-radius: 4px; }"
        "QPushButton:hover { background: #2ea043; }");
    m_btnDeselectAll = new QPushButton("☐ 取消全选");
    m_btnDeselectAll->setStyleSheet(
        "QPushButton { background: #6e7681; color: white; border: none; border-radius: 4px; }"
        "QPushButton:hover { background: #8b949e; }");
    for (auto *btn : {m_btnRefresh, m_btnSelectAll, m_btnDeselectAll})
        btn->setFixedHeight(30);
    fileActRow->addWidget(m_btnRefresh);
    fileActRow->addWidget(m_btnSelectAll);
    fileActRow->addWidget(m_btnDeselectAll);
    fileActRow->addStretch();
    fileActRow->setSpacing(6);
    rightLay->addLayout(fileActRow);
    rightLay->addSpacing(2);

    connect(m_btnRefresh, &QPushButton::clicked, this, &LogCollectWidget::onRefreshFiles);
    connect(m_btnSelectAll, &QPushButton::clicked, this, &LogCollectWidget::onSelectAll);
    connect(m_btnDeselectAll, &QPushButton::clicked, this, &LogCollectWidget::onDeselectAll);
    connect(m_fileTable, &QTableWidget::cellDoubleClicked,
            this, &LogCollectWidget::onFileTableDoubleClicked);

    // 操作日志
    auto *logGroup = new QGroupBox("  操作日志");
    auto *logLay = new QVBoxLayout(logGroup);
    logLay->setContentsMargins(10, 20, 10, 10);
    m_logView = new QTextEdit;
    m_logView->setReadOnly(true);
    m_logView->setPlaceholderText("连接主机后，操作记录将显示在此处...");
    m_logView->setStyleSheet("QTextEdit { font-family: 'Consolas', 'Courier New', monospace; font-size: 12px; }");
    logLay->addWidget(m_logView);
    rightLay->addWidget(logGroup);

    splitter->addWidget(leftPanel);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    mainLayout->addWidget(splitter);
}

// ======== 主机管理 ========

void LogCollectWidget::onAddHost()
{
    QDialog dlg(this);
    dlg.setWindowTitle("添加主机");
    dlg.setMinimumWidth(420);
    dlg.setStyleSheet(
        "QDialog { background: #161b22; color: #c9d1d9; }"
        "QLabel { color: #c9d1d9; font-size: 13px; }"
        "QLineEdit { background: #0d1117; border: 1px solid #30363d; border-radius: 4px;"
        "           padding: 6px 8px; color: #c9d1d9; font-size: 13px; }"
        "QLineEdit:focus { border-color: #58a6ff; }"
        "QListWidget { background: #0d1117; border: 1px solid #30363d; border-radius: 4px; color: #c9d1d9; }"
        "QListWidget::item { min-height: 28px; padding: 4px 6px; font-size: 13px; }"
        "QDialogButtonBox QPushButton { background: #21262d; border: 1px solid #30363d;"
        "  border-radius: 4px; padding: 6px 16px; color: #c9d1d9; font-size: 13px; }"
        "QDialogButtonBox QPushButton:hover { background: #30363d; border-color: #58a6ff; }");
    auto *form = new QFormLayout(&dlg);

    auto *nameEdit = new QLineEdit;
    nameEdit->setPlaceholderText("例: X30-103");
    auto *hostEdit = new QLineEdit;
    hostEdit->setPlaceholderText("192.168.1.103");
    auto *portEdit = new QLineEdit("22");
    auto *userEdit = new QLineEdit("user");
    auto *passEdit = new QLineEdit;
    passEdit->setEchoMode(QLineEdit::Password);
    auto *noteEdit = new QLineEdit;

    // 日志路径列表
    auto *pathList = new QListWidget;
    pathList->setMinimumHeight(120);
    {
        auto *it = new QListWidgetItem("/var/opt/robot/log/");
        it->setFlags(it->flags() | Qt::ItemIsEditable);
        pathList->addItem(it);
    }
    auto *pathRow = new QHBoxLayout;
    auto *addPathBtn = new QPushButton("+");
    auto *delPathBtn = new QPushButton("-");
    addPathBtn->setFixedSize(32, 32);
    delPathBtn->setFixedSize(32, 32);
    addPathBtn->setStyleSheet(
        "QPushButton { background: #238636; color: white; font-size: 16px; font-weight: bold;"
        "  border: none; border-radius: 4px; }"
        "QPushButton:hover { background: #2ea043; }");
    delPathBtn->setStyleSheet(
        "QPushButton { background: #da3633; color: white; font-size: 16px; font-weight: bold;"
        "  border: none; border-radius: 4px; }"
        "QPushButton:hover { background: #f85149; }");
    pathRow->addWidget(pathList);
    auto *pathBtnLay = new QVBoxLayout;
    pathBtnLay->addWidget(addPathBtn);
    pathBtnLay->addWidget(delPathBtn);
    pathBtnLay->addStretch();
    pathRow->addLayout(pathBtnLay);

    connect(addPathBtn, &QPushButton::clicked, [pathList]() {
        auto *it = new QListWidgetItem("/var/opt/robot/log/");
        it->setFlags(it->flags() | Qt::ItemIsEditable);
        pathList->addItem(it);
        pathList->editItem(it);
    });
    connect(delPathBtn, &QPushButton::clicked, [pathList]() {
        delete pathList->currentItem();
    });

    form->addRow("名称:", nameEdit);
    form->addRow("地址:", hostEdit);
    form->addRow("端口:", portEdit);
    form->addRow("用户名:", userEdit);
    form->addRow("密码:", passEdit);
    form->addRow("备注:", noteEdit);
    form->addRow("日志路径:", pathRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        HostConfig cfg;
        cfg.name = nameEdit->text().trimmed();
        cfg.host = hostEdit->text().trimmed();
        cfg.port = portEdit->text().toInt();
        cfg.user = userEdit->text().trimmed();
        cfg.password = passEdit->text();
        cfg.note = noteEdit->text().trimmed();
        for (int i = 0; i < pathList->count(); ++i)
            cfg.logPaths.append(pathList->item(i)->text().trimmed());

        // 输入验证
        if (cfg.host.isEmpty()) {
            QMessageBox::warning(this, "验证失败", "主机地址不能为空");
            return;
        }
        if (cfg.port <= 0 || cfg.port > 65535) {
            QMessageBox::warning(this, "验证失败", "端口号必须在 1~65535 之间");
            return;
        }
        if (cfg.user.isEmpty()) {
            QMessageBox::warning(this, "验证失败", "用户名不能为空");
            return;
        }

        if (cfg.name.isEmpty()) cfg.name = cfg.host;
        m_hosts.append(cfg);
        saveHostConfigs();

        auto *item = new QTreeWidgetItem(m_hostTree,
                                         {cfg.name, cfg.host + ":" + QString::number(cfg.port), "离线"});
        item->setForeground(2, QColor("#f85149"));
        m_hostTree->addTopLevelItem(item);
    }
}

void LogCollectWidget::onDeleteHost()
{
    int idx = m_hostTree->indexOfTopLevelItem(m_hostTree->currentItem());
    if (idx < 0) return;
    m_hosts.removeAt(idx);
    delete m_hostTree->takeTopLevelItem(idx);
    saveHostConfigs();
}

void LogCollectWidget::onEditHost()
{
    int idx = m_hostTree->indexOfTopLevelItem(m_hostTree->currentItem());
    if (idx < 0 || idx >= m_hosts.size()) return;

    HostConfig &cfg = m_hosts[idx];

    QDialog dlg(this);
    dlg.setWindowTitle("编辑主机");
    dlg.setMinimumWidth(420);
    dlg.setStyleSheet(
        "QDialog { background: #161b22; color: #c9d1d9; }"
        "QLabel { color: #c9d1d9; font-size: 13px; }"
        "QLineEdit { background: #0d1117; border: 1px solid #30363d; border-radius: 4px;"
        "           padding: 6px 8px; color: #c9d1d9; font-size: 13px; }"
        "QLineEdit:focus { border-color: #58a6ff; }"
        "QListWidget { background: #0d1117; border: 1px solid #30363d; border-radius: 4px; color: #c9d1d9; }"
        "QListWidget::item { min-height: 28px; padding: 4px 6px; font-size: 13px; }"
        "QDialogButtonBox QPushButton { background: #21262d; border: 1px solid #30363d;"
        "  border-radius: 4px; padding: 6px 16px; color: #c9d1d9; font-size: 13px; }"
        "QDialogButtonBox QPushButton:hover { background: #30363d; border-color: #58a6ff; }");
    auto *form = new QFormLayout(&dlg);

    auto *nameEdit = new QLineEdit(cfg.name);
    auto *hostEdit = new QLineEdit(cfg.host);
    auto *portEdit = new QLineEdit(QString::number(cfg.port));
    auto *userEdit = new QLineEdit(cfg.user);
    auto *passEdit = new QLineEdit(cfg.password);
    passEdit->setEchoMode(QLineEdit::Password);
    auto *noteEdit = new QLineEdit(cfg.note);

    auto *pathList = new QListWidget;
    pathList->setMinimumHeight(120);
    for (const auto &p : cfg.logPaths)
        pathList->addItem(p);
    if (pathList->count() == 0)
        pathList->addItem("/var/opt/robot/log/");

    for (int i = 0; i < pathList->count(); ++i)
        pathList->item(i)->setFlags(pathList->item(i)->flags() | Qt::ItemIsEditable);

    auto *pathRow = new QHBoxLayout;
    auto *addPathBtn = new QPushButton("+");
    auto *delPathBtn = new QPushButton("-");
    addPathBtn->setFixedSize(32, 32);
    delPathBtn->setFixedSize(32, 32);
    addPathBtn->setStyleSheet(
        "QPushButton { background: #238636; color: white; font-size: 16px; font-weight: bold;"
        "  border: none; border-radius: 4px; }"
        "QPushButton:hover { background: #2ea043; }");
    delPathBtn->setStyleSheet(
        "QPushButton { background: #da3633; color: white; font-size: 16px; font-weight: bold;"
        "  border: none; border-radius: 4px; }"
        "QPushButton:hover { background: #f85149; }");
    pathRow->addWidget(pathList);
    auto *pathBtnLay = new QVBoxLayout;
    pathBtnLay->addWidget(addPathBtn);
    pathBtnLay->addWidget(delPathBtn);
    pathBtnLay->addStretch();
    pathRow->addLayout(pathBtnLay);

    connect(addPathBtn, &QPushButton::clicked, [pathList]() {
        auto *it = new QListWidgetItem("/var/opt/robot/log/");
        it->setFlags(it->flags() | Qt::ItemIsEditable);
        pathList->addItem(it);
        pathList->editItem(it);
    });
    connect(delPathBtn, &QPushButton::clicked, [pathList]() {
        delete pathList->currentItem();
    });

    form->addRow("名称:", nameEdit);
    form->addRow("地址:", hostEdit);
    form->addRow("端口:", portEdit);
    form->addRow("用户名:", userEdit);
    form->addRow("密码:", passEdit);
    form->addRow("备注:", noteEdit);
    form->addRow("日志路径:", pathRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        cfg.name = nameEdit->text().trimmed();
        cfg.host = hostEdit->text().trimmed();
        cfg.port = portEdit->text().toInt();
        cfg.user = userEdit->text().trimmed();
        cfg.password = passEdit->text();
        cfg.note = noteEdit->text().trimmed();
        cfg.logPaths.clear();
        for (int i = 0; i < pathList->count(); ++i)
            cfg.logPaths.append(pathList->item(i)->text().trimmed());

        // 输入验证
        if (cfg.host.isEmpty()) {
            QMessageBox::warning(this, "验证失败", "主机地址不能为空");
            return;
        }
        if (cfg.port <= 0 || cfg.port > 65535) {
            QMessageBox::warning(this, "验证失败", "端口号必须在 1~65535 之间");
            return;
        }
        if (cfg.user.isEmpty()) {
            QMessageBox::warning(this, "验证失败", "用户名不能为空");
            return;
        }
        if (cfg.name.isEmpty()) cfg.name = cfg.host;

        auto *item = m_hostTree->topLevelItem(idx);
        item->setText(0, cfg.name);
        item->setText(1, cfg.host + ":" + QString::number(cfg.port));
        saveHostConfigs();
    }
}

void LogCollectWidget::onHostSelectionChanged()
{
    int idx = m_hostTree->indexOfTopLevelItem(m_hostTree->currentItem());
    if (idx < 0 || idx >= m_hosts.size()) return;

    const HostConfig &cfg = m_hosts[idx];
    m_pathCombo->clear();
    for (const auto &p : cfg.logPaths)
        m_pathCombo->addItem(p);
}

// ======== 连接/断开 ========

void LogCollectWidget::onConnect()
{
    int idx = m_hostTree->indexOfTopLevelItem(m_hostTree->currentItem());
    if (idx < 0 || idx >= m_hosts.size()) {
        appendLog("[提示] 请先选择一个主机");
        return;
    }

    if (!m_ssh->isHelperRunning()) {
        m_ssh->startHelper();
        // 等待 helper 就绪后再连接
        connect(m_ssh, &SshClient::helperReady, this, [this, idx]() {
            const HostConfig &cfg = m_hosts[idx];
            m_currentHost = cfg.host;
            m_currentPort = cfg.port;
            m_ssh->connectHost(cfg.host, cfg.port, cfg.user, cfg.password);
        }, Qt::SingleShotConnection);
    } else {
        const HostConfig &cfg = m_hosts[idx];
        m_currentHost = cfg.host;
        m_currentPort = cfg.port;
        m_ssh->connectHost(cfg.host, cfg.port, cfg.user, cfg.password);
    }
    m_btnConnect->setEnabled(false);
    appendLog("[操作] 正在连接...");
}

void LogCollectWidget::onDisconnect()
{
    m_ssh->disconnectHost(m_currentHost, m_currentPort);
}

void LogCollectWidget::onPathChanged(int)
{
    if (!m_connected) return;
    QString path = m_pathCombo->currentText().trimmed();
    if (!path.isEmpty())
        m_ssh->listFiles(m_currentHost, m_currentPort, path);
}

// ======== 导出 ========

void LogCollectWidget::onBrowseLocalPath()
{
    QString dir = QFileDialog::getExistingDirectory(this, "选择本地保存路径");
    if (!dir.isEmpty())
        m_localPathEdit->setText(dir);
}

void LogCollectWidget::onStartExport()
{
    if (!m_connected) return;

    QString localDir = m_localPathEdit->text().trimmed();
    if (localDir.isEmpty()) {
        appendLog("[错误] 请先选择本地保存路径");
        return;
    }

    // 收集选中的文件
    QJsonArray files;
    QString remotePath = m_pathCombo->currentText().trimmed();
    for (int row = 0; row < m_fileTable->rowCount(); ++row) {
        auto *cb = qobject_cast<QCheckBox*>(m_fileTable->cellWidget(row, 0));
        if (cb && cb->isChecked()) {
            QString name = m_fileTable->item(row, 1)->text();
            QJsonObject fo;
            fo["remote_path"] = remotePath + "/" + name;
            fo["name"] = name;
            files.append(fo);
        }
    }

    if (files.isEmpty()) {
        appendLog("[错误] 请至少勾选一个文件");
        return;
    }

    // 防误操作确认
    auto ret = QMessageBox::question(this, "确认导出",
        QString("确认导出选中的 %1 个文件到:\n%2").arg(files.size()).arg(localDir),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (ret != QMessageBox::Yes) return;

    // 始终从 103 主板获取四足机器狗信息（SN/版本/构型）
    QString infoHost = m_currentHost;
    int infoPort = m_currentPort;
    QString infoUser, infoPass;
    for (const auto &h : m_hosts) {
        if (h.host.endsWith(".103")) {
            infoHost = h.host;
            infoPort = h.port;
            infoUser = h.user;
            infoPass = h.password;
            break;
        }
    }

    // 如果 103 不是当前连接的主机，需要先连接 103
    if (infoHost != m_currentHost && !infoUser.isEmpty()) {
        appendLog(QString("[操作] 正在连接 %1 获取四足机器狗信息...").arg(infoHost));
        m_ssh->connectHost(infoHost, infoPort, infoUser, infoPass);
        // 连接成功后再获取 robot info
        connect(m_ssh, &SshClient::connected, this, [this, infoHost, infoPort](const QString &host) {
            if (host == infoHost) {
                m_ssh->getRobotInfo(infoHost, infoPort);
            }
        }, Qt::SingleShotConnection);
    } else {
        appendLog(QString("[操作] 正在从 %1 获取四足机器狗信息...").arg(infoHost));
        m_ssh->getRobotInfo(infoHost, infoPort);
    }

    // robot_info 回调中触发下载（见 onRobotInfoReceived）
    m_btnExport->setEnabled(false);
    m_btnExport->setText("⬇ 导出中...");
    m_progressBar->setValue(0);
}

// ======== SSH 信号回调 ========

void LogCollectWidget::onSshConnected(const QString &host)
{
    if (host != m_currentHost) return;
    m_connected = true;
    updateConnectionState(true);
    appendLog("[成功] SSH 连接成功: " + host);

    // 更新主机树状态
    for (int i = 0; i < m_hostTree->topLevelItemCount(); ++i) {
        auto *item = m_hostTree->topLevelItem(i);
        if (i < m_hosts.size() && m_hosts[i].host == host) {
            item->setText(2, "在线");
            item->setForeground(2, QColor("#3fb950"));
        }
    }

    // 自动列出文件
    QString path = m_pathCombo->currentText().trimmed();
    if (!path.isEmpty())
        m_ssh->listFiles(m_currentHost, m_currentPort, path);
}

void LogCollectWidget::onSshConnectFailed(const QString & /*host*/, const QString &error)
{
    m_connected = false;
    updateConnectionState(false);
    appendLog("[失败] 连接失败: " + error);
}

void LogCollectWidget::onSshDisconnected(const QString &host)
{
    m_connected = false;
    updateConnectionState(false);
    appendLog("[断开] 已断开: " + host);

    for (int i = 0; i < m_hostTree->topLevelItemCount(); ++i) {
        auto *item = m_hostTree->topLevelItem(i);
        if (i < m_hosts.size() && m_hosts[i].host == host) {
            item->setText(2, "离线");
            item->setForeground(2, QColor("#f85149"));
        }
    }
}

void LogCollectWidget::onFileListReceived(const QString &, const QString &path,
                                          const QVector<RemoteFileInfo> &files)
{
    m_fileTable->setRowCount(0);

    // 返回上级目录条目
    if (path != "/" && !path.isEmpty()) {
        int row = 0;
        m_fileTable->insertRow(row);
        m_fileTable->setItem(row, 0, new QTableWidgetItem());
        auto *nameItem = new QTableWidgetItem(QString::fromUtf8("\xe2\x86\xa9 .. (返回上级)"));
        nameItem->setData(Qt::UserRole, "PARENT_DIR");
        nameItem->setForeground(QColor("#58a6ff"));
        m_fileTable->setItem(row, 1, nameItem);
        m_fileTable->setItem(row, 2, new QTableWidgetItem(""));
        m_fileTable->setItem(row, 3, new QTableWidgetItem(""));
    }

    // 先添加目录，再添加文件
    int dirCount = 0, fileCount = 0;
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto &fi : files) {
            if (fi.name == "." || fi.name == "..") continue;
            if ((pass == 0) != fi.isDir) continue;

            int row = m_fileTable->rowCount();
            m_fileTable->insertRow(row);

            if (fi.isDir) {
                m_fileTable->setItem(row, 0, new QTableWidgetItem());
                auto *nameItem = new QTableWidgetItem(QString::fromUtf8("\xf0\x9f\x93\x82 ") + fi.name);
                nameItem->setData(Qt::UserRole, "DIR");
                nameItem->setData(Qt::UserRole + 1, fi.name);
                nameItem->setForeground(QColor("#58a6ff"));
                m_fileTable->setItem(row, 1, nameItem);
                m_fileTable->setItem(row, 2, new QTableWidgetItem("<目录>"));
                QDateTime dt = QDateTime::fromSecsSinceEpoch(fi.mtime);
                m_fileTable->setItem(row, 3, new QTableWidgetItem(dt.toString("yyyy-MM-dd HH:mm:ss")));
                dirCount++;
            } else {
                auto *cb = new QCheckBox;
                m_fileTable->setCellWidget(row, 0, cb);
                m_fileTable->setItem(row, 1, new QTableWidgetItem(fi.name));

                QString sizeStr;
                if (fi.size > 1024 * 1024)
                    sizeStr = QString("%1 MB").arg(fi.size / (1024.0 * 1024.0), 0, 'f', 1);
                else if (fi.size > 1024)
                    sizeStr = QString("%1 KB").arg(fi.size / 1024.0, 0, 'f', 1);
                else
                    sizeStr = QString("%1 B").arg(fi.size);
                m_fileTable->setItem(row, 2, new QTableWidgetItem(sizeStr));

                QDateTime dt = QDateTime::fromSecsSinceEpoch(fi.mtime);
                m_fileTable->setItem(row, 3, new QTableWidgetItem(dt.toString("yyyy-MM-dd HH:mm:ss")));
                fileCount++;
            }
        }
    }
    appendLog(QString("[文件] 列出 %1 个目录, %2 个文件").arg(dirCount).arg(fileCount));
}

void LogCollectWidget::onRobotInfoReceived(const QString &sn, const QString &version,
                                           const QString &deviceType)
{
    m_robotSn = sn;
    m_robotVersion = version;
    m_robotDeviceType = deviceType;
    appendLog(QString("[信息] SN=%1, 版本=%2, 构型=%3").arg(sn, version, deviceType));

    // 通过执行 ifconfig eth0 获取设备 eth0 的真实 IP，取最后一段作为 boardId
    appendLog("[操作] 正在获取设备 eth0 IP...");
    connect(m_ssh, &SshClient::execResult, this,
            [this](const QString &stdOut, const QString &) {
        QString boardId = "unknown";
        // 解析 ifconfig 输出: inet 10.21.33.103
        QRegularExpression re(R"(inet\s+(\d+\.\d+\.\d+\.\d+))");
        auto match = re.match(stdOut);
        if (match.hasMatch()) {
            boardId = match.captured(1).section('.', -1);
        }
        appendLog(QString("[信息] 设备 eth0 boardId=%1").arg(boardId));

        // 生成 zip 文件名
        QString now = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
        QString zipName = QString("%1_%2_%3_%4_%5.zip")
                              .arg(m_robotSn, boardId, m_robotDeviceType, m_robotVersion, now);

        // 收集选中的文件
        QJsonArray files;
        QString remotePath = m_pathCombo->currentText().trimmed();
        for (int row = 0; row < m_fileTable->rowCount(); ++row) {
            auto *cb = qobject_cast<QCheckBox*>(m_fileTable->cellWidget(row, 0));
            if (cb && cb->isChecked()) {
                QString name = m_fileTable->item(row, 1)->text();
                QJsonObject fo;
                fo["remote_path"] = remotePath + "/" + name;
                fo["name"] = name;
                files.append(fo);
            }
        }

        QString localDir = m_localPathEdit->text().trimmed();
        appendLog("[下载] 开始下载, 压缩包: " + zipName);
        m_ssh->downloadFiles(m_currentHost, m_currentPort, files, localDir, zipName);
    }, Qt::SingleShotConnection);

    m_ssh->execCommand(m_currentHost, m_currentPort, "ifconfig eth0");
}

void LogCollectWidget::onDownloadProgress(int current, int total,
                                          const QString &file, int percent)
{
    int val = (total > 0) ? (current * 100 / total) : percent;
    m_progressBar->setValue(val);
    appendLog(QString("[下载] (%1/%2) %3").arg(current).arg(total).arg(file));
}

void LogCollectWidget::onDownloadComplete(const QString &zipPath, int totalFiles)
{
    m_progressBar->setValue(100);
    appendLog(QString("[完成] 导出成功: %1 (%2个文件)").arg(zipPath).arg(totalFiles));
    QMessageBox::information(this, "导出完成",
                             QString("日志已导出:\n%1\n共 %2 个文件").arg(zipPath).arg(totalFiles));
    // 对话框关闭后重置控件到初始状态
    m_progressBar->setValue(0);
    m_btnExport->setEnabled(true);
    m_btnExport->setText("⬇ 开始导出");
    emit exportCompleted(zipPath);
}

void LogCollectWidget::onSshLog(const QString &level, const QString &msg)
{
    appendLog(QString("[%1] %2").arg(level.toUpper(), msg));
}

void LogCollectWidget::onDownloadError(const QString &error)
{
    m_progressBar->setValue(0);
    m_btnExport->setEnabled(true);
    m_btnExport->setText("⬇ 开始导出");
    appendLog("[错误] 下载失败: " + error);
}

void LogCollectWidget::onSshError(const QString &msg)
{
    appendLog("[错误] " + msg);
}

void LogCollectWidget::onRefreshFiles()
{
    if (!m_connected) {
        appendLog("[提示] 请先连接主机");
        return;
    }
    QString path = m_pathCombo->currentText().trimmed();
    if (!path.isEmpty())
        m_ssh->listFiles(m_currentHost, m_currentPort, path);
}

void LogCollectWidget::onSelectAll()
{
    for (int row = 0; row < m_fileTable->rowCount(); ++row) {
        auto *cb = qobject_cast<QCheckBox*>(m_fileTable->cellWidget(row, 0));
        if (cb) cb->setChecked(true);
    }
}

void LogCollectWidget::onDeselectAll()
{
    for (int row = 0; row < m_fileTable->rowCount(); ++row) {
        auto *cb = qobject_cast<QCheckBox*>(m_fileTable->cellWidget(row, 0));
        if (cb) cb->setChecked(false);
    }
}

void LogCollectWidget::onFileTableDoubleClicked(int row, int)
{
    auto *nameItem = m_fileTable->item(row, 1);
    if (!nameItem) return;

    QString role = nameItem->data(Qt::UserRole).toString();
    if (role == "PARENT_DIR") {
        QString current = m_pathCombo->currentText().trimmed();
        if (current.endsWith('/')) current.chop(1);
        int lastSlash = current.lastIndexOf('/');
        QString parent = (lastSlash > 0) ? current.left(lastSlash) : "/";
        m_pathCombo->blockSignals(true);
        m_pathCombo->setCurrentText(parent);
        m_pathCombo->blockSignals(false);
        m_ssh->listFiles(m_currentHost, m_currentPort, parent);
    } else if (role == "DIR") {
        QString dirName = nameItem->data(Qt::UserRole + 1).toString();
        QString current = m_pathCombo->currentText().trimmed();
        if (!current.endsWith('/')) current += '/';
        QString newPath = current + dirName;
        m_pathCombo->blockSignals(true);
        m_pathCombo->setCurrentText(newPath);
        m_pathCombo->blockSignals(false);
        m_ssh->listFiles(m_currentHost, m_currentPort, newPath);
    }
}

void LogCollectWidget::onFileTableContextMenu(const QPoint &pos)
{
    int row = m_fileTable->rowAt(pos.y());
    if (row < 0) return;

    auto *nameItem = m_fileTable->item(row, 1);
    if (!nameItem) return;

    // 只对普通文件显示右键菜单（跳过目录和返回上级）
    QString role = nameItem->data(Qt::UserRole).toString();
    if (role == "DIR" || role == "PARENT_DIR") return;

    QString fileName = nameItem->text();
    QString remotePath = m_pathCombo->currentText().trimmed();
    if (!remotePath.endsWith('/')) remotePath += '/';
    QString fullPath = remotePath + fileName;

    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background: #161b22; border: 1px solid #30363d; border-radius: 6px; padding: 4px; }"
        "QMenu::item { color: #c9d1d9; padding: 6px 20px; border-radius: 4px; }"
        "QMenu::item:selected { background: #1f6feb; color: white; }");
    auto *actView = menu.addAction("查看文件内容");

    auto *chosen = menu.exec(m_fileTable->viewport()->mapToGlobal(pos));
    if (chosen == actView) {
        previewRemoteFile(fullPath, fileName);
    }
}

void LogCollectWidget::previewRemoteFile(const QString &remotePath, const QString &fileName)
{
    if (!m_connected) {
        appendLog("[提示] 请先连接主机");
        return;
    }

    appendLog(QString("[操作] 正在获取文件信息: %1").arg(remotePath));

    // 第一步：获取文件总行数
    connect(m_ssh, &SshClient::execResult, this,
            [this, fileName, remotePath](const QString &stdOut, const QString &) {
        int totalLines = stdOut.trimmed().toInt();
        if (totalLines <= 0) {
            appendLog("[错误] 无法获取文件行数");
            return;
        }

        const int PAGE_SIZE = 1000;

        appendLog(QString("[信息] %1: 共 %2 行").arg(fileName).arg(totalLines));

        // 创建预览对话框
        auto *dlg = new QDialog(this);
        dlg->setWindowTitle(QString("文件预览 - %1").arg(fileName));
        dlg->setAttribute(Qt::WA_DeleteOnClose);

        QSize screenSize = QApplication::primaryScreen()->availableSize();
        dlg->resize(screenSize.width() * 0.75, screenSize.height() * 0.75);

        dlg->setStyleSheet(
            "QDialog { background: #0d1117; }"
            "QLabel { color: #8b949e; font-size: 12px; }"
            "QTableWidget { background: #0d1117; color: #c9d1d9; border: 1px solid #21262d;"
            "  border-radius: 4px; font-family: 'Consolas','Courier New',monospace; font-size: 12px; }"
            "QTableWidget::item { padding: 2px 6px; border-bottom: 1px solid #161b22; }"
            "QTableWidget::item:selected { background: rgba(56,139,253,0.2); }"
            "QHeaderView::section { background: #161b22; color: #8b949e; border: none;"
            "  padding: 4px 8px; font-weight: bold; font-size: 12px; }"
            "QLineEdit { background: #0d1117; color: #c9d1d9; border: 1px solid #30363d;"
            "  border-radius: 4px; padding: 5px 10px; font-size: 13px; }"
            "QLineEdit:focus { border-color: #58a6ff; }"
            "QPushButton { background: #21262d; color: #c9d1d9; font-weight: bold;"
            "  border: 1px solid #30363d; border-radius: 4px; padding: 6px 14px; }"
            "QPushButton:hover { background: #30363d; border-color: #58a6ff; }"
            "QPushButton:disabled { color: #484f58; border-color: #21262d; }");

        auto *layout = new QVBoxLayout(dlg);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->setSpacing(6);

        // === 顶部栏：搜索 ===
        auto *topRow = new QHBoxLayout;
        auto *infoLabel = new QLabel;
        infoLabel->setStyleSheet("color: #58a6ff; font-size: 13px; padding: 4px 0;");
        topRow->addWidget(infoLabel);
        topRow->addStretch();

        auto *searchEdit = new QLineEdit;
        searchEdit->setPlaceholderText("搜索关键字...");
        searchEdit->setFixedWidth(220);
        auto *btnSearch = new QPushButton("🔍 筛选");
        btnSearch->setStyleSheet(
            "QPushButton { background: #1f6feb; color: white; border: none; }"
            "QPushButton:hover { background: #388bfd; }");
        auto *btnClearSearch = new QPushButton("✖ 显示全部");

        topRow->addWidget(searchEdit);
        topRow->addWidget(btnSearch);
        topRow->addWidget(btnClearSearch);
        layout->addLayout(topRow);

        // === 内容表格 ===
        auto *table = new QTableWidget;
        table->setColumnCount(2);
        table->setHorizontalHeaderLabels({"行号", "内容"});
        table->horizontalHeader()->setStretchLastSection(true);
        table->setColumnWidth(0, 70);
        table->verticalHeader()->setVisible(false);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        layout->addWidget(table, 1);

        // === 分页导航栏 ===
        auto *navRow = new QHBoxLayout;
        auto *btnFirst = new QPushButton("┃◀ 首页");
        auto *btnPrev = new QPushButton("◀ 上一页");
        auto *pageLabel = new QLabel;
        pageLabel->setStyleSheet("color: #c9d1d9; font-size: 13px; font-weight: bold;");
        pageLabel->setAlignment(Qt::AlignCenter);
        auto *btnNext = new QPushButton("下一页 ▶");
        auto *btnLast = new QPushButton("末页 ▶┃");
        auto *btnClose = new QPushButton("✖ 关闭");
        btnClose->setStyleSheet(
            "QPushButton { background: #da3633; color: white; font-weight: bold;"
            "  border: none; border-radius: 4px; padding: 6px 16px; }"
            "QPushButton:hover { background: #f85149; }");

        navRow->addWidget(btnFirst);
        navRow->addWidget(btnPrev);
        navRow->addStretch();
        navRow->addWidget(pageLabel);
        navRow->addStretch();
        navRow->addWidget(btnNext);
        navRow->addWidget(btnLast);
        navRow->addSpacing(20);
        navRow->addWidget(btnClose);
        layout->addLayout(navRow);

        // === 状态变量 ===
        struct PreviewState {
            int currentPage = 0;
            int totalPages = 0;
            int totalLines = 0;
            bool isSearchMode = false;
            int searchMatchCount = 0;
            int searchTotalPages = 0;
        };
        auto *state = new PreviewState;
        state->totalLines = totalLines;
        state->totalPages = (totalLines + PAGE_SIZE - 1) / PAGE_SIZE;
        
        // 日志等级颜色 — 宽松匹配（兼容 [WARN ], [INFO ] 等带空格格式）
        auto getLogLevelColor = [](const QString &line) -> QColor {
            QString upper = line.toUpper();
            if (upper.contains("ERROR") || upper.contains("FATAL"))
                return QColor("#f85149");
            if (upper.contains("WARN"))
                return QColor("#d29922");
            if (upper.contains("INFO"))
                return QColor("#58a6ff");
            if (upper.contains("DEBUG"))
                return QColor("#8b949e");
            return QColor("#c9d1d9");
        };
        
        // 填充表格通用函数
        auto fillTable = [=](const QStringList &lines, const QList<int> &lineNums) {
            table->setRowCount(lines.size());
            for (int i = 0; i < lines.size(); ++i) {
                int lineNo = (i < lineNums.size()) ? lineNums[i] : (i + 1);
                auto *numItem = new QTableWidgetItem(QString::number(lineNo));
                numItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                numItem->setForeground(QColor("#6e7681"));
                table->setItem(i, 0, numItem);
        
                auto *contentItem = new QTableWidgetItem(lines[i]);
                contentItem->setForeground(getLogLevelColor(lines[i]));
                table->setItem(i, 1, contentItem);
            }
            table->scrollToTop();
        };
        
        // 更新导航栏
        auto updateNav = [=]() {
            int pages = state->isSearchMode ? state->searchTotalPages : state->totalPages;
            int page = state->currentPage;
            btnFirst->setEnabled(page > 0);
            btnPrev->setEnabled(page > 0);
            btnNext->setEnabled(page < pages - 1);
            btnLast->setEnabled(page < pages - 1);
        };
        
        // === 普通模式加载页 ===
        auto loadNormalPage = [=](int page) {
            int pages = state->totalPages;
            if (page < 0 || page >= pages) return;
            state->currentPage = page;
            state->isSearchMode = false;
        
            int startLine = page * PAGE_SIZE + 1;
            int endLine = qMin((page + 1) * PAGE_SIZE, state->totalLines);
        
            infoLabel->setText(QString("文件: %1  |  总行数: %2  |  共 %3 页")
                               .arg(fileName).arg(state->totalLines).arg(pages));
            infoLabel->setStyleSheet("color: #58a6ff; font-size: 13px; padding: 4px 0;");
            pageLabel->setText(QString("第 %1 / %2 页  (行 %3 - %4)")
                               .arg(page + 1).arg(pages).arg(startLine).arg(endLine));
            updateNav();
        
            QString sedCmd = QString("sed -n '%1,%2p' %3")
                                 .arg(startLine).arg(endLine).arg(shellQuote(remotePath));
        
            connect(m_ssh, &SshClient::execResult, dlg,
                    [=](const QString &out, const QString &) {
                QStringList lines = out.split('\n');
                if (!lines.isEmpty() && lines.last().isEmpty())
                    lines.removeLast();
                QList<int> lineNums;
                for (int i = 0; i < lines.size(); ++i)
                    lineNums.append(startLine + i);
                fillTable(lines, lineNums);
            }, Qt::SingleShotConnection);
        
            m_ssh->execCommand(m_currentHost, m_currentPort, sedCmd);
        };
        
        // === 搜索筛选模式加载页 ===
        auto loadSearchPage = [=](int page, const QString &keyword) {
            state->currentPage = page;
            state->isSearchMode = true;
            updateNav();
        
            int skipLines = page * PAGE_SIZE;
            QString cmd = QString("grep -n -i -- %1 %2 | sed -n '%3,%4p'")
                              .arg(shellQuote(keyword)).arg(shellQuote(remotePath))
                              .arg(skipLines + 1).arg(skipLines + PAGE_SIZE);
        
            connect(m_ssh, &SshClient::execResult, dlg,
                    [=](const QString &out, const QString &) {
                QStringList rawLines = out.split('\n');
                if (!rawLines.isEmpty() && rawLines.last().isEmpty())
                    rawLines.removeLast();
        
                QStringList contents;
                QList<int> lineNums;
                for (const QString &raw : rawLines) {
                    int colonPos = raw.indexOf(':');
                    if (colonPos > 0) {
                        lineNums.append(raw.left(colonPos).toInt());
                        contents.append(raw.mid(colonPos + 1));
                    }
                }
        
                int pages = state->searchTotalPages;
                pageLabel->setText(QString("筛选结果 第 %1 / %2 页")
                                   .arg(page + 1).arg(pages));
                fillTable(contents, lineNums);
            }, Qt::SingleShotConnection);
        
            m_ssh->execCommand(m_currentHost, m_currentPort, cmd);
        };
        
        // === 搜索触发 ===
        auto doSearch = [=]() {
            QString keyword = searchEdit->text().trimmed();
            if (keyword.isEmpty()) return;
        
            QString countCmd = QString("grep -c -i -- %1 %2")
                                   .arg(shellQuote(keyword)).arg(shellQuote(remotePath));
        
            connect(m_ssh, &SshClient::execResult, dlg,
                    [=](const QString &out, const QString &) {
                int matchCount = out.trimmed().toInt();
                state->searchMatchCount = matchCount;
                state->searchTotalPages = qMax(1, (matchCount + PAGE_SIZE - 1) / PAGE_SIZE);
        
                if (matchCount == 0) {
                    infoLabel->setText(QString("文件: %1  |  ⚠ 未找到: \"%2\"")
                                       .arg(fileName).arg(keyword));
                    infoLabel->setStyleSheet("color: #d29922; font-size: 13px; padding: 4px 0;");
                    table->setRowCount(0);
                    pageLabel->setText("无结果");
                    return;
                }
        
                infoLabel->setText(QString("文件: %1  |  🔍 筛选: \"%2\"  |  匹配: %3 行  |  共 %4 页")
                                   .arg(fileName).arg(keyword)
                                   .arg(matchCount).arg(state->searchTotalPages));
                infoLabel->setStyleSheet("color: #3fb950; font-size: 13px; padding: 4px 0;");
        
                loadSearchPage(0, keyword);
            }, Qt::SingleShotConnection);
        
            m_ssh->execCommand(m_currentHost, m_currentPort, countCmd);
        };
        
        auto goPage = [=](int page) {
            if (state->isSearchMode) {
                loadSearchPage(page, searchEdit->text().trimmed());
            } else {
                loadNormalPage(page);
            }
        };
        
        connect(btnSearch, &QPushButton::clicked, dlg, doSearch);
        connect(searchEdit, &QLineEdit::returnPressed, dlg, doSearch);
        connect(btnClearSearch, &QPushButton::clicked, dlg, [=]() {
            searchEdit->clear();
            state->isSearchMode = false;
            infoLabel->setStyleSheet("color: #58a6ff; font-size: 13px; padding: 4px 0;");
            loadNormalPage(0);
        });
        
        connect(btnFirst, &QPushButton::clicked, dlg, [=]() { goPage(0); });
        connect(btnPrev, &QPushButton::clicked, dlg, [=]() { goPage(state->currentPage - 1); });
        connect(btnNext, &QPushButton::clicked, dlg, [=]() { goPage(state->currentPage + 1); });
        connect(btnLast, &QPushButton::clicked, dlg, [=]() {
            int pages = state->isSearchMode ? state->searchTotalPages : state->totalPages;
            goPage(pages - 1);
        });
        connect(btnClose, &QPushButton::clicked, dlg, &QDialog::close);
        
        connect(dlg, &QDialog::destroyed, dlg, [state]() { delete state; });
        
        dlg->show();
        loadNormalPage(0);
    }, Qt::SingleShotConnection);

    m_ssh->execCommand(m_currentHost, m_currentPort,
                       QString("wc -l < %1").arg(shellQuote(remotePath)));
}

// ======== 工具函数 ========

void LogCollectWidget::updateConnectionState(bool connected)
{
    m_btnConnect->setEnabled(!connected);
    m_btnDisconnect->setEnabled(connected);
    m_btnExport->setEnabled(connected);
    m_statusLabel->setText(connected ? "● 已连接" : "● 未连接");
    m_statusLabel->setStyleSheet(connected
        ? "color: #3fb950; font-weight: bold; font-size: 14px; padding: 4px 10px; "
          "background: rgba(63, 185, 80, 0.15); border-radius: 4px;"
        : "color: #f85149; font-weight: bold; font-size: 14px; padding: 4px 10px; "
          "background: rgba(248, 81, 73, 0.15); border-radius: 4px;");
}

void LogCollectWidget::appendLog(const QString &msg)
{
    QString time = QDateTime::currentDateTime().toString("HH:mm:ss");
    QString color = "#c9d1d9";

    if (msg.startsWith("[错误]") || msg.startsWith("[失败]"))
        color = "#f85149";
    else if (msg.startsWith("[成功]") || msg.startsWith("[完成]"))
        color = "#3fb950";
    else if (msg.startsWith("[操作]") || msg.startsWith("[下载]"))
        color = "#58a6ff";
    else if (msg.startsWith("[信息]") || msg.startsWith("[文件]"))
        color = "#d2a8ff";
    else if (msg.startsWith("[提示]"))
        color = "#d29922";
    else if (msg.startsWith("[断开]"))
        color = "#f0883e";

    // 在标签后插入空格
    QString formatted = msg;
    int closeIdx = formatted.indexOf(']');
    if (closeIdx > 0 && closeIdx + 1 < formatted.size() && formatted[closeIdx + 1] != ' ')
        formatted.insert(closeIdx + 1, ' ');

    m_logView->append(QString("<span style='color:#6e7681'>[%1]</span>&nbsp;&nbsp;"
                              "<span style='color:%2; font-size:12px'>%3</span>")
                      .arg(time, color, formatted.toHtmlEscaped()));

    // 限制最大行数，防止内存无限增长
    QTextDocument *doc = m_logView->document();
    while (doc->blockCount() > 500) {
        QTextCursor cursor(doc->begin());
        cursor.select(QTextCursor::BlockUnderCursor);
        cursor.removeSelectedText();
        cursor.deleteChar();
    }
}

QString LogCollectWidget::configFilePath() const
{
    QString dir = QCoreApplication::applicationDirPath() + "/ssh_configs";
    QDir().mkpath(dir);
    return dir + "/hosts.json";
}

void LogCollectWidget::loadHostConfigs()
{
    QFile f(configFilePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    f.close();

    m_hosts.clear();
    m_hostTree->clear();
    bool hadStoredPassword = false;
    for (auto v : arr) {
        QJsonObject o = v.toObject();
        HostConfig cfg;
        cfg.name = o["name"].toString();
        cfg.host = o["host"].toString();
        cfg.port = o["port"].toInt(22);
        cfg.user = o["user"].toString("user");
        // 密码只保存在当前进程内，不从明文配置文件恢复。
        hadStoredPassword = hadStoredPassword || o.contains("password");
        cfg.password.clear();
        cfg.note = o["note"].toString();
        for (auto p : o["log_paths"].toArray())
            cfg.logPaths.append(p.toString());
        if (cfg.logPaths.isEmpty())
            cfg.logPaths.append("/var/opt/robot/log/");
        m_hosts.append(cfg);

        auto *item = new QTreeWidgetItem(m_hostTree,
                                         {cfg.name, cfg.host + ":" + QString::number(cfg.port), "离线"});
        item->setForeground(2, QColor("#f85149"));
    }
    // 自动清理旧版本 hosts.json 中曾经明文保存的密码字段。
    if (hadStoredPassword)
        saveHostConfigs();
}

void LogCollectWidget::saveHostConfigs()
{
    QJsonArray arr;
    for (const auto &cfg : m_hosts) {
        QJsonObject o;
        o["name"] = cfg.name;
        o["host"] = cfg.host;
        o["port"] = cfg.port;
        o["user"] = cfg.user;
        // 不把 SSH 密码写入磁盘；重新启动后由用户再次输入。
        o["note"] = cfg.note;
        QJsonArray paths;
        for (const auto &p : cfg.logPaths) paths.append(p);
        o["log_paths"] = paths;
        arr.append(o);
    }
    QFile f(configFilePath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
        f.close();
    }
}

HostConfig LogCollectWidget::currentHostConfig() const
{
    int idx = m_hostTree->indexOfTopLevelItem(m_hostTree->currentItem());
    if (idx >= 0 && idx < m_hosts.size())
        return m_hosts[idx];
    return {};
}
