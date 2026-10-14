#include "mainwindow.h"
#include "parser.h"
#include "translations.h"
#include "widgets.h"

#include <QCloseEvent>
#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTextEdit>
#include <QTimer>
#include "guidance.h"
#include "diagnostics.h"
#include <QBuffer>
#include <QDateTime>
#include <QPageSize>
#include <QComboBox>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPrintDialog>
#include <QPrinter>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSet>
#include <QStackedWidget>
#include <QStyle>
#include <QTableWidget>
#include <QTextDocument>
#include <QPainter>
#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <QTextCursor>
#include <QThread>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

QLabel* label(const QString& text, const QString& objectName = QString())
{
    auto* result = new QLabel(text);
    result->setWordWrap(true);
    if (!objectName.isEmpty()) result->setObjectName(objectName);
    return result;
}

QWidget* panel(QWidget* content, const QString& objectName = QStringLiteral("panel"))
{
    auto* wrapper = new QWidget;
    wrapper->setObjectName(objectName);
    auto* layout = new QVBoxLayout(wrapper);
    layout->setContentsMargins(18, 16, 18, 18);
    layout->addWidget(content);
    return wrapper;
}

QTableWidget* table(const QStringList& headers)
{
    auto* result = new QTableWidget;
    result->setColumnCount(headers.size());
    result->setHorizontalHeaderLabels(headers);
    result->verticalHeader()->hide();
    result->setShowGrid(false);
    result->setAlternatingRowColors(true);
    result->setSelectionBehavior(QAbstractItemView::SelectRows);
    result->setEditTriggers(QAbstractItemView::NoEditTriggers);
    result->setWordWrap(true);
    result->setSortingEnabled(false);
    return result;
}

QToolButton* plotButton(const QString& text, const QString& tooltip, QWidget* parent)
{
    auto* button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("chartToolButton"));
    button->setText(text);
    button->setToolTip(tooltip);
    button->setFixedSize(30, 30);
    button->setAutoRaise(false);
    return button;
}

QToolButton* plotExportButton(QWidget* parent)
{
    auto* button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("chartToolButton"));
    button->setIcon(parent->style()->standardIcon(QStyle::SP_DialogSaveButton));
    button->setToolTip(QStringLiteral("导出当前可见范围"));
    button->setFixedSize(34, 30);
    button->setPopupMode(QToolButton::InstantPopup);
    return button;
}

QString numberOrDash(double value, int decimals = 1)
{
    return std::isfinite(value) && std::abs(value) < 1e90 ? QString::number(value, 'f', decimals) : QStringLiteral("--");
}

QColor severityColor(Severity severity)
{
    switch (severity) {
    case Severity::Critical: return QColor("#ad2f38");
    case Severity::Warning: return QColor("#d15d3f");
    case Severity::Notice: return QColor("#b17b21");
    default: return QColor("#23806d");
    }
}

QString htmlEscaped(const QString& value)
{
    return value.toHtmlEscaped().replace('\n', QStringLiteral("<br>"));
}

}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    qRegisterMetaType<LogReport>("LogReport");
    setAcceptDrops(true);
    setWindowTitle(QStringLiteral("飞析 · ArduPilot 日志分析"));
    setMinimumSize(1024, 680);
    resize(1380, 860);
    QSettings settings(QCoreApplication::applicationDirPath() + "/飞析设置.ini", QSettings::IniFormat);
    _options.vibration = settings.value("rules/vibration", 60.0).toDouble();
    _options.ekfRatio = settings.value("rules/ekf", 1.0).toDouble();
    _options.rollError = settings.value("rules/roll", 15.0).toDouble();
    _options.pitchError = settings.value("rules/pitch", 10.0).toDouble();
    _options.trackingDuration = settings.value("rules/trackingDuration", 1.0).toDouble();
    _options.motorDuration = settings.value("rules/motorDuration", 3.0).toDouble();
    buildUi();
    if (settings.contains("window/geometry")) restoreGeometry(settings.value("window/geometry").toByteArray());
    auto* openShortcut = new QShortcut(QKeySequence::Open, this);
    connect(openShortcut, &QShortcut::activated, this, &MainWindow::chooseLog);
    auto* guideShortcut = new QShortcut(QKeySequence(Qt::Key_F1), this);
    connect(guideShortcut, &QShortcut::activated, this, [this]() { _navigation->setCurrentRow(7); });
    renderRecentFiles();
}

MainWindow::~MainWindow()
{
    if (_parserThread && _parserThread->isRunning()) {
        _parserThread->requestInterruption();
        _parserThread->quit();
        _parserThread->wait();
    }
}

void MainWindow::buildUi()
{
    auto* central = new QWidget;
    auto* root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* sidebar = new QWidget;
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(196);
    auto* sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(16, 22, 14, 18);
    sideLayout->setSpacing(12);
    sideLayout->addWidget(label(QStringLiteral("FLIGHT INSIGHT"), QStringLiteral("brandKicker")));
    sideLayout->addWidget(label(QStringLiteral("飞析"), QStringLiteral("brandTitle")));
    sideLayout->addWidget(label(QStringLiteral("QuadPlane 试飞复盘工作台"), QStringLiteral("brandSubtitle")));

    _navigation = new QListWidget;
    _navigation->setObjectName(QStringLiteral("navigation"));
    _navigation->addItems({
        QStringLiteral("总览"), QStringLiteral("数据浏览"), QStringLiteral("飞行轨迹"),
        QStringLiteral("曲线分析"), QStringLiteral("风险诊断"), QStringLiteral("事件时间线"),
        QStringLiteral("参数检查"), QStringLiteral("分析引导"), QStringLiteral("试飞记录")
    });
    _navigation->setCurrentRow(0);
    sideLayout->addWidget(_navigation, 1);
    auto* rules = new QPushButton(QStringLiteral("诊断门限设置"));
    rules->setObjectName("sidebarButton");
    sideLayout->addWidget(rules);
    connect(rules, &QPushButton::clicked, this, &MainWindow::configureRules);
    auto* demo = new QPushButton(QStringLiteral("打开教学示例"));
    demo->setObjectName("sidebarButton");
    demo->setObjectName("demoButton");
    demo->setStyleSheet("QPushButton { background: #23334d; color: #ccdaf0; border: 1px solid #354864; border-radius: 6px; padding: 9px; text-align: left; }");
    sideLayout->addWidget(demo);
    connect(demo, &QPushButton::clicked, this, &MainWindow::loadDemo);
    sideLayout->addWidget(label(QStringLiteral("飞析 3.0 · 离线分析\nF1 分析引导 · Ctrl+O 打开"), "supportText"));
    sideLayout->addWidget(label(QStringLiteral("支持 ArduPlane · ArduCopter\nQuadPlane · Rover · Sub"), QStringLiteral("supportText")));
    root->addWidget(sidebar);

    auto* workspace = new QWidget;
    auto* workspaceLayout = new QVBoxLayout(workspace);
    workspaceLayout->setContentsMargins(20, 20, 20, 16);
    workspaceLayout->setSpacing(14);

    auto* top = new QHBoxLayout;
    auto* heading = new QVBoxLayout;
    heading->setSpacing(2);
    _fileTitle = label(QStringLiteral("试飞复盘，从这里开始"), QStringLiteral("pageTitle"));
    _fileSubtitle = label(QStringLiteral("将 .BIN 文件拖入窗口，或点击“打开日志”"), QStringLiteral("pageSubtitle"));
    heading->addWidget(_fileTitle);
    heading->addWidget(_fileSubtitle);
    top->addLayout(heading, 1);
    _statusPill = label(QStringLiteral("未载入"), QStringLiteral("statusPill"));
    _statusPill->setAlignment(Qt::AlignCenter);
    _statusPill->setFixedHeight(30);
    top->addWidget(_statusPill, 0, Qt::AlignVCenter);
    _exportButton = new QPushButton(QStringLiteral("导出报告"));
    _exportButton->setObjectName(QStringLiteral("secondaryButton"));
    _exportButton->setEnabled(false);
    auto* exportMenu = new QMenu(_exportButton);
    exportMenu->addAction(QStringLiteral("导出 HTML 报告"), this, &MainWindow::exportHtml);
    exportMenu->addAction(QStringLiteral("导出 PDF 报告"), this, &MainWindow::exportPdf);
    _exportButton->setMenu(exportMenu);
    top->addWidget(_exportButton);
    _openButton = new QPushButton(QStringLiteral("打开日志"));
    _openButton->setObjectName(QStringLiteral("primaryButton"));
    top->addWidget(_openButton);
    _cancelButton = new QPushButton(QStringLiteral("取消分析"));
    _cancelButton->setObjectName("secondaryButton");
    _cancelButton->hide();
    top->addWidget(_cancelButton);
    connect(_cancelButton, &QPushButton::clicked, this, [this]() {
        if (_parserThread) _parserThread->requestInterruption();
        _cancelButton->setEnabled(false);
        _fileSubtitle->setText(QStringLiteral("正在取消，已有分析结果会保留。"));
    });
    workspaceLayout->addLayout(top);

    _progress = new QProgressBar;
    _progress->setRange(0, 100);
    _progress->setTextVisible(true);
    _progress->setFixedHeight(7);
    _progress->hide();
    workspaceLayout->addWidget(_progress);

    _pages = new QStackedWidget;
    _pages->addWidget(createOverviewPage());
    _pages->addWidget(createDataBrowserPage());
    _pages->addWidget(createRoutePage());
    _pages->addWidget(createChartsPage());
    _pages->addWidget(createFindingsPage());
    _pages->addWidget(createEventsPage());
    _pages->addWidget(createParametersPage());
    _pages->addWidget(createGuidePage());
    _pages->addWidget(createNotesPage());
    workspaceLayout->addWidget(_pages, 1);
    root->addWidget(workspace, 1);
    setCentralWidget(central);
    statusBar()->showMessage(QStringLiteral("本机离线分析 · 支持 .BIN 拖入 · F1 打开分析引导"));

    connect(_navigation, &QListWidget::currentRowChanged, this, &MainWindow::navigationChanged);
    connect(_openButton, &QPushButton::clicked, this, &MainWindow::chooseLog);
}

QWidget* MainWindow::createOverviewPage()
{
    auto* page = new QWidget;
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(0, 0, 0, 0);
    _welcomeHost = new QWidget;
    auto* welcome = new QVBoxLayout(_welcomeHost);
    welcome->setContentsMargins(0,0,0,0);
    welcome->setSpacing(14);
    _dropPanel = new DropPanel;
    _dropPanel->setMinimumHeight(220);
    connect(_dropPanel, &DropPanel::browseRequested, this, &MainWindow::chooseLog);
    welcome->addWidget(_dropPanel, 2);
    auto* steps = new QHBoxLayout;
    const QStringList titles = {QStringLiteral("01  自动扫描"), QStringLiteral("02  证据定位"), QStringLiteral("03  引导分析"), QStringLiteral("04  复盘交付")};
    const QStringList details = {QStringLiteral("识别机型与飞行窗口\n列出异常和缺失的检查"), QStringLiteral("点击异常跳转曲线\n保留原始采样与时间"), QStringLiteral("知道先看什么、怎样判断\n逐步排查可能原因"), QStringLiteral("记录结论与后续动作\n导出报告、曲线和数据")};
    for (int i=0;i<4;++i) {
        auto* card=new QWidget; card->setObjectName("metricCard"); auto* l=new QVBoxLayout(card);
        l->setContentsMargins(14,16,14,16); l->addWidget(label(titles[i],"sectionTitle")); l->addWidget(label(details[i],"helperText")); steps->addWidget(card,1);
    }
    welcome->addLayout(steps);
    auto* recentHeader = new QHBoxLayout;
    recentHeader->addWidget(label(QStringLiteral("最近分析"),"sectionTitle")); recentHeader->addStretch();
    auto* learn = new QPushButton(QStringLiteral("先学习分析方法 →")); learn->setObjectName("secondaryButton");
    recentHeader->addWidget(learn); connect(learn,&QPushButton::clicked,this,[this](){_navigation->setCurrentRow(7);});
    welcome->addLayout(recentHeader);
    _recentList=new QListWidget; _recentList->setObjectName("recentList"); _recentList->setMaximumHeight(132);
    connect(_recentList,&QListWidget::itemDoubleClicked,this,[this](QListWidgetItem* item){const QString path=item->data(Qt::UserRole).toString();if(!path.isEmpty())openLog(path);});
    welcome->addWidget(_recentList,1);
    root->addWidget(_welcomeHost,1);

    _overviewContent = new QWidget;
    auto* contentLayout = new QVBoxLayout(_overviewContent);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(14);
    _overviewBanner = label(QString(), QStringLiteral("overviewBanner"));
    contentLayout->addWidget(_overviewBanner);
    auto* actions=new QHBoxLayout;
    for (const auto& action : {qMakePair(QStringLiteral("查看全部异常 →"),4),qMakePair(QStringLiteral("开始引导分析 →"),7),qMakePair(QStringLiteral("填写试飞结论 →"),8)}) {
        auto* b=new QPushButton(action.first);b->setObjectName("secondaryButton");actions->addWidget(b);
        connect(b,&QPushButton::clicked,this,[this,action](){_navigation->setCurrentRow(action.second);});
    }
    actions->addStretch();contentLayout->addLayout(actions);
    _metricsHost = new QWidget;

    contentLayout->addWidget(label(QStringLiteral("优先复核 · 点击一行定位证据曲线"),"sectionTitle"));
    _priorityTable=table({QStringLiteral("级别"),QStringLiteral("证据类型"),QStringLiteral("需要复核的发现"),QStringLiteral("时间 / 区间")});
    _priorityTable->setObjectName("priorityTable");_priorityTable->setMinimumHeight(132);_priorityTable->setMaximumHeight(190);
    _priorityTable->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
    for(int col : {0,1,3})_priorityTable->horizontalHeader()->setSectionResizeMode(col,QHeaderView::ResizeToContents);
    connect(_priorityTable,&QTableWidget::cellClicked,this,[this](int row,int){auto* item=_priorityTable->item(row,0);if(item)findingActivated(item->data(Qt::UserRole).toInt(),0);});
    contentLayout->addWidget(_priorityTable);
    contentLayout->addWidget(_metricsHost);
    contentLayout->addWidget(label(QStringLiteral("电池与能源"), QStringLiteral("sectionTitle")));
    _batteryTable = table({QStringLiteral("电池"), QStringLiteral("最低电压"), QStringLiteral("最大电流"), QStringLiteral("窗口耗电量"), QStringLiteral("窗口能耗"), QStringLiteral("最低余量"), QStringLiteral("最高温度"), QStringLiteral("健康")});
    _batteryTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    _batteryTable->setMaximumHeight(190);
    contentLayout->addWidget(_batteryTable);
    contentLayout->addStretch();

    auto* scroll = new QScrollArea;
    _overviewScroll=scroll;
    scroll->hide();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(_overviewContent);
    root->addWidget(scroll, 1);
    _overviewContent->hide();
    return page;
}

