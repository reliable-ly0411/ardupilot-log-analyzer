#include "diagnostics.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <QRegularExpression>
#include <limits>

namespace {
const Series* get(const LogReport& r, const QString& key)
{
    for (const Series& s : r.series) if (s.key == key && !s.points.isEmpty()) return &s;
    return nullptr;
}
bool inside(const LogReport& r, double t)
{
    return t >= r.flightStartUs / 1e6 && t <= r.flightEndUs / 1e6;
}
struct Episode { double start = 0, end = 0, extreme = 0; int count = 0; };
QVector<Episode> episodes(const LogReport& r, const Series& s,
                         const std::function<bool(double)>& test, double duration, bool minimum = false)
{
    QVector<double> intervals;
    for (int i = 1; i < std::min(200, s.points.size()); ++i) {
        const double dt = s.points[i].x() - s.points[i-1].x();
        if (dt > 0) intervals.append(dt);
    }
    std::sort(intervals.begin(), intervals.end());
    const double maxGap = std::max(0.5, intervals.isEmpty() ? 1.0 : intervals[intervals.size()/2] * 4);
    QVector<Episode> result;
    Episode active;
    auto flush = [&]() {
        if (active.count && active.end - active.start + 1e-6 >= duration) result.append(active);
        active = {};
    };
    for (const QPointF& p : s.points) {
        if (!inside(r, p.x()) || !std::isfinite(p.y()) || !test(p.y())) { flush(); continue; }
        if (active.count && p.x() - active.end > maxGap) flush();
        if (!active.count) { active.start = p.x(); active.extreme = p.y(); }
        active.end = p.x();
        active.extreme = minimum ? std::min(active.extreme, p.y()) : std::max(active.extreme, p.y());
        ++active.count;
    }
    flush();
    return result;
}
void evidence(LogReport& r, const QVector<Episode>& items, const QString& category, const QString& title,
              const QString& detail, const QString& rule, const QString& key, Severity severity = Severity::Warning)
{
    for (int i = 0; i < items.size(); ++i) {
        const Episode& e = items[i];
        Finding f;
        f.severity = severity; f.category = category; f.title = title;
        f.detail = detail;
        f.evidence = QStringLiteral("%1；%2；极值 %3；持续 %4 s；%5 个原始样本；共 %6 段")
            .arg(key, rule).arg(e.extreme, 0, 'g', 6).arg(e.end-e.start, 0, 'f', 2).arg(e.count).arg(items.size());
        f.timeUs = qRound64(e.start*1e6); f.endTimeUs = qRound64(e.end*1e6);
        f.source = QStringLiteral("飞行实测异常");
        r.findings.append(f);
    }
}
void cover(LogReport& r, const QString& title, bool available, const QString& detail)
{
    r.coverage.append({title, available, detail});
}
Series tracking(const LogReport& r, const QString& desired, const QString& actual)
{
    Series error; error.key = desired + " - " + actual;
    const Series* a = get(r, desired); const Series* b = get(r, actual);
    if (!a || !b) return error;
    int j = 0;
    for (const QPointF& p : a->points) {
        while (j+1 < b->points.size() && b->points[j+1].x() <= p.x()) ++j;
        int nearest = j;
        if (j+1 < b->points.size() && std::abs(b->points[j+1].x()-p.x()) < std::abs(b->points[j].x()-p.x())) nearest = j+1;
        if (std::abs(b->points[nearest].x()-p.x()) <= 0.05) error.points.append({p.x(), std::abs(p.y()-b->points[nearest].y())});
    }
    return error;
}
}

