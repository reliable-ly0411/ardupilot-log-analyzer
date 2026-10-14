#include "mainwindow.h"
#include "parser.h"
#include "widgets.h"
#include "theme.h"
#include "regression.h"

#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QMouseEvent>
#include <QTableWidget>
#include <QTimer>
#include <QTreeWidget>

#include <cmath>

namespace {

QJsonObject checkResult(const LogReport& report)
{
    QJsonObject object;
    object.insert(QStringLiteral("valid"), report.valid);
    object.insert(QStringLiteral("error"), report.error);
    object.insert(QStringLiteral("firmware"), report.firmware);
    object.insert(QStringLiteral("vehicle"), report.vehicleName);
    object.insert(QStringLiteral("layout"), report.layoutName);
    object.insert(QStringLiteral("parameters"), report.parameters.size());
    object.insert(QStringLiteral("messageTypes"), report.messageSchemas.size());
    object.insert(QStringLiteral("events"), report.events.size());
    object.insert(QStringLiteral("series"), report.series.size());
    object.insert(QStringLiteral("routePoints"), report.route.size());
    object.insert(QStringLiteral("flightDurationUs"), static_cast<double>(report.flightEndUs - report.flightStartUs));
    object.insert(QStringLiteral("distanceMeters"), report.distanceMeters);
    object.insert(QStringLiteral("position1Distance"), report.vtolPosition1Distance);
    QJsonArray untranslatedMessages;
    QJsonArray genericFields;
    int attRollPoints = 0;
    for (const LogMessageSchema& schema : report.messageSchemas) {
        if (schema.chineseName == QStringLiteral("日志数据")) untranslatedMessages.append(schema.name);
        for (const LogFieldSchema& field : schema.fields) {
            if (field.chineseName == QStringLiteral("数据字段")) genericFields.append(schema.name + QLatin1Char('.') + field.name);
        }
    }
    for (const Series& series : report.series) {
        if (series.key == QStringLiteral("ATT.Roll")) attRollPoints = series.points.size();
    }
    object.insert(QStringLiteral("untranslatedMessages"), untranslatedMessages);
    object.insert(QStringLiteral("genericFields"), genericFields);
    object.insert(QStringLiteral("attDisplay"), QStringLiteral("ATT（姿态）"));
    object.insert(QStringLiteral("rollDisplay"), QStringLiteral("Roll（横滚角）"));
    object.insert(QStringLiteral("attRollPoints"), attRollPoints);
    QJsonArray seriesCatalog;
    for (const Series& series : report.series) {
        if (series.points.isEmpty()) continue;
        QJsonObject item;
        item.insert(QStringLiteral("key"), series.key);
        item.insert(QStringLiteral("title"), series.title);
        item.insert(QStringLiteral("points"), series.points.size());
        seriesCatalog.append(item);
    }
    object.insert(QStringLiteral("seriesCatalog"), seriesCatalog);
    QJsonArray findings;
    for (const Finding& finding : report.findings) {
        QJsonObject item;
        item.insert(QStringLiteral("severity"), static_cast<int>(finding.severity));
        item.insert(QStringLiteral("category"), finding.category);
        item.insert("timeUs",double(finding.timeUs));
        item.insert("endTimeUs",double(finding.endTimeUs));
        item.insert("source",finding.source);
        item.insert(QStringLiteral("title"), finding.title);
        item.insert(QStringLiteral("evidence"), finding.evidence);
        findings.append(item);
    }
    object.insert(QStringLiteral("findings"), findings);
    object.insert("errors",report.errorCount);object.insert("failsafes",report.failsafeCount);object.insert("clippingDelta",double(report.maxClipCount));object.insert("maxVibration",report.maxVibration);object.insert("decodedRecords",double(report.decodedRecords));object.insert("skippedBytes",double(report.skippedBytes));
    QJsonArray coverage;for(const RuleCoverage& c:report.coverage){QJsonObject item;item.insert("title",c.title);item.insert("available",c.available);item.insert("detail",c.detail);coverage.append(item);}object.insert("coverage",coverage);
    return object;
}

}