QWidget* MainWindow::createDataBrowserPage()
{
    auto* page = new QWidget;
    auto* root = new QHBoxLayout(page);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(14);

    auto* left = new QWidget;
    left->setObjectName(QStringLiteral("panel"));
    left->setMinimumWidth(220);
    left->setMaximumWidth(440);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(16, 15, 16, 16);
    leftLayout->addWidget(label(QStringLiteral("日志项目"), QStringLiteral("sectionTitle")));
    leftLayout->addWidget(label(QStringLiteral("勾选字段绘图，最多 8 条。不同量纲自动分轴；未知单位保留原始值。"), QStringLiteral("helperText")));
    _dataSearch = new QLineEdit;
    _dataSearch->setObjectName(QStringLiteral("dataSearch"));
    _dataSearch->setClearButtonEnabled(true);
    _dataSearch->setPlaceholderText(QStringLiteral("搜索 ATT、姿态、Roll、横滚角"));
    leftLayout->addWidget(_dataSearch);
    _dataTree = new QTreeWidget;
    _dataTree->setObjectName(QStringLiteral("dataTree"));
    _dataTree->setHeaderHidden(true);
    _dataTree->setUniformRowHeights(true);
    leftLayout->addWidget(_dataTree, 1);
    auto* splitter=new QSplitter;
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(left);
    root->addWidget(splitter,1);

    auto* right = new QWidget;
    right->setObjectName(QStringLiteral("panel"));
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(18, 15, 18, 18);
    auto* plotHeader = new QHBoxLayout;
    plotHeader->addWidget(label(QStringLiteral("字段曲线"), QStringLiteral("sectionTitle")));
    plotHeader->addStretch();
    _selectedSeriesLabel = label(QStringLiteral("已选择 0/8"), QStringLiteral("helperText"));
    plotHeader->addWidget(_selectedSeriesLabel);
    auto* clear=new QPushButton(QStringLiteral("清空"));clear->setObjectName("secondaryButton");plotHeader->addWidget(clear);
    connect(clear,&QPushButton::clicked,this,[this](){_updatingDataTree=true;for(int i=0;i<_dataTree->topLevelItemCount();++i){auto* top=_dataTree->topLevelItem(i);for(int j=0;j<top->childCount();++j)if(top->child(j)->flags().testFlag(Qt::ItemIsUserCheckable))top->child(j)->setCheckState(0,Qt::Unchecked);}_updatingDataTree=false;_dataPlot->setSeriesKeys({});_selectedSeriesLabel->setText(QStringLiteral("已选择 0/8"));});
    auto* dataZoomIn = plotButton(QStringLiteral("+"), QStringLiteral("放大（也可使用鼠标滚轮）"), right);
    auto* dataZoomOut = plotButton(QStringLiteral("-"), QStringLiteral("缩小（也可使用鼠标滚轮）"), right);
    auto* dataFit = plotButton(QStringLiteral("↺"), QStringLiteral("恢复完整曲线（也可双击图表）"), right);
    auto* dataExport = plotExportButton(right);
    plotHeader->addWidget(dataZoomIn);
    plotHeader->addWidget(dataZoomOut);
    plotHeader->addWidget(dataFit);
    plotHeader->addWidget(dataExport);
    rightLayout->addLayout(plotHeader);
    _dataPlot = new PlotWidget;
    _dataPlot->setObjectName(QStringLiteral("dataPlot"));
    attachPlotTools(rightLayout,_dataPlot);
    auto* dataScroll=new QScrollArea;dataScroll->setFrameShape(QFrame::NoFrame);dataScroll->setWidgetResizable(true);dataScroll->setMinimumHeight(280);dataScroll->setWidget(_dataPlot);rightLayout->addWidget(dataScroll,1);
    splitter->addWidget(right);
    splitter->setStretchFactor(1,1);
    splitter->setSizes({270,900});

    connect(_dataSearch, &QLineEdit::textChanged, this, &MainWindow::filterDataTree);
    connect(_dataTree, &QTreeWidget::itemChanged, this, &MainWindow::dataTreeChanged);
    connect(dataZoomIn, &QToolButton::clicked, _dataPlot, &PlotWidget::zoomIn);
    connect(dataZoomOut, &QToolButton::clicked, _dataPlot, &PlotWidget::zoomOut);
    connect(dataFit, &QToolButton::clicked, _dataPlot, &PlotWidget::resetView);
    auto* dataExportMenu = new QMenu(dataExport);
    dataExportMenu->addAction(QStringLiteral("导出当前范围 CSV"), this, [this]() { exportPlotCsv(_dataPlot, QStringLiteral("数据曲线")); });
    dataExportMenu->addAction(QStringLiteral("保存图表 PNG"), this, [this]() { savePlotImage(_dataPlot, QStringLiteral("数据曲线")); });
    dataExport->setMenu(dataExportMenu);
    return page;
}

QWidget* MainWindow::createRoutePage()
{
    _routeWidget = new RouteWidget;
    return panel(_routeWidget);
}

QWidget* MainWindow::createChartsPage()
{
    auto* page = new QWidget;
    auto* root = new QHBoxLayout(page);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(14);

    auto* left = new QWidget;
    left->setObjectName(QStringLiteral("panel"));
    left->setMinimumWidth(220);
    left->setMaximumWidth(440);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(16, 15, 16, 16);
    leftLayout->addWidget(label(QStringLiteral("智能分析项目"), QStringLiteral("sectionTitle")));
    _analysisSearch = new QLineEdit;
    _analysisSearch->setPlaceholderText(QStringLiteral("搜索姿态、PID、TECS、垂起、电池"));
    leftLayout->addWidget(_analysisSearch);
    _analysisTree = new QTreeWidget;
    _analysisTree->setObjectName(QStringLiteral("analysisTree"));
    _analysisTree->setHeaderHidden(true);
    _analysisTree->setUniformRowHeights(true);
    leftLayout->addWidget(_analysisTree, 1);
    auto* splitter=new QSplitter;
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(left);
    root->addWidget(splitter,1);

    auto* right = new QWidget;
    right->setObjectName(QStringLiteral("panel"));
    auto* layout = new QVBoxLayout(right);
    layout->setContentsMargins(18, 15, 18, 16);
    auto* header = new QHBoxLayout;
    header->addWidget(label(QStringLiteral("关键飞行曲线"), QStringLiteral("sectionTitle")));
    header->addStretch();
    auto* chartZoomIn = plotButton(QStringLiteral("+"), QStringLiteral("放大（也可使用鼠标滚轮）"), right);
    auto* chartZoomOut = plotButton(QStringLiteral("-"), QStringLiteral("缩小（也可使用鼠标滚轮）"), right);
    auto* chartFit = plotButton(QStringLiteral("↺"), QStringLiteral("恢复完整曲线（也可双击图表）"), right);
    auto* chartExport = plotExportButton(right);
    header->addWidget(chartZoomIn);
    header->addWidget(chartZoomOut);
    header->addWidget(chartFit);
    header->addWidget(chartExport);
    layout->addLayout(header);
    _analysisInsight = label(QStringLiteral("载入日志后自动生成分析项目。"), QStringLiteral("analysisInsight"));
    layout->addWidget(_analysisInsight);
    _chartPlot = new PlotWidget;
    _chartPlot->setObjectName(QStringLiteral("chartPlot"));
    attachPlotTools(layout,_chartPlot);
    auto* chartScroll=new QScrollArea;chartScroll->setFrameShape(QFrame::NoFrame);chartScroll->setWidgetResizable(true);chartScroll->setMinimumHeight(280);chartScroll->setWidget(_chartPlot);layout->addWidget(chartScroll,1);
    _analysisStats = table({QStringLiteral("当前范围曲线"), QStringLiteral("最小值"), QStringLiteral("最大值"), QStringLiteral("平均值"), QStringLiteral("样本数")});
    _analysisStats->setObjectName(QStringLiteral("analysisStats"));
    _analysisStats->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < 5; ++column) _analysisStats->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    _analysisStats->setMaximumHeight(150);
    layout->addWidget(_analysisStats);
    splitter->addWidget(right);
    splitter->setStretchFactor(1,1);
    splitter->setSizes({270,900});

    connect(_analysisSearch, &QLineEdit::textChanged, this, &MainWindow::filterAnalysisProjects);
    connect(_analysisTree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item,QTreeWidgetItem*){analysisProjectChanged(item,0);});
    connect(chartZoomIn, &QToolButton::clicked, _chartPlot, &PlotWidget::zoomIn);
    connect(chartZoomOut, &QToolButton::clicked, _chartPlot, &PlotWidget::zoomOut);
    connect(chartFit, &QToolButton::clicked, _chartPlot, &PlotWidget::resetView);
    auto* chartExportMenu = new QMenu(chartExport);
    chartExportMenu->addAction(QStringLiteral("导出当前范围 CSV"), this, [this]() { exportPlotCsv(_chartPlot, QStringLiteral("关键飞行曲线")); });
    chartExportMenu->addAction(QStringLiteral("保存图表 PNG"), this, [this]() { savePlotImage(_chartPlot, QStringLiteral("关键飞行曲线")); });
    chartExport->setMenu(chartExportMenu);
    connect(_chartPlot, &PlotWidget::viewChanged, this, &MainWindow::updateAnalysisStats);
    return page;
}

void MainWindow::exportPlotCsv(PlotWidget* plot, const QString& baseName)
{
    if (!plot || plot->seriesKeys().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("没有可导出的曲线"), QStringLiteral("请先选择至少一个数值字段。"));
        return;
    }
    const QString suggested = QFileInfo(_report.fileName).completeBaseName() + QLatin1Char('_') + baseName + QStringLiteral(".csv");
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出当前可见范围"), suggested, QStringLiteral("CSV 数据 (*.csv)"));
    if (!path.isEmpty() && !plot->exportVisibleCsv(path)) QMessageBox::warning(this, QStringLiteral("导出失败"), QStringLiteral("无法写入所选文件。"));
}

void MainWindow::savePlotImage(PlotWidget* plot, const QString& baseName)
{
    if (!plot || plot->seriesKeys().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("没有可保存的图表"), QStringLiteral("请先选择至少一个数值字段。"));
        return;
    }
    const QString suggested = QFileInfo(_report.fileName).completeBaseName() + QLatin1Char('_') + baseName + QStringLiteral(".png");
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("保存当前图表"), suggested, QStringLiteral("PNG 图片 (*.png)"));
    if (!path.isEmpty() && !plot->savePng(path)) QMessageBox::warning(this, QStringLiteral("保存失败"), QStringLiteral("无法写入所选文件。"));
}

QWidget* MainWindow::createFindingsPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    _findingSummary = label(QStringLiteral("导入日志后生成诊断结果。"), QStringLiteral("overviewBanner"));
    layout->addWidget(_findingSummary);
    auto* filters=new QHBoxLayout;_findingSearch=new QLineEdit;_findingSearch->setObjectName("findingSearch");_findingSearch->setClearButtonEnabled(true);_findingSearch->setPlaceholderText(QStringLiteral("搜索异常、证据或系统"));_findingSeverity=new QComboBox;_findingSeverity->setObjectName("findingSeverity");
    _findingSeverity->addItems({QStringLiteral("全部级别"),QStringLiteral("严重 / 警告"),QStringLiteral("仅飞行实测与事件")});filters->addWidget(_findingSearch,1);filters->addWidget(_findingSeverity);layout->addLayout(filters);
    auto apply=[this](){if(!_findingTable)return;for(int row=0;row<_findingTable->rowCount();++row){const Finding& f=_report.findings[row];const bool match=(f.title+f.category+f.evidence+f.detail).contains(_findingSearch->text(),Qt::CaseInsensitive);const bool level=_findingSeverity->currentIndex()==0 || (_findingSeverity->currentIndex()==1 && f.severity>=Severity::Warning) || (_findingSeverity->currentIndex()==2 && f.timeUs>0);_findingTable->setRowHidden(row,!match || !level);}};
    connect(_findingSearch,&QLineEdit::textChanged,this,[apply](const QString&){apply();});connect(_findingSeverity,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[apply](int){apply();});
    _findingTable = table({QStringLiteral("级别"), QStringLiteral("类别"), QStringLiteral("发现"), QStringLiteral("说明与建议"), QStringLiteral("证据与规则"), QStringLiteral("时间 / 区间")});
    _findingTable->setObjectName(QStringLiteral("findingTable"));
    _findingTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    _findingTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    _findingTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    _findingTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    _findingTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Interactive);
    _findingTable->setColumnWidth(4,210);
    _findingTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    layout->addWidget(_findingTable, 1);
    connect(_findingTable, &QTableWidget::cellDoubleClicked, this, &MainWindow::findingActivated);
    return page;
}