void appendMeasuredDiagnostics(LogReport& r)
{
    const DiagnosticOptions& o = r.diagnosticOptions;
    r.coverage.clear();
    const bool flightWindow = r.flightStartUs > 0 && r.flightEndUs > r.flightStartUs;
    cover(r, QStringLiteral("飞行时段"), flightWindow, r.flightWindowInferred ? QStringLiteral("根据运动推断；请在曲线中复核起止，不能区分多次起降。") : QStringLiteral("第一条解锁至最后一条上锁；多架次合并日志需分段复核。"));
    for (const auto& axis : {QStringLiteral("Roll"), QStringLiteral("Pitch")}) {
        const double limit = axis == "Roll" ? o.rollError : o.pitchError;
        const QString name = axis == "Roll" ? QStringLiteral("横滚") : QStringLiteral("俯仰");
        Series error = tracking(r, "ATT.Des" + axis, "ATT." + axis);
        cover(r, name + QStringLiteral("目标跟踪"), !error.points.isEmpty(), QStringLiteral("|目标−实际| > %1° 且持续 ≥ %2 s；配对时间差 ≤ 50 ms。").arg(limit).arg(o.trackingDuration));
        evidence(r, episodes(r, error, [limit](double v) { return v > limit; }, o.trackingDuration),
            QStringLiteral("姿态控制"), name + QStringLiteral("目标跟踪偏差持续偏大"),
            QStringLiteral("先核对是否为主动大幅机动或转换。叠加目标与实际姿态，检查延迟、超调和输出饱和，再决定是否调整控制参数。此门限是筛查设置，不代表机型验收标准。"),
            QStringLiteral("误差 > %1°，持续 ≥ %2 s").arg(limit).arg(o.trackingDuration), error.key);
    }
    bool vibe = false;
    for (const QString& key : {QStringLiteral("VIBE.VibeX"),QStringLiteral("VIBE.VibeY"),QStringLiteral("VIBE.VibeZ")}) {
        const Series* s = get(r,key); if (!s) continue; vibe = true;
        evidence(r, episodes(r,*s,[&](double v){return v>o.vibration;},0), QStringLiteral("振动"), QStringLiteral("振动超过筛查门限"),
            QStringLiteral("比较三个轴和电机输出，观察尖峰是否出现在旋翼介入、转换或着陆。先排查桨叶、电机、机架及飞控安装；单个尖峰需结合削波复核。"),
            QStringLiteral("> %1 m/s²").arg(o.vibration),key);
    }
    cover(r,QStringLiteral("振动"),vibe,QStringLiteral("原始 VIBE 三轴；门限 %1 m/s²；保留短时尖峰。").arg(o.vibration));
    bool clip = false;
    for (const Series& s : r.series) {
        if (!s.key.startsWith("VIBE") || !s.fieldName.startsWith("Clip") || s.points.isEmpty()) continue;
        clip = true; Series delta; delta.key=s.key;
        double previous = s.points.first().y();
        for (const QPointF& p : s.points) {
            if (p.y()>previous) delta.points.append({p.x(),p.y()-previous});
            previous=p.y();
        }
        evidence(r,episodes(r,delta,[](double v){return v>0;},0),QStringLiteral("惯性传感器"),QStringLiteral("飞行中新增加速度计削波"),
            QStringLiteral("这是累计计数在飞行窗口内的增量，已排除窗口之前的历史计数。对照振动尖峰和电机输出，优先检查机械振动。"),QStringLiteral("计数增量 > 0"),s.key);
    }
    cover(r,QStringLiteral("IMU 削波增量"),clip,QStringLiteral("需要 VIBE.Clip/Clip0/Clip1/Clip2；只计算飞行窗口内新增计数。"));
    bool ekf = false;
    for (const Series& s : r.series) {
        if (!(s.messageType.startsWith("XKF4") || s.messageType.startsWith("NKF4")) ||
            !(QStringList{"SV","SP","SH","SM","SVT"}.contains(s.fieldName))) continue;
        ekf=true;
        evidence(r,episodes(r,s,[&](double v){return std::abs(v)>o.ekfRatio;},0.5),QStringLiteral("EKF"),QStringLiteral("EKF 创新检验比持续超限"),
            QStringLiteral("查看对应 EKF 核心的速度、位置、高度、磁场或空速检验比，叠加 GPS、振动与电源。只对无量纲创新检验比执行门限，姿态误差和光流数值不参与。"),
            QStringLiteral("|比值| > %1，持续 ≥ 0.5 s").arg(o.ekfRatio),s.key);
    }
    cover(r,QStringLiteral("EKF 创新检验"),ekf,QStringLiteral("XKF4/NKF4 的 SV/SP/SH/SM/SVT；区分 EKF 核心，门限 %1。").arg(o.ekfRatio));
    const Series* fix=get(r,"GPS.Status"); const Series* sats=get(r,"GPS.NSats");
    cover(r,QStringLiteral("GPS 定位"),fix && sats,QStringLiteral("主 GPS 状态 < 3 持续 1 s，或卫星数 < 8 持续 2 s；缺失状态数据时无法确认失锁。"));
    if (fix) evidence(r,episodes(r,*fix,[](double v){return v<3;},1,true),QStringLiteral("定位"),QStringLiteral("主 GPS 定位状态不足 3D Fix"),
        QStringLiteral("定位状态低于 3 不等同于飞控完全失去导航。检查是否切到另一 GPS、EKF 是否使用替代位置源，再看轨迹和模式变化。"),QStringLiteral("Status < 3，持续 ≥ 1 s"),fix->key);
    if (sats) evidence(r,episodes(r,*sats,[](double v){return v<8;},2,true),QStringLiteral("定位"),QStringLiteral("主 GPS 卫星数持续偏少"),
        QStringLiteral("对照定位状态、HDop 和实际环境。卫星数门限仅提示定位冗余，不能单独判定导航故障。"),QStringLiteral("NSats < 8，持续 ≥ 2 s"),sats->key,Severity::Notice);
    bool battery=false;
    for (const BatterySummary& b : r.batteries) {
        const QString prefix=b.instance==0 ? "BATT_" : QStringLiteral("BATT%1_").arg(b.instance+1);
        const Series* s=get(r,QStringLiteral("bat%1_voltage").arg(b.instance)); if (!s) continue;
        const double low=r.parameters.value(prefix+"LOW_VOLT").value;
        const double critical=r.parameters.value(prefix+"CRT_VOLT").value;
        battery=battery || low>0 || critical>0;
        if (critical>0) evidence(r,episodes(r,*s,[critical](double v){return v>0 && v<critical;},2,true),QStringLiteral("电池安全"),QStringLiteral("电池 %1 电压低于严重低电门限").arg(b.instance+1),
            QStringLiteral("门限来自本日志的电池参数。对照电流检查负载压降，并查看电池失效保护消息；该规则不重现固件的计时和滤波逻辑。"),QStringLiteral("%1CRT_VOLT=%2 V，持续 ≥ 2 s").arg(prefix).arg(critical),s->key,Severity::Critical);
        if (low>0) evidence(r,episodes(r,*s,[low,critical](double v){return v>0 && v<low && (critical<=0 || v>=critical);},2,true),QStringLiteral("电池安全"),QStringLiteral("电池 %1 电压低于低电门限").arg(b.instance+1),
            QStringLiteral("对照电流判断瞬时压降或持续低电，并确认电芯数量、门限与 BATT_FS_VOLTSRC。此规则使用实测 BAT.Volt，不重现固件电压滤波。"),QStringLiteral("%1LOW_VOLT=%2 V，持续 ≥ 2 s").arg(prefix).arg(low),s->key);
    }
    cover(r,QStringLiteral("电池低压"),battery,QStringLiteral("需要 BAT 电压与对应 BATT/BATT2 的正值低电门限；未设置门限时不猜测电芯数量。"));
    const Series* vcc=get(r,"POWR.Vcc");
    cover(r,QStringLiteral("飞控供电"),vcc,QStringLiteral("POWR.Vcc < 4.6 V 持续 0.2 s；仅适用于常见 5 V 供电飞控。"));
    if (vcc) evidence(r,episodes(r,*vcc,[](double v){return v>0 && v<4.6;},0.2,true),QStringLiteral("电源"),QStringLiteral("飞控 5 V 供电出现压降"),
        QStringLiteral("确认本机硬件额定输入为 5 V。叠加舵机供电、电池电流和模式事件，排查 BEC 容量、接插件和布线。"),QStringLiteral("0 < Vcc < 4.6 V，持续 ≥ 0.2 s"),vcc->key);
    bool motors=false;
    const int motorProtocol=qRound(r.parameters.value("Q_M_PWM_TYPE").value);
    for (int channel=1;channel<=32;++channel) {
        const int function=qRound(r.parameters.value(QStringLiteral("SERVO%1_FUNCTION").arg(channel)).value);
        if (function<33 || function>40 || (r.parameters.contains("Q_M_PWM_TYPE") && motorProtocol>=4)) continue;
        const Series* s=get(r,QStringLiteral("RCOU.C%1").arg(channel)); if (!s) continue;
        double low=r.parameters.value("Q_M_PWM_MIN").value, high=r.parameters.value("Q_M_PWM_MAX").value;
        if (low<=0) low=r.parameters.value(QStringLiteral("SERVO%1_MIN").arg(channel)).value;
        if (high<=0) high=r.parameters.value(QStringLiteral("SERVO%1_MAX").arg(channel)).value;
        if (low<500 || high>2500 || high<=low) continue;
        // DShot logs can use a different numeric range. Do not apply PWM thresholds to those samples.
        if (std::none_of(s->points.cbegin(),s->points.cend(),[&](const QPointF& p){return inside(r,p.x()) && p.y()>=low;})) continue;
        motors=true; const double threshold=low+0.98*(high-low);
        evidence(r,episodes(r,*s,[threshold,high](double v){return v>=threshold && v<=high+100;},o.motorDuration),QStringLiteral("垂起动力"),QStringLiteral("旋翼电机通道 %1 持续接近输出上限").arg(channel),
            QStringLiteral("此项表示控制输出余量减少，不代表测得实际推力或转速。结合姿态、下降率、载荷和电压检查是否处于大机动；确认电机协议和 PWM 范围。"),
            QStringLiteral("输出 ≥ %1 µs（范围 %2–%3 的 98%），持续 ≥ %4 s").arg(threshold,0,'f',0).arg(low).arg(high).arg(o.motorDuration),s->key);
    }
    // Consolidate simultaneous motor warnings so one event does not bury other evidence.
    for(int i=0;i<r.findings.size();++i){
        Finding first=r.findings[i];if(!first.title.startsWith(QStringLiteral("旋翼电机通道")))continue;
        QStringList keys{first.evidence.section(QStringLiteral("；"),0,0)};
        for(int j=r.findings.size()-1;j>i;--j){const Finding& next=r.findings[j];if(next.title.startsWith(QStringLiteral("旋翼电机通道")) && next.timeUs==first.timeUs && next.endTimeUs==first.endTimeUs){keys.append(next.evidence.section(QStringLiteral("；"),0,0));first.evidence+="\n"+next.evidence;r.findings.removeAt(j);}}
        if(keys.size()>1)first.title=QStringLiteral("%1 路旋翼输出同时持续接近上限").arg(keys.size());
        r.findings[i]=first;
    }
    cover(r,QStringLiteral("垂起动力余量"),motors,QStringLiteral("需要电机功能映射、RCOU 和有效 PWM 上下限；不依据未知输出量纲判断饱和。"));
    const Series* trn=get(r,"QTUN.Trn");
    const Series* assist=get(r,"QTUN.Ast");
    const Series* mode=get(r,"MODE.Mode");if(!mode)mode=get(r,"MODE.ModeNum");
    const double timeout=r.parameters.value("Q_TRANS_FAIL").value;
    // Official Plane 4.5 / 4.6: SLT 0=AirspeedWait, 1=Timer, 2=Done.
    // VTOL_update also sets 0 during hover: require fixed-wing mode and Ast=1.
    const bool knownVersion=QRegularExpression(QStringLiteral("^ArduPlane\\s+V?4\\.(5|6)\\.")).match(r.firmware).hasMatch();
    const bool standardTransition=r.vehicleKind==VehicleKind::QuadPlane &&
        r.parameters.contains("Q_TAILSIT_ENABLE") && r.parameters.value("Q_TAILSIT_ENABLE").value==0;
    const bool transitionAvailable=knownVersion && standardTransition && trn && assist && mode && timeout>0;
    cover(r,QStringLiteral("前转换空速等待"),transitionAvailable,
        QStringLiteral("支持 ArduPlane 4.5/4.6 非尾座式；需要 QTUN.Trn/Ast、MODE、Q_TAILSIT_ENABLE=0 与正值 Q_TRANS_FAIL。仅固定翼模式下 Trn=0 且 Ast=1 的空速等待计时；1 是计时退出，2 是完成。"));
    if(transitionAvailable){
        Series waiting;waiting.key="QTUN.Trn + QTUN.Ast + MODE";int ai=0,mi=0;
        for(const QPointF& point:trn->points){
            while(ai+1<assist->points.size() && assist->points[ai+1].x()<=point.x())++ai;
            int nearest=ai;if(ai+1<assist->points.size() && std::abs(assist->points[ai+1].x()-point.x())<std::abs(assist->points[ai].x()-point.x()))nearest=ai+1;
            while(mi+1<mode->points.size() && mode->points[mi+1].x()<=point.x())++mi;
            const bool fixedMode=mode->points[mi].x()<=point.x() && mode->points[mi].y()>=0 && mode->points[mi].y()<17;
            const bool assisting=std::abs(assist->points[nearest].x()-point.x())<=0.05 && assist->points[nearest].y()>0.5;
            waiting.points.append({point.x(),point.y()==0 && fixedMode && assisting?1.0:0.0});
        }
        evidence(r,episodes(r,waiting,[](double v){return v>0;},timeout),QStringLiteral("垂起转换"),QStringLiteral("前转换空速等待持续超过配置超时"),
            QStringLiteral("在固定翼模式且旋翼辅助已介入时，空速等待状态持续超过 Q_TRANS_FAIL。检查空速、推力与 Transition failed 消息。此项是实测状态筛查，不重现固件内部计时；不能用来判断后转换超时。"),
            QStringLiteral("Trn=0、Ast=1、MODE<17，持续 ≥ Q_TRANS_FAIL=%1 s；Trn=2 为转换完成").arg(timeout),waiting.key);
    }
    cover(r,QStringLiteral("飞控错误与保护消息"),r.availableMessageTypes.contains("ERR") || r.availableMessageTypes.contains("MSG"),QStringLiteral("仅把非零 ERR 错误码和实际保护触发消息作为异常；参数配置提示另行列出。"));
    for (const FlightEvent& e : r.events) {
        if (e.severity < Severity::Warning || !inside(r,e.timeUs/1e6)) continue;
        Finding f; f.severity=e.severity; f.category=e.category; f.title=e.name;
        f.detail=QStringLiteral("飞控在该时间记录了错误或保护消息。先阅读原始文本，核对是否为实际触发或状态说明，再对照事件前后 30 秒的传感器和控制输出。");
        f.evidence=e.detail; f.timeUs=e.timeUs; f.endTimeUs=e.timeUs; f.source=QStringLiteral("飞控事件证据");
        r.findings.append(f);
    }
    if(r.skippedBytes>0){Finding f;f.severity=Severity::Notice;f.category=QStringLiteral("数据完整性");f.title=QStringLiteral("日志中存在跳过的字节");f.detail=QStringLiteral("解析器在重新同步记录时跳过了数据。可能是未知格式、截断或损坏，相关区间不能据此判断正常。");f.evidence=QStringLiteral("跳过 %1 字节 / 文件 %2 字节；解码 %3 条记录").arg(r.skippedBytes).arg(r.fileSize).arg(r.decodedRecords);f.source=QStringLiteral("数据完整性");r.findings.append(f);}
    const int missing=std::count_if(r.coverage.cbegin(),r.coverage.cend(),[](const RuleCoverage& c){return !c.available;});
    if (missing) {
        Finding f; f.severity=Severity::Notice; f.category=QStringLiteral("数据完整性"); f.title=QStringLiteral("%1 类检查缺少数据或有效门限").arg(missing);
        f.detail=QStringLiteral("打开“分析引导”查看规则覆盖情况。未发现异常只表示已有证据没有触发当前规则；缺失的检查没有得出正常结论。");
        f.evidence=QStringLiteral("可执行 %1 / %2 类检查").arg(r.coverage.size()-missing).arg(r.coverage.size()); f.source=QStringLiteral("数据完整性"); r.findings.append(f);
    }
}
