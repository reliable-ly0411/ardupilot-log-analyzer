#pragma once

#include "model.h"

#include <QMainWindow>

class DropPanel;
class LogParser;
class PlotWidget;
class QComboBox;
class QDoubleSpinBox;
class QTextBrowser;
class QTextEdit;
class QTimer;
class QVBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QTableWidget;
class QThread;
class QTreeWidget;
class QTreeWidgetItem;
class RouteWidget;
class QWidget;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void openLog(const QString& filePath);
    const LogReport& report() const;
    bool writeReport(const QString& filePath) const;
    bool writePdf(const QString& filePath) const;
    void loadDemo();

signals:
    void analysisComplete();

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void chooseLog();
    void analysisProgress(int percent, const QString& phase);
    void analysisFinished(const LogReport& report);
    void navigationChanged(int row);
    void dataTreeChanged(QTreeWidgetItem* item, int column);
    void filterDataTree(const QString& text);
    void analysisProjectChanged(QTreeWidgetItem* item, int column);
    void filterAnalysisProjects(const QString& text);
    void updateAnalysisStats();
    void findingActivated(int row, int column);
    void eventActivated(int row, int column);
    void filterEvents();
    void filterParameters();
    void exportHtml();
    void exportPdf();

private:
    struct AnalysisProject;
    QWidget* createOverviewPage();
    QWidget* createGuidePage();
    QWidget* createNotesPage();
    void updateGuide();
    void configureRules();
    void renderRecentFiles();
    void loadNotes();
    void saveNotes();
    QString notesPath() const;
    QString guideHtml(int index) const;
    void attachPlotTools(QVBoxLayout* layout, PlotWidget* plot);
    QWidget* createDataBrowserPage();
    QWidget* createRoutePage();
    QWidget* createChartsPage();
    QWidget* createFindingsPage();
    QWidget* createEventsPage();
    QWidget* createParametersPage();
    void buildUi();
    void startAnalysis(const QString& filePath);
    void renderReport();
    void renderOverview();
    void renderDataBrowser();
    void renderAnalysisProjects();
    void renderFindings();
    void renderEvents();
    void renderParameters();
    void setBusy(bool busy);
    void exportPlotCsv(PlotWidget* plot, const QString& baseName);
    void savePlotImage(PlotWidget* plot, const QString& baseName);
    QString reportHtml() const;
    QString defaultExportName(const QString& suffix) const;
    const Series* findSeries(const QString& key) const;
    QString projectIdForCategory(const QString& category) const;
    void activateAnalysisProject(const QString& id, qint64 focusTimeUs = 0);
    QString analysisConclusion(const AnalysisProject& project, const QRectF& bounds) const;

    struct AnalysisProject {
        QString id;
        QString group;
        QString title;
        QString reason;
        QStringList keys;
        qint64 focusTimeUs = 0;
    };

    LogReport _report;
    QString _pendingFile;
    DiagnosticOptions _options;
    bool _busy = false;
    bool _loadingNotes = false;
    QWidget* _welcomeHost = nullptr;
    QScrollArea* _overviewScroll = nullptr;
    QListWidget* _recentList = nullptr;
    QTableWidget* _priorityTable = nullptr;
    QListWidget* _guideTopics = nullptr;
    QTextBrowser* _guideBrowser = nullptr;
    QTableWidget* _coverageTable = nullptr;
    QLineEdit* _testId = nullptr;
    QLineEdit* _engineer = nullptr;
    QLineEdit* _testObjective = nullptr;
    QTextEdit* _testNotes = nullptr;
    QLabel* _notesStatus = nullptr;
    QTimer* _noteTimer = nullptr;
    QPushButton* _cancelButton = nullptr;
    QLineEdit* _findingSearch = nullptr;
    QComboBox* _findingSeverity = nullptr;
    QThread* _parserThread = nullptr;
    LogParser* _parser = nullptr;
    bool _updatingDataTree = false;

    QListWidget* _navigation = nullptr;
    QStackedWidget* _pages = nullptr;
    QLabel* _fileTitle = nullptr;
    QLabel* _fileSubtitle = nullptr;
    QLabel* _statusPill = nullptr;
    QProgressBar* _progress = nullptr;
    QPushButton* _openButton = nullptr;
    QPushButton* _exportButton = nullptr;

    DropPanel* _dropPanel = nullptr;
    QWidget* _overviewContent = nullptr;
    QLabel* _overviewBanner = nullptr;
    QWidget* _metricsHost = nullptr;
    QTableWidget* _batteryTable = nullptr;

    QLineEdit* _dataSearch = nullptr;
    QTreeWidget* _dataTree = nullptr;
    QLabel* _selectedSeriesLabel = nullptr;
    PlotWidget* _dataPlot = nullptr;
    PlotWidget* _evidencePlot = nullptr;

    RouteWidget* _routeWidget = nullptr;
    QLineEdit* _analysisSearch = nullptr;
    QTreeWidget* _analysisTree = nullptr;
    QLabel* _analysisInsight = nullptr;
    QTableWidget* _analysisStats = nullptr;
    PlotWidget* _chartPlot = nullptr;
    QVector<AnalysisProject> _analysisProjects;
    int _activeAnalysisIndex = -1;
    int _focusedFindingIndex = -1;

    QLabel* _findingSummary = nullptr;
    QTableWidget* _findingTable = nullptr;

    QLineEdit* _eventSearch = nullptr;
    QComboBox* _eventCategory = nullptr;
    QTableWidget* _eventTable = nullptr;

    QLineEdit* _parameterSearch = nullptr;
    QComboBox* _parameterCategory = nullptr;
    QTableWidget* _parameterTable = nullptr;
};