QWidget* MainWindow::createEventsPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* filters = new QHBoxLayout;
    _eventSearch = new QLineEdit;
    _eventSearch->setPlaceholderText(QStringLiteral("搜索模式、转换、任务、错误"));
    _eventCategory = new QComboBox;
    _eventCategory->addItem(QStringLiteral("全部类别"));
    filters->addWidget(_eventSearch, 1);
    filters->addWidget(_eventCategory);
    layout->addLayout(filters);
    _eventTable = table({QStringLiteral("时间"), QStringLiteral("级别"), QStringLiteral("类别"), QStringLiteral("事件"), QStringLiteral("详情")});
    _eventTable->setObjectName(QStringLiteral("eventTable"));
    _eventTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    _eventTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    _eventTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    _eventTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    _eventTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    layout->addWidget(_eventTable, 1);
    connect(_eventSearch, &QLineEdit::textChanged, this, &MainWindow::filterEvents);
    connect(_eventCategory, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::filterEvents);
    connect(_eventTable, &QTableWidget::cellDoubleClicked, this, &MainWindow::eventActivated);
    return page;
}

QWidget* MainWindow::createParametersPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* filters = new QHBoxLayout;
    _parameterSearch = new QLineEdit;
    _parameterSearch->setPlaceholderText(QStringLiteral("搜索 Q_OPTIONS、垂起、电池、空速"));
    _parameterCategory = new QComboBox;
    _parameterCategory->addItem(QStringLiteral("全部系统"));
    filters->addWidget(_parameterSearch, 1);
    filters->addWidget(_parameterCategory);
    layout->addLayout(filters);
    _parameterTable = table({QStringLiteral("参数名"), QStringLiteral("中文释义"), QStringLiteral("系统"), QStringLiteral("当前值"), QStringLiteral("默认值"), QStringLiteral("状态")});
    _parameterTable->setObjectName("parameterTable");
    _parameterTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    _parameterTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    _parameterTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    _parameterTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    _parameterTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    _parameterTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    layout->addWidget(_parameterTable, 1);
    connect(_parameterSearch, &QLineEdit::textChanged, this, &MainWindow::filterParameters);
    connect(_parameterCategory, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::filterParameters);
    return page;
}

void MainWindow::chooseLog()
{
    if(_busy)return;
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择 ArduPilot DataFlash 日志"), QSettings(QCoreApplication::applicationDirPath()+"/飞析设置.ini",QSettings::IniFormat).value("lastDirectory").toString(), QStringLiteral("ArduPilot 日志 (*.bin *.BIN);;所有文件 (*.*)"));
    if (!path.isEmpty()) startAnalysis(path);
}

void MainWindow::openLog(const QString& filePath)
{
    startAnalysis(filePath);
}

const LogReport& MainWindow::report() const
{
    return _report;
}

void MainWindow::startAnalysis(const QString& filePath)
{
    if (_parserThread && _parserThread->isRunning()) {
        QMessageBox::information(this, QStringLiteral("正在分析"), QStringLiteral("请等待当前日志分析完成。"));
        return;
    }
    const QFileInfo file(filePath);
    if (!file.exists() || !file.isFile()) {
        QMessageBox::warning(this, QStringLiteral("文件不存在"), QStringLiteral("无法找到所选日志文件。"));
        return;
    }
    saveNotes();
    _pendingFile = filePath;
    QSettings(QCoreApplication::applicationDirPath()+"/飞析设置.ini",QSettings::IniFormat).setValue("lastDirectory",file.absolutePath());
    _fileTitle->setText(file.fileName());
    _fileSubtitle->setText(QStringLiteral("%1 · 正在读取日志结构").arg(formatFileSize(file.size())));
    setBusy(true);

    _parserThread = new QThread(this);
    _parser = new LogParser;
    _parser->setDiagnosticOptions(_options);
    _parser->moveToThread(_parserThread);
    connect(_parserThread, &QThread::started, _parser, [this, filePath]() { _parser->parse(filePath); });
    connect(_parser, &LogParser::progress, this, &MainWindow::analysisProgress);
    connect(_parser, &LogParser::finished, this, &MainWindow::analysisFinished);
    connect(_parser, &LogParser::finished, _parserThread, &QThread::quit, Qt::DirectConnection);
    connect(_parserThread, &QThread::finished, _parser, &QObject::deleteLater);
    connect(_parserThread, &QThread::finished, _parserThread, &QObject::deleteLater);
    QThread* activeThread=_parserThread;
    connect(_parserThread, &QThread::finished, this, [this,activeThread]() { if(_parserThread==activeThread){_parser=nullptr;_parserThread=nullptr;} });
    _parserThread->start();
}

void MainWindow::analysisProgress(int percent, const QString& phase)
{
    _progress->setValue(percent);
    _progress->setFormat(phase + QStringLiteral("  %p%"));
    _fileSubtitle->setText(QStringLiteral("%1 · %2").arg(formatFileSize(QFileInfo(_pendingFile).size()), phase));
}

void MainWindow::analysisFinished(const LogReport& report)
{
    setBusy(false);
    if (!report.valid) {
        _statusPill->setText(report.cancelled ? QStringLiteral("已取消") : QStringLiteral("解析失败"));
        _fileTitle->setText(_report.valid ? _report.fileName : QStringLiteral("导入未完成"));
        _fileSubtitle->setText(report.error + (_report.valid ? QStringLiteral(" 已保留上一次分析结果。") : QString()));
        statusBar()->showMessage(report.error,15000);
        return;
    }
    _report=report;
    _report.demo=QFileInfo(report.filePath).canonicalFilePath()==QFileInfo(QCoreApplication::applicationDirPath()+"/示例日志/QuadPlane_教学示例.BIN").canonicalFilePath();
    QSettings settings(QCoreApplication::applicationDirPath()+"/飞析设置.ini",QSettings::IniFormat);
    QStringList recent=settings.value("recentFiles").toStringList();recent.removeAll(report.filePath);recent.prepend(report.filePath);recent=recent.mid(0,8);settings.setValue("recentFiles",recent);
    renderRecentFiles();
    renderReport();
    loadNotes();
    emit analysisComplete();
}

void MainWindow::setBusy(bool busy)
{
    _busy=busy;
    _openButton->setVisible(!busy);
    _openButton->setEnabled(!busy);
    _cancelButton->setVisible(busy);
    _cancelButton->setEnabled(busy);
    _exportButton->setEnabled(!busy && _report.valid);
    _navigation->setEnabled(!busy);
    _progress->setVisible(busy);
    if (busy) {
        _progress->setValue(0);
        _statusPill->setStyleSheet(QString());
        _statusPill->setText(QStringLiteral("分析中"));
    }
}

void MainWindow::renderReport()
{
    _fileTitle->setText(_report.fileName);
    _fileSubtitle->setText(QStringLiteral("%1 · %2 · %3 · %4")
                           .arg(_report.firmware, _report.vehicleName, _report.layoutName, formatFileSize(_report.fileSize)));
    const int critical = std::count_if(_report.findings.cbegin(), _report.findings.cend(), [](const Finding& item) { return item.severity == Severity::Critical; });
    const int warnings = std::count_if(_report.findings.cbegin(), _report.findings.cend(), [](const Finding& item) { return item.severity == Severity::Warning; });
    _statusPill->setStyleSheet(critical?QStringLiteral("background:#fde8e7;color:#b5403b;border-radius:12px;padding:4px 10px;"):warnings?QStringLiteral("background:#fff0dd;color:#ac701a;border-radius:12px;padding:4px 10px;"):QStringLiteral("background:#e5f3ed;color:#2b7e64;border-radius:12px;padding:4px 10px;"));
    _statusPill->setText(critical ? QStringLiteral("存在严重问题") : warnings ? QStringLiteral("发现风险") : QStringLiteral("分析完成"));
    _exportButton->setEnabled(true);
    _welcomeHost->hide();
    _overviewScroll->show();
    _overviewContent->show();
    renderOverview();
    renderDataBrowser();
    _routeWidget->setReport(&_report);
    _chartPlot->setReport(&_report);
    renderAnalysisProjects();
    _evidencePlot=_chartPlot;
    renderFindings();
    renderEvents();
    renderParameters();
    updateGuide();
    statusBar()->showMessage(_report.demo ? QStringLiteral("教学示例 · 人工合成数据，请用于练习分析流程") : QStringLiteral("原始采样已保留 · 点击异常定位 · F1 查看分析引导"));
    _navigation->setCurrentRow(0);
}

void MainWindow::renderOverview()
{
    const int critical = std::count_if(_report.findings.cbegin(), _report.findings.cend(), [](const Finding& item) { return item.severity == Severity::Critical; });
    const int warnings = std::count_if(_report.findings.cbegin(), _report.findings.cend(), [](const Finding& item) { return item.severity == Severity::Warning; });
    const int notices = std::count_if(_report.findings.cbegin(), _report.findings.cend(), [](const Finding& item) { return item.severity == Severity::Notice; });
    _overviewBanner->setText(critical
        ? QStringLiteral("本日志触发 %1 项严重提示、%2 项警告。优先复核下方证据，确认现象与原因。").arg(critical).arg(warnings)
        : warnings
            ? QStringLiteral("本次飞行发现 %1 项警告、%2 项关注事项。建议结合发生时间和曲线逐项核对。").arg(warnings).arg(notices)
            : QStringLiteral("当前规则未发现高风险项，共有 %1 项关注或说明。仍需结合任务目标和机型限制复核。").arg(notices));

    const int available=std::count_if(_report.coverage.cbegin(),_report.coverage.cend(),[](const RuleCoverage& c){return c.available;});
    _overviewBanner->setText((_report.demo ? QStringLiteral("【人工合成教学日志】 ") : QString()) + _overviewBanner->text() + QStringLiteral("  检查覆盖 %1/%2；缺失项请到分析引导中查看。").arg(available).arg(_report.coverage.size()));
    _priorityTable->setRowCount(0);
    for(int i=0;i<_report.findings.size() && _priorityTable->rowCount()<3;++i){
        const Finding& f=_report.findings[i]; if(f.severity<Severity::Notice)continue;
        const int row=_priorityTable->rowCount();_priorityTable->insertRow(row);
        auto* severity=new QTableWidgetItem(severityName(f.severity));severity->setForeground(severityColor(f.severity));severity->setData(Qt::UserRole,i);_priorityTable->setItem(row,0,severity);
        _priorityTable->setItem(row,1,new QTableWidgetItem(f.source));_priorityTable->setItem(row,2,new QTableWidgetItem(f.title));
        _priorityTable->setItem(row,3,new QTableWidgetItem(f.timeUs ? formatClock(f.timeUs,_report.flightStartUs,true) : QStringLiteral("配置 / 整段")));_priorityTable->setRowHeight(row,38);
    }
    if(_priorityTable->rowCount()==0){_priorityTable->setRowCount(1);_priorityTable->setItem(0,2,new QTableWidgetItem(QStringLiteral("当前规则未触发异常，请继续按任务目标复核。")));}
    if (QLayout* old = _metricsHost->layout()) {
        while (QLayoutItem* item = old->takeAt(0)) { delete item->widget(); delete item; }
        delete old;
    }
    struct MetricData { QString name, value, detail; };
    QVector<MetricData> metrics = {
        {QStringLiteral("飞行器"), _report.vehicleName, _report.layoutName},
        {QStringLiteral("飞行时长"), formatDuration(_report.flightEndUs - _report.flightStartUs), _report.flightWindowInferred ? QStringLiteral("根据运动与上锁事件推断") : QStringLiteral("根据解锁/上锁记录计算")},
        {QStringLiteral("飞行距离"), QStringLiteral("%1 km").arg(_report.distanceMeters / 1000.0, 0, 'f', 2), QStringLiteral("按融合位置轨迹累计")},
        {QStringLiteral("最大相对高度"), numberOrDash(_report.maxRelativeAltitude, 1) + QStringLiteral(" m"), QStringLiteral("POS.RelHomeAlt（相对返航点高度）")},
        {QStringLiteral("最大地速"), numberOrDash(_report.maxGroundSpeed, 1) + QStringLiteral(" m/s"), QStringLiteral("GPS.Spd（地速）")},
        {QStringLiteral("最大空速"), numberOrDash(_report.maxAirSpeed, 1) + QStringLiteral(" m/s"), QStringLiteral("ARSP.Airspeed（空速）")},
        {QStringLiteral("最大振动"), numberOrDash(_report.maxVibration, 1) + QStringLiteral(" m/s²"), QStringLiteral("VIBE（机体振动）")},
        {QStringLiteral("最低卫星数"), _report.minSatellites == 999 ? QStringLiteral("--") : QString::number(_report.minSatellites), QStringLiteral("GPS.NSats（卫星数量）")},
        {QStringLiteral("实测错误 / 保护消息"), QStringLiteral("%1 / %2").arg(_report.errorCount).arg(_report.failsafeCount), QStringLiteral("在事件时间线核对原始记录")}
    };
    if (_report.vehicleKind == VehicleKind::QuadPlane) {
        metrics.append({QStringLiteral("Position1 介入距离"), _report.vtolPosition1Distance >= 0 ? QStringLiteral("%1 m").arg(_report.vtolPosition1Distance, 0, 'f', 1) : QStringLiteral("--"), QStringLiteral("QPOS（垂起位置控制）")});
        metrics.append({QStringLiteral("Position2 距离"), _report.vtolPosition2Distance >= 0 ? QStringLiteral("%1 m").arg(_report.vtolPosition2Distance, 0, 'f', 1) : QStringLiteral("--"), QStringLiteral("进入第二位置控制阶段")});
    }
    auto* grid = new QGridLayout(_metricsHost);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(12);
    for (int index = 0; index < metrics.size(); ++index) {
        auto* card = new QWidget;
        card->setObjectName(QStringLiteral("metricCard"));
        auto* layout = new QVBoxLayout(card);
        layout->setContentsMargins(16, 13, 16, 14);
        layout->setSpacing(4);
        layout->addWidget(label(metrics[index].name, QStringLiteral("metricName")));
        layout->addWidget(label(metrics[index].value, QStringLiteral("metricValue")));
        layout->addWidget(label(metrics[index].detail, QStringLiteral("metricDetail")));
        grid->addWidget(card, index / 3, index % 3);
    }

    _batteryTable->setRowCount(_report.batteries.size());
    for (int row = 0; row < _report.batteries.size(); ++row) {
        const BatterySummary& battery = _report.batteries.at(row);
        const QStringList values = {
            QStringLiteral("BAT %1（电池 %2）").arg(battery.instance).arg(battery.instance + 1),
            numberOrDash(battery.minVoltage, 2) + QStringLiteral(" V"),
            numberOrDash(battery.maxCurrent, 1) + QStringLiteral(" A"),
            numberOrDash(battery.consumedAh, 2) + QStringLiteral(" Ah"),
            numberOrDash(battery.consumedWh, 1) + QStringLiteral(" Wh"),
            numberOrDash(battery.minRemainingPercent, 0) + QStringLiteral(" %"),
            numberOrDash(battery.maxTemperature, 1) + QStringLiteral(" °C"),
            battery.healthy ? QStringLiteral("正常") : QStringLiteral("异常记录")
        };
        for (int column = 0; column < values.size(); ++column) _batteryTable->setItem(row, column, new QTableWidgetItem(values.at(column)));
    }
}