int main(int argc, char* argv[])
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("飞析"));
    application.setApplicationDisplayName(QStringLiteral("飞析 · ArduPilot 日志分析"));
    application.setOrganizationName(QStringLiteral("UOM"));
    application.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10));
    application.setStyle(QStringLiteral("Fusion"));
    application.setStyleSheet(flightTheme());
    application.setApplicationVersion(QStringLiteral("3.0"));

    const QStringList arguments = application.arguments();
    if(arguments.size()==4 && arguments.at(1)=="--self-test")return runRegression(arguments.at(2),arguments.at(3));
    if (arguments.size() == 4 && arguments.at(1) == QStringLiteral("--check")) {
        LogParser parser;
        LogReport result;
        QObject::connect(&parser, &LogParser::finished, [&](const LogReport& report) { result = report; });
        parser.parse(arguments.at(2));
        QFile output(arguments.at(3));
        if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 2;
        output.write(QJsonDocument(checkResult(result)).toJson(QJsonDocument::Indented));
        output.close();
        return result.valid ? 0 : 1;
    }

    MainWindow window;
    window.show();
    if (arguments.size() >= 2 && !arguments.at(1).startsWith(QStringLiteral("--"))) {
        QTimer::singleShot(0, [&window, arguments]() { window.openLog(arguments.at(1)); });
    }
    if (arguments.size() == 3 && arguments.at(1) == QStringLiteral("--screenshot")) {
        const QString output = arguments.at(2);
        QTimer::singleShot(600, [&window, &application, output]() { window.grab().save(output); application.quit(); });
    }
    if (arguments.size() == 4 && arguments.at(1) == QStringLiteral("--screenshot-log")) {
        const QString logPath = arguments.at(2);
        const QString output = arguments.at(3);
        QObject::connect(&window, &MainWindow::analysisComplete, [&window, &application, output]() {
            QTimer::singleShot(400, [&window, &application, output]() { window.grab().save(output); application.quit(); });
        });
        QTimer::singleShot(0, [&window, logPath]() { window.openLog(logPath); });
    }
    if (arguments.size() == 4 && arguments.at(1) == QStringLiteral("--screenshot-log-data")) {
        const QString logPath = arguments.at(2);
        const QString output = arguments.at(3);
        QObject::connect(&window, &MainWindow::analysisComplete, [&window, &application, output]() {
            if (auto* navigation = window.findChild<QListWidget*>(QStringLiteral("navigation"))) navigation->setCurrentRow(1);
            if (auto* tree = window.findChild<QTreeWidget*>(QStringLiteral("dataTree"))) {
                const QList<QTreeWidgetItem*> matches = tree->findItems(QStringLiteral("ATT（姿态）"), Qt::MatchExactly);
                if (!matches.isEmpty()) {
                    QTreeWidgetItem* att = matches.first();
                    att->setExpanded(true);
                    for (int index = 0; index < att->childCount(); ++index) {
                        QTreeWidgetItem* field = att->child(index);
                        if (field->text(0).startsWith(QStringLiteral("Roll（")) || field->text(0).startsWith(QStringLiteral("Pitch（"))) {
                            field->setCheckState(0, Qt::Checked);
                        }
                    }
                    tree->scrollToItem(att);
                }
            }
            QTimer::singleShot(600, [&window, &application, output]() { window.grab().save(output); application.quit(); });
        });
        QTimer::singleShot(0, [&window, logPath]() { window.openLog(logPath); });
    }
    if (arguments.size() == 4 && arguments.at(1) == QStringLiteral("--screenshot-log-interaction")) {
        const QString logPath = arguments.at(2);
        const QString output = arguments.at(3);
        QObject::connect(&window, &MainWindow::analysisComplete, [&window, &application, output]() {
            if (auto* navigation = window.findChild<QListWidget*>(QStringLiteral("navigation"))) navigation->setCurrentRow(1);
            if (auto* tree = window.findChild<QTreeWidget*>(QStringLiteral("dataTree"))) {
                const QList<QTreeWidgetItem*> matches = tree->findItems(QStringLiteral("ATT（姿态）"), Qt::MatchExactly);
                if (!matches.isEmpty()) {
                    QTreeWidgetItem* att = matches.first();
                    att->setExpanded(true);
                    for (int index = 0; index < att->childCount(); ++index) {
                        QTreeWidgetItem* field = att->child(index);
                        if (field->text(0).startsWith(QStringLiteral("Roll（")) || field->text(0).startsWith(QStringLiteral("Pitch（"))) field->setCheckState(0, Qt::Checked);
                    }
                    tree->scrollToItem(att);
                }
            }
            QTimer::singleShot(250, [&window, &application, output]() {
                if (auto* plot = window.findChild<PlotWidget*>(QStringLiteral("dataPlot"))) {
                    const QRectF before = plot->visibleBounds();
                    const QPoint start(qRound(plot->width() * 0.30), qRound(plot->height() * 0.25));
                    const QPoint end(qRound(plot->width() * 0.72), qRound(plot->height() * 0.72));
                    QMouseEvent press(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    QMouseEvent move(QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                    QMouseEvent release(QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                    QApplication::sendEvent(plot, &press);
                    QApplication::sendEvent(plot, &move);
                    QApplication::sendEvent(plot, &release);
                    const QRectF after = plot->visibleBounds();
                    const bool selectionZoomed = after.width() < before.width() && after.height() < before.height();
                    const double selectedWidth = after.width();
                    const double beforePanLeft = after.left();
                    const QPoint panStart(qRound(plot->width() * 0.55), qRound(plot->height() * 0.50));
                    const QPoint panEnd(qRound(plot->width() * 0.65), qRound(plot->height() * 0.50));
                    QMouseEvent panPress(QEvent::MouseButtonPress, panStart, Qt::RightButton, Qt::RightButton, Qt::NoModifier);
                    QMouseEvent panMove(QEvent::MouseMove, panEnd, Qt::NoButton, Qt::RightButton, Qt::NoModifier);
                    QMouseEvent panRelease(QEvent::MouseButtonRelease, panEnd, Qt::RightButton, Qt::NoButton, Qt::NoModifier);
                    QApplication::sendEvent(plot, &panPress);
                    QApplication::sendEvent(plot, &panMove);
                    QApplication::sendEvent(plot, &panRelease);
                    const bool panMoved = std::abs(plot->visibleBounds().left() - beforePanLeft) > 0.1;
                    plot->zoomOut();
                    const bool zoomOutExpanded = plot->visibleBounds().width() > selectedWidth;
                    plot->resetView();
                    const bool resetRestored = std::abs(plot->visibleBounds().width() - before.width()) < before.width() * 0.0001;
                    const QPoint wheelPoint(qRound(plot->width() * 0.55), qRound(plot->height() * 0.50));
                    QWheelEvent wheel(wheelPoint, plot->mapToGlobal(wheelPoint), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                    QApplication::sendEvent(plot, &wheel);
                    const bool wheelZoomed = plot->visibleBounds().width() < before.width();
                    plot->resetView();
                    QJsonObject result;
                    result.insert(QStringLiteral("selectionZoomed"), selectionZoomed);
                    result.insert(QStringLiteral("panMoved"), panMoved);
                    result.insert(QStringLiteral("wheelZoomed"), wheelZoomed);
                    result.insert(QStringLiteral("zoomOutExpanded"), zoomOutExpanded);
                    result.insert(QStringLiteral("resetRestored"), resetRestored);
                    result.insert(QStringLiteral("fullTimeSpanSeconds"), before.width());
                    result.insert(QStringLiteral("selectedTimeSpanSeconds"), selectedWidth);
                    QFile resultFile(output + QStringLiteral(".json"));
                    if (resultFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) resultFile.write(QJsonDocument(result).toJson(QJsonDocument::Indented));
                    QMouseEvent pressAgain(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    QMouseEvent moveAgain(QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                    QMouseEvent releaseAgain(QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                    QApplication::sendEvent(plot, &pressAgain);
                    QApplication::sendEvent(plot, &moveAgain);
                    QApplication::sendEvent(plot, &releaseAgain);
                    plot->exportVisibleCsv(output + QStringLiteral(".csv"));
                }
                window.grab().save(output);
                application.quit();
            });
        });
        QTimer::singleShot(0, [&window, logPath]() { window.openLog(logPath); });
    }
    if (arguments.size() == 4 && arguments.at(1) == QStringLiteral("--screenshot-log-analysis")) {
        const QString logPath = arguments.at(2);
        const QString output = arguments.at(3);
        QObject::connect(&window, &MainWindow::analysisComplete, [&window, &application, output]() {
            if (auto* navigation = window.findChild<QListWidget*>(QStringLiteral("navigation"))) navigation->setCurrentRow(3);
            QTimer::singleShot(500, [&window, &application, output]() {
                QJsonObject result;
                if (auto* tree = window.findChild<QTreeWidget*>(QStringLiteral("analysisTree"))) {
                    int projects = 0;
                    QJsonArray groups;
                    for (int index = 0; index < tree->topLevelItemCount(); ++index) {
                        QTreeWidgetItem* group = tree->topLevelItem(index);
                        projects += group->childCount();
                        groups.append(QStringLiteral("%1:%2").arg(group->text(0)).arg(group->childCount()));
                    }
                    result.insert(QStringLiteral("groups"), groups);
                    result.insert(QStringLiteral("projects"), projects);
                    result.insert(QStringLiteral("selected"), tree->currentItem() ? tree->currentItem()->text(0) : QString());
                }
                if (auto* plot = window.findChild<PlotWidget*>(QStringLiteral("chartPlot"))) {
                    result.insert(QStringLiteral("selectedCurves"), plot->seriesKeys().size());
                    result.insert(QStringLiteral("visibleTimeSpanSeconds"), plot->visibleBounds().width());
                }
                if (auto* stats = window.findChild<QTableWidget*>(QStringLiteral("analysisStats"))) result.insert(QStringLiteral("statsRows"), stats->rowCount());
                QFile resultFile(output + QStringLiteral(".json"));
                if (resultFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) resultFile.write(QJsonDocument(result).toJson(QJsonDocument::Indented));
                window.grab().save(output);
                application.quit();
            });
        });
        QTimer::singleShot(0, [&window, logPath]() { window.openLog(logPath); });
    }
    return application.exec();
}
