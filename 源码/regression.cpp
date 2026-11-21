#include "regression.h"
#include "mainwindow.h"
#include "parser.h"
#include "widgets.h"
#include "diagnostics.h"
#include "translations.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <cmath>
#include <algorithm>
#include <limits>

namespace {
LogReport parse(const QString& file,bool cancel=false)
{
    LogParser parser;LogReport result;
    QObject::connect(&parser,&LogParser::finished,[&](const LogReport& r){result=r;});
    if(cancel)QObject::connect(&parser,&LogParser::progress,[&](int n,const QString&){if(n>=1)parser.cancel();});
    parser.parse(file);return result;
}
const Series* series(const LogReport& r,const QString& key)
{
    for(const Series& s:r.series){if(s.key==key)return &s;}
    return nullptr;
}
bool has(const LogReport& r,const QString& title,qint64 start=0)
{
    for(const Finding& f:r.findings){if(f.title.contains(title) && (!start || std::abs(f.timeUs-start)<1000))return true;}
    return false;
}
void pump(int milliseconds=120)
{
    QEventLoop loop;QTimer::singleShot(milliseconds,&loop,&QEventLoop::quit);loop.exec();
}
bool load(MainWindow& window,const QString& file)
{
    QEventLoop loop;bool ready=false;
    auto connection=QObject::connect(&window,&MainWindow::analysisComplete,[&](){ready=true;loop.quit();});
    QTimer::singleShot(20000,&loop,&QEventLoop::quit);window.openLog(file);loop.exec();QObject::disconnect(connection);pump();return ready;
}
}
int runRegression(const QString& directory,const QString& output)
{
    QDir fixtures(directory);const QString normalPath=fixtures.filePath("normal.BIN");
    const QString faultyPath=fixtures.filePath("../示例日志/QuadPlane_教学示例.BIN");
    QDir artifacts(QFileInfo(output).absolutePath());artifacts.mkpath(".");
    QJsonArray checks;bool passed=true;
    auto check=[&](const QString& name,bool ok){QJsonObject item;item.insert("check",name);item.insert("passed",ok);checks.append(item);passed=passed&&ok;};
    check("zero and pre-arm timestamps preserve sign and milliseconds",formatClock(10000000,10000000)=="T+0:00" && formatClock(9500000,10000000,true)=="T-0:00.500");
    const LogReport normal=parse(normalPath);const LogReport faulty=parse(faultyPath);const LogReport missing=parse(fixtures.filePath("missing.BIN"));
    check("normal and faulty DataFlash parse",normal.valid && faulty.valid && normal.vehicleKind==VehicleKind::QuadPlane);
    check("normal flight has no warning or critical findings",std::none_of(normal.findings.cbegin(),normal.findings.cend(),[](const Finding& f){return f.severity>=Severity::Warning;}));
    check("50 Hz ATT retains all 12000 samples",series(faulty,"ATT.Roll") && series(faulty,"ATT.Roll")->points.size()==12000);
    check("short vibration spike retained and located",std::abs(faulty.maxVibration-85)<0.01 && has(faulty,QStringLiteral("振动超过"),121100000));
    check("clipping counts only new in-flight increment",faulty.maxClipCount==4 && normal.maxClipCount==0 && has(faulty,QStringLiteral("新增加速度计削波"),121100000));
    check("ground attitude error excluded from measured findings",has(faulty,QStringLiteral("横滚目标"),85000000) && std::none_of(faulty.findings.cbegin(),faulty.findings.cend(),[](const Finding& f){return f.category==QStringLiteral("姿态控制") && f.timeUs<10000000;}));
    check("GPS instances isolated",series(faulty,"GPS.NSats") && series(faulty,"GPS[1].NSats") && series(faulty,"GPS.NSats")->points.size()==2400 && series(faulty,"GPS[1].NSats")->points.size()==2400);
    check("EKF cores isolated",series(faulty,"XKF4.SV") && series(faulty,"XKF4[1].SV") && series(faulty,"XKF4.SV")->points.size()==2400);
    const QString batteryUnitsPath=fixtures.filePath("battery_units.BIN");
    const LogReport batteryUnits=parse(batteryUnitsPath);
    const Series* mainCharge=series(batteryUnits,"BAT.CurrTot");
    const Series* mainDerivedCharge=series(batteryUnits,"bat0_consumed_ah");
    const Series* auxCharge=series(batteryUnits,"BAT[1].CurrTot");
    const Series* mainEnergy=series(batteryUnits,"BAT.EnrgTot");
    auto sampleAt=[](const Series* s,double time) {
        if(s) for(const QPointF& point:s->points) if(std::abs(point.x()-time)<1e-6) return point.y();
        return std::numeric_limits<double>::quiet_NaN();
    };
    check("BAT.CurrTot raw and derived values share Ah after decoding mAh",
          mainCharge && mainDerivedCharge && mainCharge->unit=="Ah" && mainDerivedCharge->unit=="Ah" &&
          std::abs(sampleAt(mainCharge,10)-0.1)<1e-7 && std::abs(sampleAt(mainCharge,230)-2.3)<1e-7 &&
          std::abs(sampleAt(mainDerivedCharge,10)-0.1)<1e-7 && std::abs(sampleAt(mainDerivedCharge,230)-2.3)<1e-7);
    check("flight consumption subtracts initial cumulative counter",!batteryUnits.batteries.isEmpty() && std::abs(batteryUnits.batteries.first().consumedAh-2.2)<0.001);
    const auto auxBattery=std::find_if(batteryUnits.batteries.cbegin(),batteryUnits.batteries.cend(),[](const BatterySummary& b){return b.instance==1;});
    check("second battery reset does not contaminate charge totals",
          auxCharge && auxCharge->unit=="Ah" && auxBattery!=batteryUnits.batteries.cend() &&
          std::abs(sampleAt(auxCharge,10)-0.44)<1e-7 && std::abs(sampleAt(auxCharge,120))<1e-7 &&
          std::abs(auxBattery->consumedAh-0.8796)<1e-6);
    check("BAT.EnrgTot remains Wh and is not scaled as charge",
          mainEnergy && mainEnergy->unit=="Wh" && !batteryUnits.batteries.isEmpty() &&
          std::abs(sampleAt(mainEnergy,230)-57.5)<1e-7 && std::abs(batteryUnits.batteries.first().consumedWh-55)<1e-6);
    check("error clear does not count as new error",faulty.errorCount==1);
    check("failsafe clear does not count as trigger",faulty.failsafeCount==1 && !has(faulty,QStringLiteral("状态消息"),115000000));
    check("battery low voltage uses logged threshold and interval",has(faulty,QStringLiteral("严重低电门限"),180000000));
    check("simultaneous motor saturation consolidated",has(faulty,QStringLiteral("4 路旋翼"),65000000));
    check("GPS loss, EKF and transition windows located",has(faulty,QStringLiteral("3D Fix"),90000000) && has(faulty,QStringLiteral("检验比持续"),108000000) && has(faulty,QStringLiteral("配置超时"),155000000));
    check("power sag located",has(faulty,QStringLiteral("供电出现压降"),190000000));
    check("missing evidence is visible, not silently healthy",missing.valid && std::any_of(missing.coverage.cbegin(),missing.coverage.cend(),[](const RuleCoverage& c){return !c.available;}) && has(missing,QStringLiteral("缺少数据")));
    check("invalid and FMT-only files rejected",!parse(fixtures.filePath("invalid.BIN")).valid && !parse(fixtures.filePath("fmt_only.BIN")).valid);
    check("cancellation returns cancelled result",parse(faultyPath,true).cancelled);
    LogReport relaxed=faulty;relaxed.findings.clear();relaxed.diagnosticOptions.rollError=40;appendMeasuredDiagnostics(relaxed);
    check("configured threshold changes diagnosis",!has(relaxed,QStringLiteral("横滚目标")) && has(relaxed,QStringLiteral("振动超过")));
    {
        LogReport repeated;repeated.flightStartUs=10000000;repeated.flightEndUs=40000000;
        Series vibration;vibration.key="VIBE.VibeX";vibration.messageType="VIBE";vibration.fieldName="VibeX";
        for(int k=0;k<300;++k){const bool high=k>=20 && k<142 && (k-20)%30<3;vibration.points.append({10+k*0.1,high?85.0:10.0});}
        repeated.series.append(vibration);appendMeasuredDiagnostics(repeated);
        check("all five separate vibration episodes remain visible",std::count_if(repeated.findings.cbegin(),repeated.findings.cend(),[](const Finding& f){return f.title.contains(QStringLiteral("振动超过"));})==5);
        repeated.series.clear();repeated.findings.clear();
        for(int i=0;i<12;++i){FlightEvent event;event.timeUs=(11+i)*1000000LL;event.severity=Severity::Warning;event.category=QStringLiteral("飞控消息");event.name=QStringLiteral("保护事件 %1").arg(i);repeated.events.append(event);}
        appendMeasuredDiagnostics(repeated);
        check("all twelve warning events remain visible",std::count_if(repeated.findings.cbegin(),repeated.findings.cend(),[](const Finding& f){return f.source==QStringLiteral("飞控事件证据");})==12);
        LogReport transition;transition.vehicleKind=VehicleKind::QuadPlane;transition.firmware="ArduPlane V4.5.7";transition.flightStartUs=10000000;transition.flightEndUs=40000000;
        transition.parameters.insert("Q_TRANS_FAIL",{10,0,false});transition.parameters.insert("Q_TAILSIT_ENABLE",{0,0,false});
        Series state,assist,mode;state.key="QTUN.Trn";assist.key="QTUN.Ast";mode.key="MODE.Mode";
        for(int k=0;k<151;++k){state.points.append({10+k*0.1,0});assist.points.append({10+k*0.1,1});}mode.points.append({10,10});
        transition.series={state,assist,mode};
        auto diagnose=[&](){transition.findings.clear();appendMeasuredDiagnostics(transition);return has(transition,QStringLiteral("配置超时"));};
        check("active forward airspeed wait triggers timeout",diagnose());
        for(QPointF& point:transition.series[0].points)point.setY(2);
        check("Trn 2 means done and never triggers timeout",!diagnose());
        for(QPointF& point:transition.series[0].points)point.setY(1);
        check("Trn 1 timer phase does not use airspeed wait timeout",!diagnose());
        for(QPointF& point:transition.series[0].points)point.setY(0);
        for(QPointF& point:transition.series[1].points)point.setY(0);
        check("VTOL hover wait with Ast 0 is not a timeout",!diagnose());
        for(QPointF& point:transition.series[1].points)point.setY(1);
        transition.series[2].points[0].setY(20);
        check("QLAND mode is excluded from forward wait timeout",!diagnose());
        transition.series[2].points[0].setY(10);transition.firmware="ArduPlane V9.0.0";
        check("unknown firmware marks transition unavailable",!diagnose() && std::any_of(transition.coverage.cbegin(),transition.coverage.cend(),[](const RuleCoverage& c){return c.title.contains(QStringLiteral("前转换")) && !c.available;}));
        transition.firmware="ArduPlane V4.6.0";transition.parameters["Q_TAILSIT_ENABLE"].value=1;
        check("tailsitter state definitions are excluded",!diagnose());
    }
    {
        MainWindow window;window.resize(1380,860);window.show();pump();window.grab().save(artifacts.filePath("welcome.png"));
        check("background loading completes",load(window,faultyPath));
        auto* nav=window.findChild<QListWidget*>("navigation");auto* plot=window.findChild<PlotWidget*>("chartPlot");auto* topics=window.findChild<QListWidget*>("guideTopics");
        check("nine guidance lessons available",topics && topics->count()==9);
        check("coverage table populated",window.findChild<QTableWidget*>("coverageTable")->rowCount()>=10);
        for(int page=0;page<9;++page){nav->setCurrentRow(page);pump(80);window.grab().save(artifacts.filePath(QStringLiteral("page_%1.png").arg(page)));}
        topics->setCurrentRow(3);nav->setCurrentRow(7);pump();window.grab().save(artifacts.filePath("guidance_attitude.png"));
        check("lesson contains current logged evidence",window.findChild<QTextBrowser*>("guideBrowser")->toPlainText().contains(QStringLiteral("本课可复核证据")));
        auto* guideButton=window.findChild<QPushButton*>("guideCurveButton");guideButton->click();pump();
        check("guided action opens relevant attitude curves",nav->currentRow()==3 && plot->seriesKeys().contains("ATT.DesRoll") && plot->visibleBounds().left()<=85 && plot->visibleBounds().right()>=88 && plot->visibleBounds().width()<=35);
        window.grab().save(artifacts.filePath("guided_attitude.png"));
        int vibrationRow=-1;for(int i=0;i<window.report().findings.size();++i)if(window.report().findings[i].title.contains(QStringLiteral("振动超过")))vibrationRow=i;
        if(vibrationRow>=0){QMetaObject::invokeMethod(&window,"findingActivated",Q_ARG(int,vibrationRow),Q_ARG(int,0));}
        pump();
        check("finding jumps to spike with appropriate curves",vibrationRow>=0 && plot->seriesKeys().contains("VIBE.VibeX") && plot->visibleBounds().left()<=121.1 && plot->visibleBounds().right()>=121.1);
        window.grab().save(artifacts.filePath("vibration_evidence.png"));
        const QPointF nearest=plot->nearestSample("VIBE.VibeX",121.09);
        check("cursor returns nearest sample rather than always future",std::abs(nearest.x()-121.1)<0.001 && std::abs(nearest.y()-85)<0.001);
        check("cursor shows missing values across a long gap",!std::isfinite(plot->nearestSample("VIBE.VibeX",500).y()));
        plot->setTimeWindow(120.8,121.4);check("precise interval entry",std::abs(plot->visibleBounds().width()-0.6)<0.0001);
        check("CSV exports current raw interval",plot->exportVisibleCsv(artifacts.filePath("evidence.csv")));
        QFile csv(artifacts.filePath("evidence.csv"));csv.open(QIODevice::ReadOnly);const QByteArray bytes=csv.readAll();check("CSV includes known spike and UTF-8 BOM",bytes.startsWith("\xEF\xBB\xBF") && bytes.contains(",85\r\n"));
        plot->resetView();const double flightWidth=plot->visibleBounds().width();plot->setFullLog(true);check("full log includes ground data",plot->visibleBounds().width()>flightWidth);plot->setFullLog(false);
        plot->setSeriesKeys({"ATT.DesRoll","ATT.Roll"});plot->setTimeWindow(80,95);plot->setIndependentAxes(false);const QRectF before=plot->visibleBounds();plot->zoomIn();check("chart zoom changes time and value ranges",plot->visibleBounds().width()<before.width() && plot->visibleBounds().height()<before.height());plot->resetView();plot->setIndependentAxes(true);
        nav->setCurrentRow(8);window.findChild<QLineEdit*>("testId")->setText(QStringLiteral("回归测试，非真实试飞"));window.findChild<QTextEdit*>("testNotes")->setPlainText(QStringLiteral("证据：横滚误差持续 3 秒。待验证假设：控制余量。<escaped>"));pump(800);
        check("report writes successfully",window.writeReport(artifacts.filePath("teaching_report.html")));
        check("PDF report writes successfully",window.writePdf(artifacts.filePath("teaching_report.pdf")));
        QFile html(artifacts.filePath("teaching_report.html"));html.open(QIODevice::ReadOnly);const QString report=QString::fromUtf8(html.readAll());check("report includes notes, evidence, coverage, thresholds and embedded chart",report.contains(QStringLiteral("回归测试")) && report.contains("&lt;escaped&gt;") && report.contains(QStringLiteral("检查覆盖")) && report.contains(QStringLiteral("筛查门限")) && report.contains("data:image/png;base64,"));
        const QString priorPath=window.report().filePath;window.openLog(fixtures.filePath("invalid.BIN"));pump(300);
        check("failed import preserves previous report",window.report().valid && window.report().filePath==priorPath);
        window.findChild<QLineEdit*>("findingSearch")->setText(QStringLiteral("振动"));
        window.findChild<QComboBox*>("findingSeverity")->setCurrentIndex(1);
        check("second valid log loads without stale control state",load(window,normalPath) && window.findChild<QTableWidget*>("findingTable")->rowCount()==window.report().findings.size());
        const QString cancelledPrior=window.report().filePath;
        window.openLog(faultyPath);
        for(QPushButton* button:window.findChildren<QPushButton*>())if(button->text()==QStringLiteral("取消分析"))button->click();
        pump(300);
        check("UI cancellation preserves the previous result",window.report().filePath==cancelledPrior && window.findChild<QLabel*>("statusPill")->text()==QStringLiteral("已取消"));
        check("returning to prior log restores notes",load(window,faultyPath) && window.findChild<QTextEdit*>("testNotes")->toPlainText().contains("<escaped>"));
        window.resize(1024,680);nav->setCurrentRow(3);pump();window.grab().save(artifacts.filePath("small_screen.png"));
        check("usable at 1024 x 680",window.width()==1024 && plot->width()>=350 && plot->height()>=280);
        window.close();pump();
    }
    {
        MainWindow window;
        const bool batteryLoaded=load(window,batteryUnitsPath);
        auto* plot=window.findChild<PlotWidget*>("chartPlot");
        if(plot) plot->setSeriesKeys({"BAT.CurrTot","BAT.EnrgTot"});
        if(plot) plot->setTimeWindow(9.99,10.01);
        const QString batteryCsvPath=artifacts.filePath("battery_units.csv");
        const bool batteryCsvWritten=batteryLoaded && plot && plot->exportVisibleCsv(batteryCsvPath);
        QFile batteryCsv(batteryCsvPath);batteryCsv.open(QIODevice::ReadOnly);
        const QByteArray batteryCsvBytes=batteryCsv.readAll();
        check("battery CSV keeps converted charge separate from unscaled energy",
              batteryCsvWritten && batteryCsvBytes.contains(",\"BAT.CurrTot\",\"Ah\",0.1\r\n") &&
              batteryCsvBytes.contains(",\"BAT.EnrgTot\",\"Wh\",2.5\r\n"));
        const QString reportPath=artifacts.filePath("battery_units.html");
        const bool reportWritten=batteryLoaded && window.writeReport(reportPath);
        QFile reportFile(reportPath);reportFile.open(QIODevice::ReadOnly);
        check("HTML reports window charge and energy with distinct units",
              reportWritten && QString::fromUtf8(reportFile.readAll()).contains(QStringLiteral("2.20 Ah / 55.0 Wh")));
    }
    QJsonObject result;result.insert("passed",passed);result.insert("checks",checks);result.insert("checkCount",checks.size());result.insert("scope",QStringLiteral("人工合成 DataFlash 日志与 Qt UI；未使用真实试飞日志。"));
    QFile file(output);if(!file.open(QIODevice::WriteOnly))return 2;file.write(QJsonDocument(result).toJson(QJsonDocument::Indented));return passed?0:1;
}