void MainWindow::renderDataBrowser()
{
    _updatingDataTree = true;
    _dataSearch->clear();
    _dataTree->clear();
    for (const LogMessageSchema& schema : _report.messageSchemas) {
        auto* messageItem = new QTreeWidgetItem(_dataTree, {QStringLiteral("%1（%2）").arg(schema.name, schema.chineseName)});
        messageItem->setData(0, Qt::UserRole + 1, schema.name);
        for (const LogFieldSchema& field : schema.fields) {
            auto* fieldItem = new QTreeWidgetItem(messageItem, {QStringLiteral("%1（%2）").arg(field.name, field.chineseName)});
            const QString key = schema.name + QLatin1Char('.') + field.name;
            fieldItem->setData(0, Qt::UserRole, key);
            if (field.numeric && field.name != QStringLiteral("TimeUS") && field.name != QStringLiteral("TimeMS") && findSeries(key)) {
                fieldItem->setFlags(fieldItem->flags() | Qt::ItemIsUserCheckable);
                fieldItem->setCheckState(0, Qt::Unchecked);
            } else {
                fieldItem->setForeground(0, QColor("#8b999d"));
            }
        }
    }
    _updatingDataTree = false;
    _dataPlot->setReport(&_report);
    _dataPlot->setSeriesKeys({});
    _selectedSeriesLabel->setText(QStringLiteral("已选择 0/8"));
}

void MainWindow::dataTreeChanged(QTreeWidgetItem* item, int)
{
    if (_updatingDataTree || !item || !item->parent()) return;
    QStringList keys;
    for (int topIndex = 0; topIndex < _dataTree->topLevelItemCount(); ++topIndex) {
        QTreeWidgetItem* top = _dataTree->topLevelItem(topIndex);
        for (int childIndex = 0; childIndex < top->childCount(); ++childIndex) {
            QTreeWidgetItem* child = top->child(childIndex);
            if (child->checkState(0) == Qt::Checked) keys.append(child->data(0, Qt::UserRole).toString());
        }
    }
    if (keys.size() > 8) {
        _updatingDataTree = true;
        item->setCheckState(0, Qt::Unchecked);
        _updatingDataTree = false;
        keys.removeAll(item->data(0, Qt::UserRole).toString());
        _statusPill->setText(QStringLiteral("最多选择 8 条曲线"));
    }
    _selectedSeriesLabel->setText(QStringLiteral("已选择 %1/8").arg(keys.size()));
    _dataPlot->setSeriesKeys(keys);
}

void MainWindow::filterDataTree(const QString& text)
{
    const QString query = text.trimmed();
    for (int topIndex = 0; topIndex < _dataTree->topLevelItemCount(); ++topIndex) {
        QTreeWidgetItem* top = _dataTree->topLevelItem(topIndex);
        bool anyVisible = query.isEmpty() || top->text(0).contains(query, Qt::CaseInsensitive);
        for (int childIndex = 0; childIndex < top->childCount(); ++childIndex) {
            QTreeWidgetItem* child = top->child(childIndex);
            const bool visible = query.isEmpty() || top->text(0).contains(query, Qt::CaseInsensitive) || child->text(0).contains(query, Qt::CaseInsensitive);
            child->setHidden(!visible);
            anyVisible = anyVisible || visible;
        }
        top->setHidden(!anyVisible);
        if (!query.isEmpty() && anyVisible) top->setExpanded(true);
    }
}

const Series* MainWindow::findSeries(const QString& key) const
{
    for (const Series& series : _report.series) if (series.key == key && !series.points.isEmpty()) return &series;
    return nullptr;
}

QString MainWindow::projectIdForCategory(const QString& category) const
{
    if (category.contains(QStringLiteral("姿态控制"))) return QStringLiteral("attitude");
    if (category.contains(QStringLiteral("垂起动力"))) return QStringLiteral("q_motors");
    if (category.contains(QStringLiteral("垂起着陆"))) return QStringLiteral("q_landing");
    if (category.contains(QStringLiteral("垂起转换"))) return QStringLiteral("q_transition");
    if (category.contains(QStringLiteral("空速")) || category.contains(QStringLiteral("失速"))) return QStringLiteral("airspeed");
    if (category.contains(QStringLiteral("电池"))) return QStringLiteral("battery_0");
    if (category.contains(QStringLiteral("EKF"))) return QStringLiteral("ekf");
    if (category.contains(QStringLiteral("定位"))) return QStringLiteral("gps");
    if (category.contains(QStringLiteral("振动")) || category.contains(QStringLiteral("惯性"))) return QStringLiteral("vibration");
    if (category.contains(QStringLiteral("系统错误")) || category.contains(QStringLiteral("电源"))) return QStringLiteral("power");
    if (category.contains(QStringLiteral("飞行模式"))) return QStringLiteral("flight_mode");
    return QStringLiteral("flight_summary");
}

void MainWindow::renderAnalysisProjects()
{
    _activeAnalysisIndex=-1;
    _focusedFindingIndex=-1;
    _analysisSearch->clear();
    _analysisProjects.clear();
    _analysisTree->clear();

    auto available = [this](const QStringList& candidates) {
        QStringList keys;
        for (const QString& key : candidates) {
            if (findSeries(key) && !keys.contains(key)) keys.append(key);
            if (keys.size() == 8) break;
        }
        return keys;
    };
    auto add = [&](const QString& id, const QString& group, const QString& title, const QString& reason, const QStringList& candidates) {
        const QStringList keys = available(candidates);
        if (!keys.isEmpty()) _analysisProjects.append({id, group, title, reason, keys, 0});
    };

    add(QStringLiteral("flight_summary"), QStringLiteral("飞行状态"), QStringLiteral("飞行全貌"),
        QStringLiteral("把高度、地速、空速和主电池电压放在同一时间轴，快速定位任务阶段与整体趋势。"),
        {"altitude","groundspeed","airspeed","bat0_voltage"});
    add(QStringLiteral("flight_mode"), QStringLiteral("飞行状态"), QStringLiteral("模式与飞行状态"),
        QStringLiteral("对照飞行模式、高度和速度，检查模式切换前后的飞行状态。"),
        {"MODE.Mode","altitude","groundspeed","airspeed"});
    add(QStringLiteral("attitude"), QStringLiteral("姿态与控制"), QStringLiteral("姿态目标跟踪"),
        QStringLiteral("比较目标横滚/俯仰与实际姿态，观察控制跟随、超调和振荡。"),
        {"ATT.DesRoll","ATT.Roll","ATT.DesPitch","ATT.Pitch"});
    add(QStringLiteral("yaw"), QStringLiteral("姿态与控制"), QStringLiteral("航向目标跟踪"),
        QStringLiteral("比较目标航向与实际航向，检查转弯和航向保持表现。"),
        {"ATT.DesYaw","ATT.Yaw"});
    add(QStringLiteral("rate"), QStringLiteral("姿态与控制"), QStringLiteral("角速度目标跟踪"),
        QStringLiteral("比较横滚、俯仰和偏航角速度目标与实际值。"),
        {"RATE.RDes","RATE.R","RATE.PDes","RATE.P","RATE.YDes","RATE.Y"});
    add(QStringLiteral("pid_roll"), QStringLiteral("姿态与控制"), QStringLiteral("横滚 PID 分解"),
        QStringLiteral("同时查看目标、实际、误差以及 P/I/D/FF 分量，辅助判断横滚控制品质。"),
        {"PIDR.Tar","PIDR.Act","PIDR.Err","PIDR.P","PIDR.I","PIDR.D","PIDR.FF"});
    add(QStringLiteral("pid_pitch"), QStringLiteral("姿态与控制"), QStringLiteral("俯仰 PID 分解"),
        QStringLiteral("同时查看目标、实际、误差以及 P/I/D/FF 分量，辅助判断俯仰控制品质。"),
        {"PIDP.Tar","PIDP.Act","PIDP.Err","PIDP.P","PIDP.I","PIDP.D","PIDP.FF"});
    add(QStringLiteral("tecs_speed"), QStringLiteral("固定翼专项"), QStringLiteral("TECS 速度控制"),
        QStringLiteral("比较目标空速、估算空速和速度变化率，检查固定翼能量分配。"),
        {"TECS.spdem","TECS.sp","TECS.dspdem","TECS.dsp","airspeed","groundspeed"});
    add(QStringLiteral("tecs_height"), QStringLiteral("固定翼专项"), QStringLiteral("TECS 高度与油门"),
        QStringLiteral("比较目标高度、实际高度、升降率和油门需求，检查爬升与下降控制。"),
        {"TECS.hdem","TECS.h","TECS.dhdem","TECS.dh","TECS.th"});
    add(QStringLiteral("navigation"), QStringLiteral("固定翼专项"), QStringLiteral("航迹与导航误差"),
        QStringLiteral("查看航点距离、横向偏差、积分项、高度误差和空速误差。"),
        {"NTUN.Dist","NTUN.XT","NTUN.XTi","NTUN.AltE","NTUN.AsE","NTUN.TAsp"});
    add(QStringLiteral("surfaces"), QStringLiteral("固定翼专项"), QStringLiteral("舵面与推进输出"),
        QStringLiteral("查看副翼、升降舵、方向舵、油门和襟翼的控制输出。"),
        {"AETR.Ail","AETR.Elev","AETR.Rudd","AETR.Thr","AETR.Flap","AETR.Steer"});
    add(QStringLiteral("airspeed"), QStringLiteral("固定翼专项"), QStringLiteral("空速传感器健康"),
        QStringLiteral("同时查看空速、差压、原始压力、零偏、温度和健康标志。"),
        {"ARSP.Airspeed","ARSP.DiffPress","ARSP.RawPress","ARSP.Offset","ARSP.Temp","ARSP.H"});

    add(QStringLiteral("q_landing"), QStringLiteral("垂起专项"), QStringLiteral("垂起着陆进近"),
        QStringLiteral("查看到落点距离、目标速度、目标减速度、垂起阶段以及实际地速/空速。"),
        {"QPOS.Dist","QPOS.TSpd","QPOS.TAcc","QPOS.State","groundspeed","airspeed","altitude"});
    add(QStringLiteral("q_transition"), QStringLiteral("垂起专项"), QStringLiteral("固定翼/旋翼转换"),
        QStringLiteral("对照转换状态、空速、地速、油门和高度，分析前后转换过程。"),
        {"QTUN.Trn","airspeed","groundspeed","QTUN.ThO","QTUN.Alt","altitude"});
    add(QStringLiteral("q_height"), QStringLiteral("垂起专项"), QStringLiteral("垂起高度与下降率"),
        QStringLiteral("比较目标高度、实际高度、气压高度、目标爬升率和实际爬升率。"),
        {"QTUN.DAlt","QTUN.Alt","QTUN.BAlt","QTUN.DCRt","QTUN.CRt","QTUN.ThO"});
    add(QStringLiteral("q_throttle"), QStringLiteral("垂起专项"), QStringLiteral("旋翼油门与辅助"),
        QStringLiteral("查看输入/输出/悬停油门、空速辅助、姿态辅助和混控状态。"),
        {"QTUN.ThI","QTUN.ThO","QTUN.ThH","QTUN.ABst","QTUN.Ast","QTUN.TMix","QTUN.Trn"});

    QStringList vtolMotors;
    for (int channel = 1; channel <= 32; ++channel) {
        const auto parameter = _report.parameters.constFind(QStringLiteral("SERVO%1_FUNCTION").arg(channel));
        if (parameter == _report.parameters.constEnd()) continue;
        const int function = qRound(parameter.value().value);
        if (function >= 33 && function <= 40) vtolMotors.append(QStringLiteral("RCOU.C%1").arg(channel));
    }
    add(QStringLiteral("q_motors"), QStringLiteral("垂起专项"), QStringLiteral("垂起电机一致性"),
        QStringLiteral("按舵机功能参数自动识别垂起电机通道，比较各电机输出是否一致。"), vtolMotors);

    for (const BatterySummary& battery : _report.batteries) {
        const QString prefix = QStringLiteral("bat%1_").arg(battery.instance);
        add(QStringLiteral("battery_%1").arg(battery.instance), QStringLiteral("动力与电气"),
            QStringLiteral("电池 %1 能源状态").arg(battery.instance + 1),
            QStringLiteral("查看电压、电流、剩余电量、累计容量、能耗和温度。"),
            {prefix + "voltage", prefix + "current", prefix + "remaining", prefix + "temperature"});
    }
    add(QStringLiteral("power"), QStringLiteral("动力与电气"), QStringLiteral("飞控与舵机供电"),
        QStringLiteral("检查飞控主电源和舵机电源是否发生压降或异常波动。"),
        {"POWR.Vcc","POWR.VServo","board_voltage"});

    add(QStringLiteral("gps"), QStringLiteral("导航与传感器"), QStringLiteral("GPS 定位质量"),
        QStringLiteral("结合卫星数、HDop、定位状态、地速和垂直速度判断定位质量。"),
        {"GPS.NSats","GPS.HDop","GPS.Status","GPS.Spd","GPS.VZ"});
    add(QStringLiteral("ekf"), QStringLiteral("导航与传感器"), QStringLiteral("EKF 创新检验"),
        QStringLiteral("查看速度、位置、高度、磁场、空速和姿态创新检验比，数值接近或超过 1 时需重点排查。"),
        {"XKF4.SV","XKF4.SP","XKF4.SH","XKF4.SM","XKF4.SVT","XKF4.errRP"});
    add(QStringLiteral("vibration"), QStringLiteral("导航与传感器"), QStringLiteral("振动与 IMU 削波"),
        QStringLiteral("比较三轴振动和削波记录，定位结构、电机或桨叶引起的振动。"),
        {"VIBE.VibeX","VIBE.VibeY","VIBE.VibeZ","VIBE.Clip","clipping"});
    add(QStringLiteral("barometer"), QStringLiteral("导航与传感器"), QStringLiteral("高度源一致性"),
        QStringLiteral("比较融合高度、气压高度与 GPS 高度，检查高度源偏差。"),
        {"POS.RelHomeAlt","BARO.Alt","GPS.Alt"});

    QStringList rcInputs, outputs;
    for (int channel = 1; channel <= 8; ++channel) {
        rcInputs.append(QStringLiteral("RCIN.C%1").arg(channel));
        outputs.append(QStringLiteral("RCOU.C%1").arg(channel));
    }
    add(QStringLiteral("rc_input"), QStringLiteral("输入与输出"), QStringLiteral("遥控输入 1-8"),
        QStringLiteral("检查遥控通道输入、开关动作和链路异常。"), rcInputs);
    add(QStringLiteral("servo_output"), QStringLiteral("输入与输出"), QStringLiteral("舵机/电机输出 1-8"),
        QStringLiteral("比较主要输出通道，检查饱和、异常跳变和通道一致性。"), outputs);

    const QVector<AnalysisProject> baseProjects = _analysisProjects;
    QVector<AnalysisProject> recommendations;
    QSet<QString> recommendedIds;
    auto appendRecommendation = [&](const Finding& finding) {
        if (finding.severity < Severity::Notice || recommendations.size() >= 6) return;
        const QString targetId = projectIdForCategory(finding.category);
        if (recommendedIds.contains(targetId)) return;
        const auto found = std::find_if(baseProjects.cbegin(), baseProjects.cend(), [&](const AnalysisProject& project) { return project.id == targetId; });
        if (found == baseProjects.cend()) return;
        AnalysisProject recommendation = *found;
        recommendation.id = QStringLiteral("recommend_%1").arg(recommendations.size());
        recommendation.group = QStringLiteral("智能推荐");
        recommendation.title = QStringLiteral("重点：") + finding.title;
        recommendation.reason = QStringLiteral("诊断依据：%1。%2").arg(finding.evidence, finding.detail);
        recommendation.focusTimeUs = finding.timeUs;
        recommendations.append(recommendation);
        recommendedIds.insert(targetId);
    };
    for (const Finding& finding : _report.findings) if (finding.timeUs > 0) appendRecommendation(finding);
    for (const Finding& finding : _report.findings) if (finding.timeUs <= 0) appendRecommendation(finding);
    if (recommendations.isEmpty() && !baseProjects.isEmpty()) {
        AnalysisProject recommendation = baseProjects.first();
        recommendation.id = QStringLiteral("recommend_0");
        recommendation.group = QStringLiteral("智能推荐");
        recommendation.title = QStringLiteral("重点：飞行全貌");
        recommendation.reason = QStringLiteral("当前规则没有定位到需要优先查看的异常时段，先从整体飞行趋势开始。");
        recommendations.append(recommendation);
    }
    _analysisProjects = recommendations;
    _analysisProjects += baseProjects;

    QMap<QString, QTreeWidgetItem*> groups;
    for (int index = 0; index < _analysisProjects.size(); ++index) {
        const AnalysisProject& project = _analysisProjects.at(index);
        if (!groups.contains(project.group)) {
            auto* group = new QTreeWidgetItem(_analysisTree, {project.group});
            group->setFlags(group->flags() & ~Qt::ItemIsSelectable);
            QFont font = group->font(0); font.setBold(true); group->setFont(0, font);
            groups.insert(project.group, group);
        }
        auto* item = new QTreeWidgetItem(groups.value(project.group), {project.title});
        item->setData(0, Qt::UserRole, index);
        item->setData(0, Qt::UserRole + 1, project.id);
        item->setToolTip(0, project.reason);
    }
    for (auto* group : groups) group->setExpanded(group->text(0) == QStringLiteral("智能推荐"));
    if (!_analysisProjects.isEmpty()) activateAnalysisProject(_analysisProjects.first().id);
}

void MainWindow::analysisProjectChanged(QTreeWidgetItem* item, int)
{
    if (!item || item->childCount() > 0) return;
    const int index = item->data(0, Qt::UserRole).toInt();
    if (index < 0 || index >= _analysisProjects.size()) return;
    const AnalysisProject& project = _analysisProjects.at(index);
    _focusedFindingIndex=-1;
    _activeAnalysisIndex = index;
    _chartPlot->setSeriesKeys(project.keys);
    if (project.focusTimeUs > 0) _chartPlot->setVisibleTimeRange(project.focusTimeUs, 120.0);
    updateAnalysisStats();
}

void MainWindow::filterAnalysisProjects(const QString& text)
{
    const QString query = text.trimmed();
    for (int groupIndex = 0; groupIndex < _analysisTree->topLevelItemCount(); ++groupIndex) {
        QTreeWidgetItem* group = _analysisTree->topLevelItem(groupIndex);
        const bool groupMatches = query.isEmpty() || group->text(0).contains(query, Qt::CaseInsensitive);
        bool anyVisible = false;
        for (int index = 0; index < group->childCount(); ++index) {
            QTreeWidgetItem* item = group->child(index);
            const int projectIndex = item->data(0, Qt::UserRole).toInt();
            const AnalysisProject& project = _analysisProjects.at(projectIndex);
            const bool visible = groupMatches || project.title.contains(query, Qt::CaseInsensitive) || project.reason.contains(query, Qt::CaseInsensitive) || project.keys.join(',').contains(query, Qt::CaseInsensitive);
            item->setHidden(!visible);
            anyVisible = anyVisible || visible;
        }
        group->setHidden(!anyVisible);
        if (!query.isEmpty() && anyVisible) group->setExpanded(true);
    }
}

void MainWindow::updateAnalysisStats()
{
    if (!_analysisStats || !_chartPlot) return;
    const QRectF bounds = _chartPlot->visibleBounds();
    const QStringList keys = _chartPlot->seriesKeys();
    _analysisStats->setRowCount(keys.size());
    for (int row = 0; row < keys.size(); ++row) {
        const Series* series = findSeries(keys.at(row));
        double minimum = std::numeric_limits<double>::infinity();
        double maximum = -std::numeric_limits<double>::infinity();
        long double sum = 0;
        int count = 0;
        if (series) {
            for (const QPointF& point : series->points) {
                if (point.x() < bounds.left() || point.x() > bounds.right() || !std::isfinite(point.y())) continue;
                minimum = std::min(minimum, point.y());
                maximum = std::max(maximum, point.y());
                sum += point.y();
                ++count;
            }
        }
        const QString unit = series && !series->unit.isEmpty() ? QStringLiteral(" %1").arg(series->unit) : QString();
        _analysisStats->setItem(row, 0, new QTableWidgetItem(series ? series->title : keys.at(row)));
        _analysisStats->setItem(row, 1, new QTableWidgetItem(count ? QString::number(minimum, 'g', 7) + unit : QStringLiteral("--")));
        _analysisStats->setItem(row, 2, new QTableWidgetItem(count ? QString::number(maximum, 'g', 7) + unit : QStringLiteral("--")));
        _analysisStats->setItem(row, 3, new QTableWidgetItem(count ? QString::number(static_cast<double>(sum / count), 'g', 7) + unit : QStringLiteral("--")));
        _analysisStats->setItem(row, 4, new QTableWidgetItem(QString::number(count)));
    }
    if (_activeAnalysisIndex >= 0 && _activeAnalysisIndex < _analysisProjects.size()) {
        const AnalysisProject& project = _analysisProjects.at(_activeAnalysisIndex);
        QString text=QStringLiteral("%1 · %2 条曲线").arg(analysisConclusion(project, bounds)).arg(project.keys.size());
        if(_focusedFindingIndex>=0 && _focusedFindingIndex<_report.findings.size()) {
            const Finding& f=_report.findings[_focusedFindingIndex];text+=QStringLiteral("\n当前复核：%1 · %2\n触发依据：%3").arg(f.title,f.source,f.evidence.section('\n',0,0));
        }
        _analysisInsight->setText(text);
    }
}

QString MainWindow::analysisConclusion(const AnalysisProject& project, const QRectF& bounds) const
{
    struct Stats { double minimum = std::numeric_limits<double>::infinity(); double maximum = -std::numeric_limits<double>::infinity(); double rms = 0; int count = 0; };
    auto stats = [&](const QString& key) {
        Stats result;
        const Series* series = findSeries(key);
        if (!series) return result;
        long double squares = 0;
        for (const QPointF& point : series->points) {
            if (point.x() < bounds.left() || point.x() > bounds.right() || !std::isfinite(point.y())) continue;
            result.minimum = std::min(result.minimum, point.y());
            result.maximum = std::max(result.maximum, point.y());
            squares += point.y() * point.y();
            ++result.count;
        }
        if (result.count) result.rms = std::sqrt(static_cast<double>(squares / result.count));
        return result;
    };
    auto trackingRms = [&](const QString& targetKey, const QString& actualKey) {
        const Series* target = findSeries(targetKey);
        const Series* actual = findSeries(actualKey);
        if (!target || !actual) return std::numeric_limits<double>::quiet_NaN();
        int left = 0, right = 0, count = 0;
        long double squares = 0;
        while (left < target->points.size() && right < actual->points.size()) {
            const QPointF& targetPoint = target->points.at(left);
            const QPointF& actualPoint = actual->points.at(right);
            if (targetPoint.x() < bounds.left()) { ++left; continue; }
            if (actualPoint.x() < bounds.left()) { ++right; continue; }
            if (targetPoint.x() > bounds.right() || actualPoint.x() > bounds.right()) break;
            const double deltaTime = targetPoint.x() - actualPoint.x();
            if (std::abs(deltaTime) <= 0.05) {
                const double error = targetPoint.y() - actualPoint.y();
                squares += error * error;
                ++count; ++left; ++right;
            } else if (deltaTime < 0) ++left;
            else ++right;
        }
        return count ? std::sqrt(static_cast<double>(squares / count)) : std::numeric_limits<double>::quiet_NaN();
    };

    QString conclusion;
    if (project.keys.contains(QStringLiteral("QPOS.Dist"))) {
        const Stats distance = stats(QStringLiteral("QPOS.Dist"));
        const Stats speed = stats(QStringLiteral("QPOS.TSpd"));
        if (distance.count) conclusion = QStringLiteral("当前窗口最大目标距离 %1 m、最大目标速度 %2 m/s；本次 Position1 首次介入约 %3 m。")
            .arg(distance.maximum, 0, 'f', 1).arg(speed.count ? speed.maximum : 0, 0, 'f', 1).arg(_report.vtolPosition1Distance, 0, 'f', 1);
    } else if (project.keys.contains(QStringLiteral("ATT.DesRoll"))) {
        const double rollRms = trackingRms(QStringLiteral("ATT.DesRoll"), QStringLiteral("ATT.Roll"));
        const double pitchRms = trackingRms(QStringLiteral("ATT.DesPitch"), QStringLiteral("ATT.Pitch"));
        if (std::isfinite(rollRms)) conclusion = QStringLiteral("当前窗口横滚跟踪 RMS 误差 %1°，俯仰跟踪 RMS 误差 %2°。").arg(rollRms, 0, 'f', 2).arg(numberOrDash(pitchRms,2));
    } else if (project.keys.contains(QStringLiteral("PIDR.Err")) || project.keys.contains(QStringLiteral("PIDP.Err"))) {
        const QString key = project.keys.contains(QStringLiteral("PIDR.Err")) ? QStringLiteral("PIDR.Err") : QStringLiteral("PIDP.Err");
        const Stats error = stats(key);
        if (error.count) conclusion = QStringLiteral("当前窗口控制误差 RMS=%1，峰值范围 %2 至 %3。").arg(error.rms, 0, 'f', 3).arg(error.minimum, 0, 'f', 3).arg(error.maximum, 0, 'f', 3);
    } else if (project.keys.contains(QStringLiteral("XKF4.SV"))) {
        double maximum = 0;
        for (const QString& key : project.keys) { const Stats value = stats(key); if (value.count) maximum = std::max(maximum, value.maximum); }
        conclusion = QStringLiteral("当前窗口最大 EKF 创新检验比 %1；超过 1 表示对应观测与预测不一致。").arg(maximum, 0, 'f', 2);
    } else if (project.keys.contains(QStringLiteral("GPS.NSats"))) {
        const Stats satellites = stats(QStringLiteral("GPS.NSats"));
        const Stats hdop = stats(QStringLiteral("GPS.HDop"));
        if (satellites.count) conclusion = QStringLiteral("当前窗口最低卫星数 %1，最大 HDop %2。").arg(satellites.minimum, 0, 'f', 0).arg(hdop.count ? hdop.maximum : 0, 0, 'f', 2);
    } else if (project.keys.contains(QStringLiteral("VIBE.VibeX"))) {
        double maximum = 0;
        for (const QString& key : {QStringLiteral("VIBE.VibeX"), QStringLiteral("VIBE.VibeY"), QStringLiteral("VIBE.VibeZ")}) { const Stats value = stats(key); if (value.count) maximum = std::max(maximum, value.maximum); }
        conclusion = QStringLiteral("当前窗口三轴最大振动 %1 m/s²，整段飞行最大值 %2 m/s²。").arg(maximum, 0, 'f', 1).arg(_report.maxVibration, 0, 'f', 1);
    } else if (project.id.contains(QStringLiteral("battery")) || project.keys.contains(QStringLiteral("bat0_voltage"))) {
        const QString voltageKey = project.keys.filter(QRegularExpression(QStringLiteral("voltage$"))).value(0);
        const QString currentKey = project.keys.filter(QRegularExpression(QStringLiteral("current$"))).value(0);
        const Stats voltage = stats(voltageKey);
        const Stats current = stats(currentKey);
        if (voltage.count) conclusion = QStringLiteral("当前窗口最低电压 %1 V，最大电流 %2 A。").arg(voltage.minimum, 0, 'f', 2).arg(current.count ? current.maximum : 0, 0, 'f', 1);
    } else if (project.keys.contains(QStringLiteral("POWR.Vcc"))) {
        const Stats vcc = stats(QStringLiteral("POWR.Vcc"));
        const Stats servo = stats(QStringLiteral("POWR.VServo"));
        if (vcc.count) conclusion = QStringLiteral("当前窗口飞控最低供电 %1 V，舵机最低供电 %2 V。").arg(vcc.minimum, 0, 'f', 2).arg(servo.count ? servo.minimum : 0, 0, 'f', 2);
    }
    return conclusion.isEmpty() ? project.reason : project.reason + QStringLiteral(" 自动结论：") + conclusion;
}

void MainWindow::activateAnalysisProject(const QString& id, qint64 focusTimeUs)
{
    for (int groupIndex = 0; groupIndex < _analysisTree->topLevelItemCount(); ++groupIndex) {
        QTreeWidgetItem* group = _analysisTree->topLevelItem(groupIndex);
        for (int index = 0; index < group->childCount(); ++index) {
            QTreeWidgetItem* item = group->child(index);
            if (item->data(0, Qt::UserRole + 1).toString() != id) continue;
            group->setExpanded(true);
            _analysisTree->setCurrentItem(item);
            analysisProjectChanged(item, 0);
            if (focusTimeUs > 0) _chartPlot->setVisibleTimeRange(focusTimeUs, 120.0);
            return;
        }
    }
}

void MainWindow::renderFindings()
{
    const int critical = std::count_if(_report.findings.cbegin(), _report.findings.cend(), [](const Finding& item) { return item.severity == Severity::Critical; });
    const int warnings = std::count_if(_report.findings.cbegin(), _report.findings.cend(), [](const Finding& item) { return item.severity == Severity::Warning; });
    const int notices = std::count_if(_report.findings.cbegin(), _report.findings.cend(), [](const Finding& item) { return item.severity == Severity::Notice; });
    _findingSummary->setText(QStringLiteral("严重 %1 项 · 警告 %2 项 · 关注 %3 项 · 共执行通用与 %4 专项诊断")
                             .arg(critical).arg(warnings).arg(notices).arg(_report.vehicleName));
    _findingTable->setRowCount(0);
    _findingSearch->clear();
    _findingSeverity->setCurrentIndex(0);
    _findingTable->setRowCount(_report.findings.size());
    for (int row = 0; row < _report.findings.size(); ++row) {
        const Finding& finding = _report.findings.at(row);
        auto* severityItem = new QTableWidgetItem(severityName(finding.severity));
        severityItem->setForeground(severityColor(finding.severity));
        QFont font = severityItem->font(); font.setBold(true); severityItem->setFont(font);
        _findingTable->setItem(row, 0, severityItem);
        _findingTable->setItem(row, 1, new QTableWidgetItem(finding.category + "\n" + finding.source));
        _findingTable->setItem(row, 2, new QTableWidgetItem(finding.title));
        _findingTable->setItem(row, 3, new QTableWidgetItem(finding.detail));
        _findingTable->setItem(row, 4, new QTableWidgetItem(finding.evidence));
        _findingTable->setItem(row, 5, new QTableWidgetItem(finding.timeUs ? formatClock(finding.timeUs, _report.flightStartUs,true) + (finding.endTimeUs>finding.timeUs ? "\n→ " + formatClock(finding.endTimeUs,_report.flightStartUs,true) : QString()) : QStringLiteral("配置 / 整段")));
        _findingTable->setRowHeight(row, 68);
        for (int column = 0; column < _findingTable->columnCount(); ++column) _findingTable->item(row, column)->setToolTip(_findingTable->item(row,column)->text()+QStringLiteral("\n\n双击打开相关分析曲线"));
    }
}

void MainWindow::findingActivated(int row, int)
{
    if (row < 0 || row >= _report.findings.size()) return;
    const Finding& finding = _report.findings.at(row);
    _focusedFindingIndex=row;
    _analysisSearch->clear();
    _navigation->setCurrentRow(3);
    activateAnalysisProject(projectIdForCategory(finding.category), finding.timeUs);
    _focusedFindingIndex=row;
    const QString evidenceKey=finding.evidence.section(QStringLiteral("；"),0,0);
    if(findSeries(evidenceKey) && !_chartPlot->seriesKeys().contains(evidenceKey)) {
        QStringList keys=_chartPlot->seriesKeys();if(keys.size()>=8)keys.removeLast();keys.prepend(evidenceKey);_chartPlot->setSeriesKeys(keys);
    }
    if(finding.timeUs>0) _chartPlot->setVisibleTimeRange((finding.timeUs+std::max(finding.timeUs,finding.endTimeUs))/2,std::max(30.0,(finding.endTimeUs-finding.timeUs)/1e6+20.0));
    updateAnalysisStats();
}

void MainWindow::renderEvents()
{
    _eventCategory->blockSignals(true);
    _eventCategory->clear();
    _eventCategory->addItem(QStringLiteral("全部类别"));
    QStringList categories;
    for (const FlightEvent& event : _report.events) if (!categories.contains(event.category)) categories.append(event.category);
    std::sort(categories.begin(), categories.end());
    _eventCategory->addItems(categories);
    _eventCategory->blockSignals(false);
    filterEvents();
}

void MainWindow::filterEvents()
{
    if (!_eventTable) return;
    const QString query = _eventSearch->text().trimmed();
    const QString category = _eventCategory->currentIndex() > 0 ? _eventCategory->currentText() : QString();
    QVector<const FlightEvent*> filtered;
    for (const FlightEvent& event : _report.events) {
        const bool categoryMatch = category.isEmpty() || event.category == category;
        const bool textMatch = query.isEmpty() || event.category.contains(query, Qt::CaseInsensitive) || event.name.contains(query, Qt::CaseInsensitive) || event.detail.contains(query, Qt::CaseInsensitive);
        if (categoryMatch && textMatch) filtered.append(&event);
    }
    _eventTable->setRowCount(filtered.size());
    for (int row = 0; row < filtered.size(); ++row) {
        const FlightEvent& event = *filtered.at(row);
        auto* timeItem = new QTableWidgetItem(formatClock(event.timeUs, _report.flightStartUs));
        timeItem->setData(Qt::UserRole, event.timeUs);
        timeItem->setData(Qt::UserRole + 1, event.category);
        _eventTable->setItem(row, 0, timeItem);
        auto* severityItem = new QTableWidgetItem(severityName(event.severity));
        severityItem->setForeground(severityColor(event.severity));
        _eventTable->setItem(row, 1, severityItem);
        _eventTable->setItem(row, 2, new QTableWidgetItem(event.category));
        _eventTable->setItem(row, 3, new QTableWidgetItem(event.name));
        _eventTable->setItem(row, 4, new QTableWidgetItem(event.detail));
        _eventTable->setRowHeight(row, 44);
        for (int column = 0; column < _eventTable->columnCount(); ++column) _eventTable->item(row, column)->setToolTip(QStringLiteral("双击在分析曲线中定位此时间"));
    }
}

void MainWindow::eventActivated(int row, int)
{
    QTableWidgetItem* item = _eventTable->item(row, 0);
    if (!item) return;
    const qint64 timeUs = item->data(Qt::UserRole).toLongLong();
    const QString category = item->data(Qt::UserRole + 1).toString();
    _navigation->setCurrentRow(3);
    if(timeUs<_report.flightStartUs || timeUs>_report.flightEndUs){if(auto* scope=findChild<QComboBox*>("chartPlotScope"))scope->setCurrentIndex(1);}
    activateAnalysisProject(projectIdForCategory(category), timeUs);
}

void MainWindow::renderParameters()
{
    _parameterCategory->blockSignals(true);
    _parameterCategory->clear();
    _parameterCategory->addItem(QStringLiteral("全部系统"));
    QStringList categories;
    for (auto it = _report.parameters.cbegin(); it != _report.parameters.cend(); ++it) {
        const QString category = parameterCategory(it.key());
        if (!categories.contains(category)) categories.append(category);
    }
    std::sort(categories.begin(), categories.end());
    _parameterCategory->addItems(categories);
    _parameterCategory->blockSignals(false);
    filterParameters();
}

void MainWindow::filterParameters()
{
    if (!_parameterTable) return;
    const QString query = _parameterSearch->text().trimmed();
    const QString selectedCategory = _parameterCategory->currentIndex() > 0 ? _parameterCategory->currentText() : QString();
    QVector<QPair<QString, ParameterValue>> filtered;
    for (auto it = _report.parameters.cbegin(); it != _report.parameters.cend(); ++it) {
        const QString chinese = parameterChineseName(it.key());
        const QString category = parameterCategory(it.key());
        const bool categoryMatch = selectedCategory.isEmpty() || category == selectedCategory;
        const bool textMatch = query.isEmpty() || it.key().contains(query, Qt::CaseInsensitive) || chinese.contains(query, Qt::CaseInsensitive) || category.contains(query, Qt::CaseInsensitive);
        if (categoryMatch && textMatch) filtered.append({it.key(), it.value()});
    }
    _parameterTable->setRowCount(filtered.size());
    for (int row = 0; row < filtered.size(); ++row) {
        const QString& name = filtered.at(row).first;
        const ParameterValue& value = filtered.at(row).second;
        _parameterTable->setItem(row, 0, new QTableWidgetItem(name));
        _parameterTable->setItem(row, 1, new QTableWidgetItem(parameterChineseName(name)));
        _parameterTable->setItem(row, 2, new QTableWidgetItem(parameterCategory(name)));
        _parameterTable->setItem(row, 3, new QTableWidgetItem(QString::number(value.value, 'g', 10)));
        _parameterTable->setItem(row, 4, new QTableWidgetItem(value.hasDefault ? QString::number(value.defaultValue, 'g', 10) : QStringLiteral("--")));
        const bool changed = value.hasDefault && std::abs(value.value - value.defaultValue) > 1e-7;
        _parameterTable->setItem(row, 5, new QTableWidgetItem(value.hasDefault ? (changed ? QStringLiteral("已修改") : QStringLiteral("默认值")) : QStringLiteral("无默认值记录")));
    }
}

void MainWindow::navigationChanged(int row)
{
    _pages->setCurrentIndex(std::clamp(row,0,_pages->count()-1));
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasUrls() || event->mimeData()->urls().isEmpty()) return;
    const QString path = event->mimeData()->urls().first().toLocalFile();
    if (path.endsWith(QStringLiteral(".bin"), Qt::CaseInsensitive)) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    if (!event->mimeData()->hasUrls() || event->mimeData()->urls().isEmpty()) return;
    const QString path = event->mimeData()->urls().first().toLocalFile();
    if (!path.isEmpty()) startAnalysis(path);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (_parserThread && _parserThread->isRunning()) {
        _parserThread->requestInterruption();
        _parserThread->quit();
        _parserThread->wait();
    }
    saveNotes();
    QSettings(QCoreApplication::applicationDirPath()+"/飞析设置.ini",QSettings::IniFormat).setValue("window/geometry",saveGeometry());
    event->accept();
}

QString MainWindow::defaultExportName(const QString& suffix) const
{
    return QFileInfo(_report.filePath).absolutePath() + QLatin1Char('/') + QFileInfo(_report.fileName).completeBaseName() + QStringLiteral("_中文分析报告.") + suffix;
}

QString MainWindow::reportHtml() const
{
    QString html = QStringLiteral("<!doctype html><html><head><meta charset='utf-8'><style>body{font-family:'Microsoft YaHei',sans-serif;color:#20333a;margin:36px}h1{font-size:25px}h2{font-size:17px;margin-top:28px;border-bottom:1px solid #dfe7e3;padding-bottom:8px}table{border-collapse:collapse;width:100%;font-size:12px}th,td{border:1px solid #dfe7e3;padding:8px;text-align:left;vertical-align:top}th{background:#eff4f2}.critical{color:#ad2f38}.warning{color:#d15d3f}.notice{color:#9a6a19}.meta{color:#65757a}</style></head><body>");
    html += QStringLiteral("<h1>QuadPlane / ArduPilot 试飞复盘报告</h1>");
    if(_report.demo)html+=QStringLiteral("<p class='warning'><b>人工合成教学日志 · 仅用于练习与软件验证</b></p>");
    html+=QStringLiteral("<p>试验编号：%1<br>测试工程师：%2<br>试验目标：%3</p><h2>工程师复盘记录</h2><p>%4</p>").arg(htmlEscaped(_testId->text()),htmlEscaped(_engineer->text()),htmlEscaped(_testObjective->text()),htmlEscaped(_testNotes->toPlainText()));
    html+=QStringLiteral("<p class='meta'>筛查门限：振动 %1 m/s² · EKF %2 · 横滚误差 %3° · 俯仰误差 %4° · 跟踪误差持续 %5 s · 电机高输出持续 %6 s。保留日志原始采样；统计不补造缺失数据。</p>").arg(_report.diagnosticOptions.vibration).arg(_report.diagnosticOptions.ekfRatio).arg(_report.diagnosticOptions.rollError).arg(_report.diagnosticOptions.pitchError).arg(_report.diagnosticOptions.trackingDuration).arg(_report.diagnosticOptions.motorDuration);
    html += QStringLiteral("<p class='meta'>文件：%1<br>固件：%2<br>机型：%3 · %4<br>飞行时长：%5 · 距离：%6 km</p>")
        .arg(htmlEscaped(_report.fileName), htmlEscaped(_report.firmware), htmlEscaped(_report.vehicleName), htmlEscaped(_report.layoutName),
             formatDuration(_report.flightEndUs - _report.flightStartUs), QString::number(_report.distanceMeters / 1000.0, 'f', 2));
    PlotWidget* evidencePlot=_evidencePlot?_evidencePlot:_chartPlot;
    if(!evidencePlot->seriesKeys().isEmpty()){
        QByteArray image;QBuffer buffer(&image);buffer.open(QIODevice::WriteOnly);evidencePlot->grab().save(&buffer,"PNG");
        html+=QStringLiteral("<h2>当前证据曲线</h2><p>字段：%1</p><p>相对解锁区间：%2 → %3</p><p><img width='550' src='data:image/png;base64,%4'></p>").arg(htmlEscaped(evidencePlot->seriesKeys().join(", ")),formatClock(qRound64(evidencePlot->visibleBounds().left()*1e6),_report.flightStartUs),formatClock(qRound64(evidencePlot->visibleBounds().right()*1e6),_report.flightStartUs),QString::fromLatin1(image.toBase64()));
    }
    html+=QStringLiteral("<h2 style='page-break-before:always'>检查覆盖</h2><table><tr><th>项目</th><th>状态</th><th>依据 / 条件</th></tr>");
    for(const RuleCoverage& c:_report.coverage)html+=QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td></tr>").arg(htmlEscaped(c.title),c.available?QStringLiteral("有证据可检查"):QStringLiteral("缺少证据，未判断"),htmlEscaped(c.detail));
    html+=QStringLiteral("</table>");
    html += QStringLiteral("<h2 style='page-break-before:always'>风险诊断</h2>");
    for (const Finding& finding : _report.findings) {
        const QString css = finding.severity == Severity::Critical ? QStringLiteral("critical") : finding.severity == Severity::Warning ? QStringLiteral("warning") : QStringLiteral("notice");
        const QString time=finding.timeUs ? formatClock(finding.timeUs,_report.flightStartUs,true)+QStringLiteral(" → ")+formatClock(std::max(finding.timeUs,finding.endTimeUs),_report.flightStartUs,true) : QStringLiteral("配置提示 / 整段记录");
        html += QStringLiteral("<h3 class='%1'>%2 · %3</h3><p class='meta'>%4 / %5<br>时间：%6</p><p>%7</p><p><b>证据与条件：</b>%8</p>")
            .arg(css,severityName(finding.severity),htmlEscaped(finding.title),htmlEscaped(finding.category),htmlEscaped(finding.source),time,htmlEscaped(finding.detail),htmlEscaped(finding.evidence));
    }
    html += QStringLiteral("<h2>关键指标</h2><table>");
    html += QStringLiteral("<tr><th>最大相对高度</th><td>%1 m</td><th>最大地速</th><td>%2 m/s</td></tr>").arg(numberOrDash(_report.maxRelativeAltitude), numberOrDash(_report.maxGroundSpeed));
    html += QStringLiteral("<tr><th>最大空速</th><td>%1 m/s</td><th>最大振动</th><td>%2 m/s²</td></tr>").arg(numberOrDash(_report.maxAirSpeed), numberOrDash(_report.maxVibration));
    html+=QStringLiteral("</table><h2>电池与能源（飞行窗口）</h2><table><tr><th>电池</th><th>最低电压</th><th>最大电流</th><th>计数增量 Ah / Wh</th><th>最低余量</th></tr>");
    for(const BatterySummary& b:_report.batteries)html+=QStringLiteral("<tr><td>%1</td><td>%2 V</td><td>%3 A</td><td>%4 Ah / %5 Wh</td><td>%6 %</td></tr>").arg(b.instance+1).arg(numberOrDash(b.minVoltage,2),numberOrDash(b.maxCurrent),numberOrDash(b.consumedAh,2),numberOrDash(b.consumedWh),numberOrDash(b.minRemainingPercent,0));
    html+=QStringLiteral("</table><h2>事件时间线</h2><table><tr><th>相对解锁时间</th><th>事件</th><th>原始证据</th></tr>");
    for(const FlightEvent& e:_report.events)html+=QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td></tr>").arg(formatClock(e.timeUs,_report.flightStartUs,true),htmlEscaped(e.name),htmlEscaped(e.detail));
    html += QStringLiteral("</table><h2 style='page-break-before:always'>关键参数摘要</h2><table><tr><th>参数</th><th>中文释义</th><th>当前值</th><th>默认值</th></tr>");
    const QRegularExpression relevant(QStringLiteral("^(Q_ENABLE|Q_FRAME_(CLASS|TYPE)|Q_OPTIONS|Q_TRANS_(DECEL|FAIL|FAIL_ACT)|Q_ASSIST_SPEED|Q_M_PWM_(MIN|MAX|TYPE)|AIRSPEED_(MIN|MAX)|ARSPD_(TYPE|USE)|ARMING_CHECK|BATT[0-9]*_(MONITOR|LOW_VOLT|CRT_VOLT|LOW_MAH|CRT_MAH|FS_VOLTSRC|FS_LOW_ACT|FS_CRT_ACT)|SERVO[0-9]+_(FUNCTION|MIN|MAX))$"));
    for (auto it = _report.parameters.cbegin(); it != _report.parameters.cend(); ++it) {
        if(!relevant.match(it.key()).hasMatch())continue;
        html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td></tr>")
            .arg(it.key(), htmlEscaped(parameterChineseName(it.key())), QString::number(it.value().value, 'g', 10), it.value().hasDefault ? QString::number(it.value().defaultValue, 'g', 10) : QStringLiteral("--"));
    }
    html += QStringLiteral("</table><p class='meta'>本报告由飞析在本机生成。自动诊断用于辅助排查，不替代机型手册、适航要求与试飞安全评审。</p></body></html>");
    return html;
}

void MainWindow::exportHtml()
{
    if (!_report.valid) return;
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出 HTML 报告"), defaultExportName(QStringLiteral("html")), QStringLiteral("HTML 报告 (*.html)"));
    if (path.isEmpty()) return;
    saveNotes();
    if(!writeReport(path)){QMessageBox::warning(this,QStringLiteral("导出失败"),QStringLiteral("无法完整写入所选文件，请检查目录权限和磁盘空间。"));return;}
    statusBar()->showMessage(QStringLiteral("报告已导出：")+path,15000);

}

void MainWindow::exportPdf()
{
    if (!_report.valid) return;
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出 PDF 报告"), defaultExportName(QStringLiteral("pdf")), QStringLiteral("PDF 报告 (*.pdf)"));
    if (path.isEmpty()) return;
    saveNotes();
    if(!writePdf(path)){QMessageBox::warning(this,QStringLiteral("导出失败"),QStringLiteral("PDF 未成功写入，请检查目录权限和磁盘空间。"));return;}
    statusBar()->showMessage(QStringLiteral("报告已导出：")+path,15000);

}

void MainWindow::loadDemo()
{
    const QString path=QCoreApplication::applicationDirPath()+"/示例日志/QuadPlane_教学示例.BIN";
    if(!QFile::exists(path)){statusBar()->showMessage(QStringLiteral("找不到教学日志，请保留完整软件目录。"),10000);return;}
    openLog(path);
}
void MainWindow::renderRecentFiles()
{
    _recentList->clear();
    QSettings settings(QCoreApplication::applicationDirPath()+"/飞析设置.ini",QSettings::IniFormat);
    for(const QString& path:settings.value("recentFiles").toStringList()){
        if(!QFile::exists(path))continue;
        auto* item=new QListWidgetItem(QFileInfo(path).fileName()+"    ·    "+QFileInfo(path).absolutePath(),_recentList);
        item->setData(Qt::UserRole,path);item->setToolTip(path);
    }
    if(!_recentList->count()){
        auto* item=new QListWidgetItem(QStringLiteral("暂无记录。打开你的 .BIN，或点击侧栏“打开教学示例”。"),_recentList);
        item->setFlags(Qt::NoItemFlags);
    }
}
QWidget* MainWindow::createGuidePage()
{
    auto* page=new QWidget;auto* root=new QVBoxLayout(page);root->setContentsMargins(0,0,0,0);
    auto* intro=label(QStringLiteral("从问题出发，沿证据分析 · 不知道怎么看时，按左侧顺序学习。"),"overviewBanner");root->addWidget(intro);
    auto* splitter=new QSplitter;splitter->setChildrenCollapsible(false);
    _guideTopics=new QListWidget;_guideTopics->setObjectName("guideTopics");_guideTopics->addItems(guideTitles());_guideTopics->setMinimumWidth(220);_guideTopics->setMaximumWidth(320);splitter->addWidget(_guideTopics);
    auto* content=new QWidget;auto* layout=new QVBoxLayout(content);layout->setContentsMargins(8,0,0,0);
    _guideBrowser=new QTextBrowser;_guideBrowser->setObjectName("guideBrowser");_guideBrowser->setOpenExternalLinks(false);layout->addWidget(_guideBrowser,1);
    auto* actions=new QHBoxLayout;
    auto* curves=new QPushButton(QStringLiteral("打开本课推荐曲线 →"));curves->setObjectName("primaryButton");curves->setObjectName("guideCurveButton");
    auto* anomalies=new QPushButton(QStringLiteral("查看自动诊断证据"));anomalies->setObjectName("secondaryButton");actions->addWidget(curves);actions->addWidget(anomalies);actions->addStretch();layout->addLayout(actions);
    connect(curves,&QPushButton::clicked,this,[this](){
        if(!_report.valid){statusBar()->showMessage(QStringLiteral("先打开日志或教学示例，即可对照实际曲线练习。"),10000);return;}
        QString id=guideProject(_guideTopics->currentRow());
        auto available=std::find_if(_analysisProjects.cbegin(),_analysisProjects.cend(),[&](const AnalysisProject& p){return p.id==id;});
        if(available==_analysisProjects.cend()){statusBar()->showMessage(QStringLiteral("本日志缺少本课所需曲线。请查看下方覆盖表或在数据浏览中检查日志字段。"),10000);return;}
        _analysisSearch->clear();_navigation->setCurrentRow(3);activateAnalysisProject(id);
        for(int i=0;i<_report.findings.size();++i){const Finding& f=_report.findings[i];if(f.timeUs>0 && projectIdForCategory(f.category)==id){findingActivated(i,0);break;}}
    });
    connect(anomalies,&QPushButton::clicked,this,[this](){_navigation->setCurrentRow(4);});
    splitter->addWidget(content);splitter->setStretchFactor(1,1);root->addWidget(splitter,1);
    root->addWidget(label(QStringLiteral("自动检查覆盖 · “缺少证据”表示尚未判断，不表示正常"),"sectionTitle"));
    _coverageTable=table({QStringLiteral("检查项目"),QStringLiteral("状态"),QStringLiteral("所需证据 / 筛查条件")});_coverageTable->setObjectName("coverageTable");
    _coverageTable->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);_coverageTable->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);_coverageTable->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);_coverageTable->setMaximumHeight(155);root->addWidget(_coverageTable);
    connect(_guideTopics,&QListWidget::currentRowChanged,this,[this](int){_guideBrowser->setHtml(guideHtml(_guideTopics->currentRow()));});
    _guideTopics->setCurrentRow(0);
    return page;
}
QString MainWindow::guideHtml(int index) const
{
    QString html=guideContent(index,_options);
    if(_report.valid) html.replace("</body>",QStringLiteral("<hr><p><b>当前日志：</b>%1%2<br>本次有 %3 条诊断记录。先验证原始证据，再写入试飞结论。</p></body>").arg(_report.demo?QStringLiteral("【人工合成教学示例】 "):QString(),htmlEscaped(_report.fileName)).arg(_report.findings.size()));
    if(_report.valid && index>0){
        const QString id=guideProject(index);
        for(const Finding& f:_report.findings)if(f.timeUs>0 && projectIdForCategory(f.category)==id){
            html.replace("</body>",QStringLiteral("<p style='background:#edf3ff;padding:12px'><b>本课可复核证据：</b>%1<br>相对解锁 %2 → %3<br>%4<br>点击下方按钮，会自动定位到这段曲线。</p></body>").arg(htmlEscaped(f.title),formatClock(f.timeUs,_report.flightStartUs,true),formatClock(std::max(f.timeUs,f.endTimeUs),_report.flightStartUs,true),htmlEscaped(f.evidence)));break;
        }
    }
    return html;
}
void MainWindow::updateGuide()
{
    _coverageTable->setRowCount(_report.coverage.size());
    for(int row=0;row<_report.coverage.size();++row){
        const RuleCoverage& c=_report.coverage[row];_coverageTable->setItem(row,0,new QTableWidgetItem(c.title));auto* state=new QTableWidgetItem(c.available?QStringLiteral("有证据可检查"):QStringLiteral("缺少证据"));state->setForeground(c.available?QColor("#22836d"):QColor("#b17b21"));_coverageTable->setItem(row,1,state);auto* detail=new QTableWidgetItem(c.detail);detail->setToolTip(c.detail);_coverageTable->setItem(row,2,detail);_coverageTable->setRowHeight(row,38);
    }
    _guideBrowser->setHtml(guideHtml(_guideTopics->currentRow()));
}
QWidget* MainWindow::createNotesPage()
{
    auto* page=new QWidget;auto* root=new QVBoxLayout(page);root->setContentsMargins(0,0,0,0);
    root->addWidget(label(QStringLiteral("把推测和已验证结论分开记录。记录自动保存在软件目录的“试飞记录”，并写入导出报告。"),"overviewBanner"));
    auto* form=new QFormLayout;_testId=new QLineEdit;_testId->setObjectName("testId");_testId->setPlaceholderText(QStringLiteral("例如：QP-2601 / 架次 03 / 载荷 2 kg"));_engineer=new QLineEdit;_engineer->setObjectName("engineer");_testObjective=new QLineEdit;_testObjective->setObjectName("testObjective");_testObjective->setPlaceholderText(QStringLiteral("例如：验证后转换减速距离和落点精度"));form->addRow(QStringLiteral("试验编号 / 工况"),_testId);form->addRow(QStringLiteral("测试工程师"),_engineer);form->addRow(QStringLiteral("试验目标"),_testObjective);root->addLayout(form);
    _testNotes=new QTextEdit;_testNotes->setObjectName("testNotes");_testNotes->setPlaceholderText(QStringLiteral("建议记录格式：\n\n现象：\n发生时间 / 阶段：\n证据：字段、数值、区间、对应曲线\n已验证结论：\n待验证假设：\n下一步检查 / 下一架次验证："));root->addWidget(_testNotes,1);
    auto* footer=new QHBoxLayout;_notesStatus=label(QStringLiteral("打开日志后即可记录。"),"helperText");footer->addWidget(_notesStatus,1);auto* exportButton=new QPushButton(QStringLiteral("导出含记录的报告"));exportButton->setObjectName("primaryButton");footer->addWidget(exportButton);root->addLayout(footer);connect(exportButton,&QPushButton::clicked,this,&MainWindow::exportHtml);
    _noteTimer=new QTimer(this);_noteTimer->setSingleShot(true);_noteTimer->setInterval(650);connect(_noteTimer,&QTimer::timeout,this,&MainWindow::saveNotes);
    auto changed=[this](){if(!_loadingNotes && _report.valid){_notesStatus->setText(QStringLiteral("正在保存…"));_noteTimer->start();}};
    for(auto* input:{_testId,_engineer,_testObjective}) { connect(input,&QLineEdit::textChanged,this,[changed](const QString&){changed();}); }
    connect(_testNotes,&QTextEdit::textChanged,this,changed);
    return page;
}
QString MainWindow::notesPath() const
{
    if(!_report.valid)return {};
    const QFileInfo file(_report.filePath);
    const QByteArray identity=(file.canonicalFilePath()+"|"+QString::number(file.size())+"|"+QString::number(file.lastModified().toMSecsSinceEpoch())).toUtf8();
    return QCoreApplication::applicationDirPath()+"/试飞记录/"+QString::fromLatin1(QCryptographicHash::hash(identity,QCryptographicHash::Sha256).toHex())+".json";
}
void MainWindow::loadNotes()
{
    _loadingNotes=true;_noteTimer->stop();QJsonObject object;
    QFile file(notesPath());if(file.open(QIODevice::ReadOnly))object=QJsonDocument::fromJson(file.readAll()).object();
    _testId->setText(object.value("testId").toString());_engineer->setText(object.value("engineer").toString());_testObjective->setText(object.value("objective").toString());_testNotes->setPlainText(object.value("notes").toString());
    _notesStatus->setText(object.isEmpty()?QStringLiteral("准备记录 · 自动保存于本机"):QStringLiteral("已恢复本日志的试飞记录"));_loadingNotes=false;
}
void MainWindow::saveNotes()
{
    if(!_report.valid || !_testNotes || _loadingNotes)return;
    _noteTimer->stop();
    if(_testId->text().isEmpty() && _engineer->text().isEmpty() && _testObjective->text().isEmpty() && _testNotes->toPlainText().isEmpty() && !QFile::exists(notesPath()))return;
    QJsonObject object;object.insert("log",_report.filePath);object.insert("testId",_testId->text());object.insert("engineer",_engineer->text());object.insert("objective",_testObjective->text());object.insert("notes",_testNotes->toPlainText());
    if(!QDir().mkpath(QFileInfo(notesPath()).absolutePath())){_notesStatus->setText(QStringLiteral("保存失败：软件目录不可写。请复制到可写目录。"));return;}
    QSaveFile file(notesPath());if(!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(object).toJson())<0 || !file.commit()){_notesStatus->setText(QStringLiteral("保存失败：请检查磁盘空间和目录权限。"));return;}
    _notesStatus->setText(QStringLiteral("已自动保存 · ")+_report.fileName);
}
void MainWindow::configureRules()
{
    if(_busy)return;
    QDialog dialog(this);dialog.setWindowTitle(QStringLiteral("自动诊断筛查门限"));dialog.resize(520,420);auto* root=new QVBoxLayout(&dialog);root->addWidget(label(QStringLiteral("这些值用于筛查，不是机型验收标准。修改后对当前日志重新诊断，并在报告中记录门限。"),"overviewBanner"));auto* form=new QFormLayout;root->addLayout(form);
    auto spin=[&](const QString& title,double value,double minimum,double maximum,const QString& suffix){auto* box=new QDoubleSpinBox;box->setRange(minimum,maximum);box->setDecimals(2);box->setSuffix(suffix);box->setValue(value);form->addRow(title,box);return box;};
    auto* vibration=spin(QStringLiteral("振动警告"),_options.vibration,1,200," m/s²");auto* ekf=spin(QStringLiteral("EKF 创新检验比"),_options.ekfRatio,0.1,10,"");auto* roll=spin(QStringLiteral("横滚目标跟踪误差"),_options.rollError,1,90," °");auto* pitch=spin(QStringLiteral("俯仰目标跟踪误差"),_options.pitchError,1,90," °");auto* persistence=spin(QStringLiteral("跟踪误差最短持续"),_options.trackingDuration,0.1,30," s");auto* motor=spin(QStringLiteral("电机高输出最短持续"),_options.motorDuration,0.1,30," s");
    auto* reset=new QPushButton(QStringLiteral("恢复默认筛查值"));reset->setObjectName("secondaryButton");root->addWidget(reset);connect(reset,&QPushButton::clicked,&dialog,[=](){vibration->setValue(60);ekf->setValue(1);roll->setValue(15);pitch->setValue(10);persistence->setValue(1);motor->setValue(3);});
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("应用并重新诊断"));buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));root->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted)return;
    _options={vibration->value(),ekf->value(),roll->value(),pitch->value(),persistence->value(),motor->value()};
    QSettings settings(QCoreApplication::applicationDirPath()+"/飞析设置.ini",QSettings::IniFormat);settings.setValue("rules/vibration",_options.vibration);settings.setValue("rules/ekf",_options.ekfRatio);settings.setValue("rules/roll",_options.rollError);settings.setValue("rules/pitch",_options.pitchError);settings.setValue("rules/trackingDuration",_options.trackingDuration);settings.setValue("rules/motorDuration",_options.motorDuration);
    if(_report.valid){
        _report.diagnosticOptions=_options;
        _report.findings.erase(std::remove_if(_report.findings.begin(),_report.findings.end(),[](const Finding& f){return f.source!=QStringLiteral("参数配置提示");}),_report.findings.end());
        appendMeasuredDiagnostics(_report);
        std::stable_sort(_report.findings.begin(),_report.findings.end(),[](const Finding& a,const Finding& b){if(a.severity!=b.severity)return a.severity>b.severity;return a.timeUs<b.timeUs;});
        const int page=_navigation->currentRow();renderReport();_navigation->setCurrentRow(page);statusBar()->showMessage(QStringLiteral("已按新门限重新诊断。"),10000);
    }else _guideBrowser->setHtml(guideHtml(_guideTopics->currentRow()));
}
void MainWindow::attachPlotTools(QVBoxLayout* layout,PlotWidget* plot)
{
    auto* tools=new QHBoxLayout;auto* axes=new QComboBox;axes->addItems({QStringLiteral("按量纲分轴"),QStringLiteral("共用纵轴")});axes->setToolTip(QStringLiteral("不同单位分开显示；共用纵轴仅适合相同量纲。"));tools->addWidget(axes);
    auto* scope=new QComboBox;scope->setObjectName(plot->objectName()+"Scope");scope->addItems({QStringLiteral("飞行窗口"),QStringLiteral("完整日志（含地面）")});tools->addWidget(scope);tools->addStretch();auto* help=new QPushButton(QStringLiteral("怎么看？ F1"));help->setObjectName("secondaryButton");tools->addWidget(help);layout->addLayout(tools);
    connect(axes,QOverload<int>::of(&QComboBox::currentIndexChanged),plot,[plot](int index){plot->setIndependentAxes(index==0);});connect(scope,QOverload<int>::of(&QComboBox::currentIndexChanged),plot,[plot](int index){plot->setFullLog(index==1);});connect(help,&QPushButton::clicked,this,[this](){
        int lesson=0;if(_activeAnalysisIndex>=0 && _activeAnalysisIndex<_analysisProjects.size()){
            const QString id=_analysisProjects[_activeAnalysisIndex].id;
            if(id.contains("transition"))lesson=1;else if(id.contains("landing") || id=="q_height")lesson=2;else if(id.contains("pid") || id=="attitude" || id=="rate" || id=="yaw")lesson=3;else if(id=="q_motors" || id=="q_throttle")lesson=4;else if(id=="vibration")lesson=5;else if(id=="ekf" || id=="gps")lesson=6;else if(id.contains("battery") || id=="power")lesson=7;
        }_guideTopics->setCurrentRow(lesson);_navigation->setCurrentRow(7);
    });
    auto* range=new QHBoxLayout;range->addWidget(label(QStringLiteral("相对解锁"),"helperText"));auto* start=new QDoubleSpinBox;auto* end=new QDoubleSpinBox;for(auto* b:{start,end}){b->setDecimals(2);b->setRange(-86400,86400);b->setSuffix(" s");b->setMinimumWidth(94);b->setKeyboardTracking(false);}range->addWidget(start);range->addWidget(label("→","helperText"));range->addWidget(end);
    auto* apply=new QPushButton(QStringLiteral("定位区间"));apply->setObjectName("secondaryButton");range->addWidget(apply);auto* note=new QPushButton(QStringLiteral("记入复盘"));note->setObjectName("secondaryButton");range->addWidget(note);range->addStretch();layout->addLayout(range);
    connect(plot,&PlotWidget::viewChanged,this,[this,start,end,plot](const QRectF& b){if(!b.isValid())return;if((_navigation->currentRow()==1 && plot==_dataPlot) || (_navigation->currentRow()==3 && plot==_chartPlot))_evidencePlot=plot;const double base=_report.flightStartUs/1e6;start->blockSignals(true);end->blockSignals(true);start->setValue(b.left()-base);end->setValue(b.right()-base);start->blockSignals(false);end->blockSignals(false);});
    connect(apply,&QPushButton::clicked,this,[this,plot,start,end](){if(!_report.valid)return;if(end->value()<=start->value()){statusBar()->showMessage(QStringLiteral("结束时间必须晚于开始时间。"),7000);return;}plot->setTimeWindow(start->value()+_report.flightStartUs/1e6,end->value()+_report.flightStartUs/1e6);});
    connect(note,&QPushButton::clicked,this,[this,plot](){if(!_report.valid || plot->seriesKeys().isEmpty())return;_evidencePlot=plot;const QRectF b=plot->visibleBounds();_testNotes->append(QStringLiteral("\n证据区间：%1 → %2\n字段：%3\n观察与结论：").arg(formatClock(qRound64(b.left()*1e6),_report.flightStartUs),formatClock(qRound64(b.right()*1e6),_report.flightStartUs),plot->seriesKeys().join(", ")));_navigation->setCurrentRow(8);_testNotes->setFocus();});
}
bool MainWindow::writeReport(const QString& path) const
{
    if(!_report.valid)return false;
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly))return false;
    const QByteArray bytes=reportHtml().toUtf8();
    return file.write(bytes)==bytes.size() && file.commit();
}

bool MainWindow::writePdf(const QString& path) const
{
    if(!_report.valid)return false;
    QPrinter printer(QPrinter::ScreenResolution);printer.setResolution(96);printer.setOutputFormat(QPrinter::PdfFormat);printer.setOutputFileName(path);printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageMargins(QMarginsF(12,12,12,12),QPageLayout::Millimeter);
    QTextDocument document;document.setDefaultFont(QFont(QStringLiteral("Microsoft YaHei UI"),10));document.setDocumentMargin(0);
    QString html=reportHtml();
    html.replace("<table>","<table width='100%' cellspacing='0' cellpadding='4'>");
    html.replace("margin:36px","margin:0");
    html.replace(QRegularExpression(QStringLiteral("(<table[^>]*>)<tr>(.*?)</tr>")),QStringLiteral("\\1<thead><tr>\\2</tr></thead>"));
    html.replace("padding:8px","padding:5px");
    document.setHtml(html);
    const QSizeF printable=printer.pageRect(QPrinter::DevicePixel).size();
    document.setPageSize(QSizeF(printable.width(),printable.height()-24));
    // Keep each short finding and its evidence on the same PDF page.
    const double pageHeight=document.pageSize().height();
    for(QTextBlock first=document.begin();first.isValid();first=first.next()){
        if(first.blockFormat().headingLevel()!=3)continue;
        QTextBlock last=first;
        for(QTextBlock next=first.next();next.isValid() && next.blockFormat().headingLevel()<2;next=next.next())last=next;
        const double top=document.documentLayout()->blockBoundingRect(first).top();
        const double bottom=document.documentLayout()->blockBoundingRect(last).bottom();
        if(bottom-top<pageHeight && std::floor(top/pageHeight)<std::floor((bottom-0.01)/pageHeight)){
            QTextCursor cursor(first);QTextBlockFormat format=first.blockFormat();format.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);cursor.setBlockFormat(format);
        }
    }
    QPainter painter;
    if(!painter.begin(&printer))return false;
    const int pages=document.pageCount();
    for(int page=0;page<pages;++page){
        if(page && !printer.newPage()){painter.end();return false;}
        painter.save();painter.translate(0,-page*pageHeight);
        document.drawContents(&painter,QRectF(0,page*pageHeight,printable.width(),pageHeight));painter.restore();
        painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"),9));painter.setPen(QColor("#65757a"));
        painter.drawText(QRectF(0,pageHeight+4,printable.width(),20),Qt::AlignRight|Qt::AlignVCenter,QStringLiteral("%1 / %2").arg(page+1).arg(pages));
    }
    painter.end();
    return QFileInfo(path).size()>0;
}
