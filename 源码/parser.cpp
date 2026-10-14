#include "parser.h"
#include "translations.h"
#include "diagnostics.h"
#include <QThread>

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace {

struct FormatDefinition {
    int length = 0;
    QString name;
    QString format;
    QStringList columns;
};

struct DecodedMessage {
    qint64 timeUs = 0;
    QHash<QString, double> numbers;
    QHash<QString, QString> strings;
};

struct ParseState {
    LogReport report;
    QHash<int, FormatDefinition> formats;
    QHash<QString, int> seriesIndex;
    QHash<QString, qint64> lastSeriesTime;
    QHash<int, qint64> lastRawSampleTime;
    QHash<int, int> batteryIndex;
    QVector<GeoSample> posRoute;
    QVector<GeoSample> gpsRoute;
    qint64 lastPosRouteTime = 0;
    qint64 lastGpsRouteTime = 0;
    qint64 motionStartUs = 0;
    qint64 firstArmUs = 0;
    qint64 lastDisarmUs = 0;
    qint64 firstDataUs = 0;
    bool armed = false;
    bool sawArmMessage = false;
    int lastMode = -1;
    int lastQposState = -1;
    bool arspSeen = false;
    bool arspHealthy = false;
};

QString asciiString(const uchar* data, int size)
{
    QByteArray bytes(reinterpret_cast<const char*>(data), size);
    const int nullIndex = bytes.indexOf('\0');
    if (nullIndex >= 0) bytes.truncate(nullIndex);
    return QString::fromLatin1(bytes).trimmed();
}

int fieldSize(QChar code)
{
    switch (code.unicode()) {
    case 'b': case 'B': case 'M': return 1;
    case 'h': case 'H': case 'c': case 'C': case 'g': return 2;
    case 'i': case 'I': case 'f': case 'e': case 'E': case 'L': return 4;
    case 'q': case 'Q': case 'd': return 8;
    case 'n': return 4;
    case 'N': return 16;
    case 'Z': return 64;
    case 'a': return 64;
    default: return 0;
    }
}

float halfToFloat(quint16 value)
{
    const quint32 sign = (value & 0x8000u) << 16;
    quint32 exponent = (value >> 10) & 0x1fu;
    quint32 mantissa = value & 0x3ffu;
    quint32 bits = 0;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {
            exponent = 1;
            while ((mantissa & 0x400u) == 0) {
                mantissa <<= 1;
                --exponent;
            }
            mantissa &= 0x3ffu;
            bits = sign | ((exponent + 112u) << 23) | (mantissa << 13);
        }
    } else if (exponent == 31) {
        bits = sign | 0x7f800000u | (mantissa << 13);
    } else {
        bits = sign | ((exponent + 112u) << 23) | (mantissa << 13);
    }
    float result = 0;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

double readNumber(const uchar* data, QChar code)
{
    switch (code.unicode()) {
    case 'b': return *reinterpret_cast<const qint8*>(data);
    case 'B': case 'M': return *data;
    case 'h': return qFromLittleEndian<qint16>(data);
    case 'H': return qFromLittleEndian<quint16>(data);
    case 'i': return qFromLittleEndian<qint32>(data);
    case 'I': return qFromLittleEndian<quint32>(data);
    case 'q': return static_cast<double>(qFromLittleEndian<qint64>(data));
    case 'Q': return static_cast<double>(qFromLittleEndian<quint64>(data));
    case 'f': { float value = 0; std::memcpy(&value, data, sizeof(value)); return value; }
    case 'd': { double value = 0; std::memcpy(&value, data, sizeof(value)); return value; }
    case 'g': return halfToFloat(qFromLittleEndian<quint16>(data));
    case 'c': return qFromLittleEndian<qint16>(data) / 100.0;
    case 'C': return qFromLittleEndian<quint16>(data) / 100.0;
    case 'e': return qFromLittleEndian<qint32>(data) / 100.0;
    case 'E': return qFromLittleEndian<quint32>(data) / 100.0;
    case 'L': return qFromLittleEndian<qint32>(data) / 1.0e7;
    default: return 0;
    }
}

DecodedMessage decode(const uchar* payload, int payloadSize, const FormatDefinition& format)
{
    DecodedMessage result;
    int offset = 0;
    const int count = std::min(format.format.size(), format.columns.size());
    for (int index = 0; index < count; ++index) {
        const QChar code = format.format.at(index);
        const int size = fieldSize(code);
        if (size <= 0 || offset + size > payloadSize) break;
        const QString column = format.columns.at(index).trimmed();
        if (code == 'n' || code == 'N' || code == 'Z') {
            result.strings.insert(column, asciiString(payload + offset, size));
        } else if (code != 'a') {
            result.numbers.insert(column, readNumber(payload + offset, code));
        }
        offset += size;
    }
    result.timeUs = static_cast<qint64>(result.numbers.value(QStringLiteral("TimeUS"), result.numbers.value(QStringLiteral("TimeMS"), 0) * 1000.0));
    return result;
}

double numberValue(const DecodedMessage& message, std::initializer_list<const char*> names, double fallback = 0)
{
    for (const char* name : names) {
        const auto found = message.numbers.constFind(QString::fromLatin1(name));
        if (found != message.numbers.constEnd()) return found.value();
    }
    return fallback;
}

QString stringValue(const DecodedMessage& message, std::initializer_list<const char*> names)
{
    for (const char* name : names) {
        const auto found = message.strings.constFind(QString::fromLatin1(name));
        if (found != message.strings.constEnd()) return found.value();
    }
    return {};
}

bool finite(double value)
{
    return std::isfinite(value);
}

void ensureSeries(ParseState& state, const QString& key, const QString& title, const QString& english, const QString& unit)
{
    if (state.seriesIndex.contains(key)) return;
    Series series;
    series.key = key;
    series.title = title;
    series.english = english;
    series.unit = unit;
    state.seriesIndex.insert(key, state.report.series.size());
    state.report.series.append(series);
}

void appendSeries(ParseState& state, const QString& key, qint64 timeUs, double value, qint64 intervalUs = 0)
{
    if (!finite(value) || timeUs <= 0 || !state.seriesIndex.contains(key)) return;
    const qint64 last = state.lastSeriesTime.value(key, 0);
    if (last && timeUs - last < intervalUs) return;
    state.lastSeriesTime.insert(key, timeUs);
    state.report.series[state.seriesIndex.value(key)].points.append(QPointF(timeUs / 1000000.0, value));
}

void ensureRawSeries(ParseState& state, const QString& messageType, const QString& fieldName)
{
    const QString key = messageType + QLatin1Char('.') + fieldName;
    if (state.seriesIndex.contains(key)) return;
    Series series;
    series.key = key;
    series.title = QStringLiteral("%1（%2） · %3（%4）")
        .arg(messageType, messageChineseName(messageType.section('[',0,0)), fieldName, fieldChineseName(messageType.section('[',0,0), fieldName));
    series.english = key;
    series.messageType = messageType;
    series.fieldName = fieldName;
    state.seriesIndex.insert(key, state.report.series.size());
    state.report.series.append(series);
}

QString instanceName(const QString& type, const DecodedMessage& message)
{
    int instance = 0;
    if (message.numbers.contains("Inst")) instance = qRound(message.numbers.value("Inst"));
    else if (message.numbers.contains("Instance")) instance = qRound(message.numbers.value("Instance"));
    else if ((type.startsWith("XKF") || type.startsWith("NKF")) && message.numbers.contains("C")) instance = qRound(message.numbers.value("C"));
    else if (message.numbers.contains("I") && (type.startsWith("GPS") || type == "IMU" || type == "BARO" || type == "ARSP" || type == "VIBE" || type.startsWith("MAG"))) instance = qRound(message.numbers.value("I"));
    return instance > 0 ? type + QStringLiteral("[%1]").arg(instance) : type;
}

QString knownUnit(const QString& type, const QString& field)
{
    if ((type == "RCOU" || type == "RCIN") && field.startsWith('C')) return QStringLiteral("µs");
    if (type == "ATT" && (field.contains("Roll") || field.contains("Pitch") || field.contains("Yaw"))) return QStringLiteral("°");
    if (type == "RATE" && (field == "R" || field == "P" || field == "Y" || field.endsWith("Des"))) return QStringLiteral("°/s");
    if (type == "QTUN" && (field.endsWith("Alt"))) return QStringLiteral("m");
    if (type == "QTUN" && (field.endsWith("CRt"))) return QStringLiteral("m/s");
    if (type == "QTUN" && (field.startsWith("Th"))) return QStringLiteral("比例");
    if (type == "GPS" && field=="NSats") return QStringLiteral("颗");
    if (type == "GPS" && (field=="HDop" || field=="Status")) return QStringLiteral("无量纲");
    if ((type=="XKF4" || type=="NKF4") && QStringList{"SV","SP","SH","SM","SVT"}.contains(field)) return QStringLiteral("检验比");
    if (type == "VIBE" && field.startsWith("Vibe")) return QStringLiteral("m/s²");
    if (type == "VIBE" && field.startsWith("Clip")) return QStringLiteral("次");
    if ((type == "BAT" && field.startsWith("Volt")) || (type == "POWR" && field.startsWith('V'))) return QStringLiteral("V");
    if (type == "BAT" && field == "Curr") return QStringLiteral("A");
    if (type == "BAT" && field == "CurrTot") return QStringLiteral("Ah");
    if (type == "BAT" && field == "EnrgTot") return QStringLiteral("Wh");
    if (type == "BAT" && field == "RemPct") return QStringLiteral("%");
    if (field == "Temp") return QStringLiteral("°C");
    if (field == "Lat" || field == "Lng") return QStringLiteral("°");
    if (field == "Alt" || field == "RelHomeAlt" || field == "RelOriginAlt" || (type == "QPOS" && field == "Dist")) return QStringLiteral("m");
    if (field == "Airspeed" || field == "Spd" || field == "GSpd" || field == "VZ" || (type == "QPOS" && field == "TSpd")) return QStringLiteral("m/s");
    if (type == "QPOS" && field == "TAcc") return QStringLiteral("m/s²");
    return {};
}

void appendRawFields(ParseState& state, const FormatDefinition& format, const DecodedMessage& message)
{
    if (message.timeUs <= 0 || format.name=="PARM" || format.name=="MSG") return;
    const QString name = instanceName(format.name, message);
    for (auto it = message.numbers.cbegin(); it != message.numbers.cend(); ++it) {
        if (it.key() == "TimeUS" || it.key() == "TimeMS") continue;
        ensureRawSeries(state, name, it.key());
        Series& series = state.report.series[state.seriesIndex.value(name + '.' + it.key())];
        series.unit = knownUnit(format.name, it.key());
        appendSeries(state, series.key, message.timeUs, it.value(), 0);
    }
}

BatterySummary& battery(ParseState& state, int instance)
{
    if (!state.batteryIndex.contains(instance)) {
        state.batteryIndex.insert(instance, state.report.batteries.size());
        BatterySummary summary;
        summary.instance = instance;
        state.report.batteries.append(summary);
    }
    return state.report.batteries[state.batteryIndex.value(instance)];
}

void addEvent(ParseState& state, qint64 timeUs, const QString& category, const QString& name, const QString& detail, Severity severity = Severity::Info)
{
    if (state.report.events.size() >= 2500) return;
    state.report.events.append({timeUs, category, name, detail, severity});
}

void addFinding(LogReport& report, Severity severity, const QString& category, const QString& title, const QString& detail, const QString& evidence, qint64 timeUs = 0)
{
    Finding finding;finding.severity=severity;finding.category=category;finding.title=title;finding.detail=detail;finding.evidence=evidence;finding.timeUs=timeUs;report.findings.append(finding);
}

double parameter(const LogReport& report, const QString& name, double fallback = 0, bool* exists = nullptr)
{
    const auto found = report.parameters.constFind(name);
    const bool present = found != report.parameters.constEnd();
    if (exists) *exists = present;
    return present ? found.value().value : fallback;
}

QString frameClassName(int value)
{
    static const QMap<int, QString> names = {
        {1,"四旋翼"},{2,"六旋翼"},{3,"八旋翼"},{4,"八旋翼共轴"},{5,"Y6 共轴六旋翼"},
        {7,"三旋翼"},{8,"单旋翼直升机"},{9,"双旋翼直升机"},{10,"十旋翼"},{11,"十二旋翼"},
        {12,"十二旋翼共轴"},{13,"动态脚本机架"},{14,"直升机混控"},{15,"六自由度机架"}
    };
    return names.value(value, QStringLiteral("机架类别 %1").arg(value));
}

QString frameTypeName(int value)
{
    static const QMap<int, QString> names = {
        {0,"Plus 布局"},{1,"X 布局"},{2,"V 布局"},{3,"H 布局"},{4,"V 尾布局"},
        {5,"A 尾布局"},{10,"Y6B 布局"},{11,"DJI X 布局"},{12,"顺时针 X 布局"}
    };
    return names.value(value, QStringLiteral("布局 %1").arg(value));
}

int countVtolMotors(const LogReport& report)
{
    int count = 0;
    for (int index = 1; index <= 32; ++index) {
        const int function = qRound(parameter(report, QStringLiteral("SERVO%1_FUNCTION").arg(index), -1));
        if (function >= 33 && function <= 40) ++count;
    }
    return count;
}

bool hasForwardMotor(const LogReport& report)
{
    for (int index = 1; index <= 32; ++index) {
        const int function = qRound(parameter(report, QStringLiteral("SERVO%1_FUNCTION").arg(index), -1));
        if (function == 70 || function == 73 || function == 74) return true;
    }
    return false;
}

void detectVehicle(ParseState& state)
{
    LogReport& report = state.report;
    const QString firmware = report.firmware;
    const bool quadEnabled = parameter(report, QStringLiteral("Q_ENABLE"), 0) > 0 ||
                             report.availableMessageTypes.contains(QStringLiteral("QPOS")) ||
                             report.messages.join('\n').contains(QStringLiteral("QuadPlane"), Qt::CaseInsensitive);

    if (firmware.startsWith(QStringLiteral("ArduPlane"))) {
        report.vehicleKind = quadEnabled ? VehicleKind::QuadPlane : VehicleKind::FixedWing;
    } else if (firmware.startsWith(QStringLiteral("ArduCopter"))) {
        report.vehicleKind = VehicleKind::Copter;
    } else if (firmware.startsWith(QStringLiteral("ArduRover"))) {
        report.vehicleKind = qRound(parameter(report, QStringLiteral("FRAME_CLASS"), 1)) == 2 ? VehicleKind::Boat : VehicleKind::Rover;
    } else if (firmware.startsWith(QStringLiteral("ArduSub"))) {
        report.vehicleKind = VehicleKind::Sub;
    } else if (firmware.startsWith(QStringLiteral("AntennaTracker"))) {
        report.vehicleKind = VehicleKind::Tracker;
    } else if (firmware.startsWith(QStringLiteral("Blimp"))) {
        report.vehicleKind = VehicleKind::Blimp;
    }
    report.vehicleName = vehicleKindName(report.vehicleKind);

    if (report.vehicleKind == VehicleKind::QuadPlane) {
        if (parameter(report, QStringLiteral("Q_TAILSIT_ENABLE"), 0) > 0) {
            report.layoutName = QStringLiteral("尾座式垂直起降固定翼");
        } else if (parameter(report, QStringLiteral("Q_TILT_ENABLE"), 0) > 0) {
            report.layoutName = QStringLiteral("倾转旋翼垂直起降固定翼");
        } else {
            const int frameClass = qRound(parameter(report, QStringLiteral("Q_FRAME_CLASS"), 0));
            const int frameType = qRound(parameter(report, QStringLiteral("Q_FRAME_TYPE"), 0));
            const int motorCount = countVtolMotors(report);
            const bool forward = hasForwardMotor(report);
            report.layoutName = QStringLiteral("%1 %2").arg(frameClassName(frameClass), frameTypeName(frameType));
            if (motorCount > 0) report.layoutName += QStringLiteral(" · %1 个垂起电机").arg(motorCount);
            if (forward) report.layoutName += QStringLiteral(" + 固定翼推进");
            if (motorCount == 4 && forward) report.layoutName += QStringLiteral("（4+1）");
        }
        report.confidence = QStringLiteral("高：固件、Q_ 参数、垂起状态和输出功能综合识别");
    } else if (report.vehicleKind == VehicleKind::Copter) {
        report.layoutName = QStringLiteral("%1 · %2").arg(frameClassName(qRound(parameter(report, QStringLiteral("FRAME_CLASS"), 0))), frameTypeName(qRound(parameter(report, QStringLiteral("FRAME_TYPE"), 0))));
        report.confidence = QStringLiteral("高：ArduCopter 固件与机架参数一致");
    } else if (report.vehicleKind != VehicleKind::Unknown) {
        report.layoutName = report.vehicleName;
        report.confidence = QStringLiteral("高：由固件标识识别");
    } else {
        report.layoutName = QStringLiteral("无法从当前日志确认");
        report.confidence = QStringLiteral("低：缺少可识别的固件文本消息");
    }
}

bool inFlightWindow(const LogReport& report, double seconds)
{
    const qint64 timeUs = static_cast<qint64>(seconds * 1000000.0);
    return timeUs >= report.flightStartUs && timeUs <= report.flightEndUs;
}

const Series* findSeries(const LogReport& report, const QString& key)
{
    for (const Series& series : report.series) if (series.key == key) return &series;
    return nullptr;
}

QPair<double, double> seriesRange(const LogReport& report, const QString& key)
{
    const Series* series = findSeries(report, key);
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    if (series) {
        for (const QPointF& point : series->points) {
            if (!inFlightWindow(report, point.x())) continue;
            minimum = std::min(minimum, point.y());
            maximum = std::max(maximum, point.y());
        }
    }
    return {minimum, maximum};
}

double consumption(const LogReport& report,const QString& key)
{
    const Series* series=findSeries(report,key);
    if(!series)return std::numeric_limits<double>::quiet_NaN();
    double total=0,previous=0;bool started=false;
    for(const QPointF& p:series->points){
        if(!inFlightWindow(report,p.x()))continue;
        if(started && p.y()>previous)total+=p.y()-previous;
        previous=p.y();started=true;
    }
    return started?total:std::numeric_limits<double>::quiet_NaN();
}

double haversine(double lat1, double lon1, double lat2, double lon2)
{
    constexpr double earthRadius = 6371000.0;
    constexpr double toRadians = 3.14159265358979323846 / 180.0;
    const double p1 = lat1 * toRadians;
    const double p2 = lat2 * toRadians;
    const double dp = (lat2 - lat1) * toRadians;
    const double dl = (lon2 - lon1) * toRadians;
    const double a = std::sin(dp / 2) * std::sin(dp / 2) + std::cos(p1) * std::cos(p2) * std::sin(dl / 2) * std::sin(dl / 2);
    return earthRadius * 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
}

void calculateMetrics(ParseState& state)
{
    LogReport& report = state.report;
    if (state.firstArmUs > 0) {
        report.flightStartUs = state.firstArmUs;
    } else if (state.motionStartUs > 0) {
        report.flightStartUs = state.motionStartUs;
        report.flightWindowInferred = true;
    } else {
        report.flightStartUs = report.logStartUs;
        report.flightWindowInferred = true;
    }
    report.flightEndUs = state.lastDisarmUs > report.flightStartUs ? state.lastDisarmUs : report.logEndUs;

    report.route = !state.posRoute.isEmpty() ? state.posRoute : state.gpsRoute;
    QVector<GeoSample> filteredRoute;
    filteredRoute.reserve(report.route.size());
    for (const GeoSample& point : report.route) {
        if (point.timeUs >= report.flightStartUs && point.timeUs <= report.flightEndUs) filteredRoute.append(point);
    }
    report.route = filteredRoute;

    for (int index = 1; index < report.route.size(); ++index) {
        const GeoSample& previous = report.route.at(index - 1);
        const GeoSample& current = report.route.at(index);
        const double distance = haversine(previous.latitude, previous.longitude, current.latitude, current.longitude);
        const double dt = (current.timeUs - previous.timeUs) / 1000000.0;
        if (dt > 0 && dt < 10 && distance < 300) report.distanceMeters += distance;
    }

    const auto altitude = seriesRange(report, QStringLiteral("altitude"));
    report.minRelativeAltitude = altitude.first;
    report.maxRelativeAltitude = altitude.second;
    report.maxGroundSpeed = seriesRange(report, QStringLiteral("groundspeed")).second;
    report.maxAirSpeed = seriesRange(report, QStringLiteral("airspeed")).second;
    report.maxVibration = seriesRange(report, QStringLiteral("vibration")).second;
    report.maxEkfVariance = seriesRange(report, QStringLiteral("ekf_variance")).second;
    report.maxHdop = seriesRange(report, QStringLiteral("hdop")).second;
    const auto satellites = seriesRange(report, QStringLiteral("satellites"));
    if (finite(satellites.first)) report.minSatellites = qRound(satellites.first);
    const auto roll = seriesRange(report, QStringLiteral("roll"));
    const auto pitch = seriesRange(report, QStringLiteral("pitch"));
    report.maxRoll = std::max(std::abs(roll.first), std::abs(roll.second));
    report.maxPitch = std::max(std::abs(pitch.first), std::abs(pitch.second));
    report.maxClipCount = 0;
    for (const Series& series : report.series) {
        if (!series.key.startsWith("VIBE") || !series.fieldName.startsWith("Clip")) continue;
        double previous = series.points.isEmpty() ? 0 : series.points.first().y();
        for (const QPointF& point : series.points) {
            if (inFlightWindow(report, point.x()) && point.y() > previous) report.maxClipCount += quint64(point.y() - previous);
            previous = point.y();
        }
    }

    for (BatterySummary& summary : report.batteries) {
        const QString prefix = QStringLiteral("bat%1_").arg(summary.instance);
        const auto voltage = seriesRange(report, prefix + QStringLiteral("voltage"));
        const auto current = seriesRange(report, prefix + QStringLiteral("current"));

        const auto remaining = seriesRange(report, prefix + QStringLiteral("remaining"));
        const auto temperature = seriesRange(report, prefix + QStringLiteral("temperature"));
        summary.minVoltage = voltage.first;
        summary.maxCurrent = current.second;
        summary.consumedAh = consumption(report,prefix+"consumed_ah");
        summary.consumedWh = consumption(report,prefix+"consumed_wh");
        summary.minRemainingPercent = remaining.first;
        summary.maxTemperature = temperature.second;
    }
}

void runDiagnostics(ParseState& state)
{
    LogReport& report = state.report;
    if (report.vehicleKind == VehicleKind::QuadPlane) {
        bool hasOptions = false;
        const quint32 options = static_cast<quint32>(std::llround(parameter(report, QStringLiteral("Q_OPTIONS"), 0, &hasOptions)));
        if (hasOptions && (options & (1u << 16))) {
            addFinding(report, Severity::Warning, QStringLiteral("垂起着陆"), QStringLiteral("固定翼进近与空中减速阶段被禁用"),
                       QStringLiteral("Q_OPTIONS 的 bit 16 DisableApproach 已开启。进入 VTOL_LAND 后飞控会直接进入 Position1，旋翼可能在较远位置开始工作。清除此位时应保留其他选项位。"),
                       QStringLiteral("Q_OPTIONS=%1，bit16=1").arg(options), report.vtolPosition1TimeUs);
        }
        bool hasDecel = false;
        const double decel = parameter(report, QStringLiteral("Q_TRANS_DECEL"), 0, &hasDecel);
        if (hasDecel && decel <= 0) {
            addFinding(report, Severity::Critical, QStringLiteral("垂起转换"), QStringLiteral("减速度参数无效"),
                       QStringLiteral("飞控无法用有效减速度计算后转换停止距离。"), QStringLiteral("Q_TRANS_DECEL=%1").arg(decel));
        } else if (hasDecel && decel < 1.2) {
            addFinding(report, Severity::Notice, QStringLiteral("垂起转换"), QStringLiteral("后转换减速度设置偏保守"),
                       QStringLiteral("较低数值会让旋翼更早介入并增加旋翼工作时间。应以相同载荷下的实测减速度校准。"), QStringLiteral("Q_TRANS_DECEL=%1 m/s²").arg(decel, 0, 'f', 2));
        } else if (hasDecel && decel > 3.0) {
            addFinding(report, Severity::Notice, QStringLiteral("垂起转换"), QStringLiteral("后转换减速度设置需要实测复核"),
                       QStringLiteral("如果飞机实际达不到设定减速度，飞控会过晚开始减速并增加冲过落点的风险。"), QStringLiteral("Q_TRANS_DECEL=%1 m/s²").arg(decel, 0, 'f', 2));
        }
        if (parameter(report, QStringLiteral("Q_TRANS_FAIL"), -1) == 0) {
            addFinding(report, Severity::Notice, QStringLiteral("垂起转换"), QStringLiteral("前转换没有超时限制"),
                       QStringLiteral("前转换持续异常时不会由 Q_TRANS_FAIL 触发保护。应根据正常转换时间留出合理裕量，并核对 Q_TRANS_FAIL_ACT。"), QStringLiteral("Q_TRANS_FAIL=0"));
        }
        bool hasAssist = false, hasMinAirspeed = false;
        const double assist = parameter(report, QStringLiteral("Q_ASSIST_SPEED"), 0, &hasAssist);
        const double minimum = parameter(report, QStringLiteral("AIRSPEED_MIN"), 0, &hasMinAirspeed);
        if (hasAssist && hasMinAirspeed && assist > 0 && assist < minimum) {
            addFinding(report, Severity::Notice, QStringLiteral("失速保护"), QStringLiteral("旋翼辅助门限低于最小空速"),
                       QStringLiteral("空速下降到最小空速以下后，仍需继续下降才会触发 QAssist。请结合真实失速速度检查保护裕量。"),
                       QStringLiteral("Q_ASSIST_SPEED=%1 m/s，AIRSPEED_MIN=%2 m/s").arg(assist, 0, 'f', 1).arg(minimum, 0, 'f', 1));
        }

    }

    const bool batteryMonitorEnabled = parameter(report, QStringLiteral("BATT_MONITOR"), 0) > 0 || !report.batteries.isEmpty();
    if (batteryMonitorEnabled && report.parameters.contains("BATT_LOW_VOLT") && report.parameters.contains("BATT_LOW_MAH") && report.parameters.contains("BATT_CRT_VOLT") && report.parameters.contains("BATT_CRT_MAH")) {
        const bool noLow = parameter(report, QStringLiteral("BATT_LOW_VOLT"), 0) <= 0 && parameter(report, QStringLiteral("BATT_LOW_MAH"), 0) <= 0;
        const bool noCritical = parameter(report, QStringLiteral("BATT_CRT_VOLT"), 0) <= 0 && parameter(report, QStringLiteral("BATT_CRT_MAH"), 0) <= 0;
        if (noLow && noCritical) {
            addFinding(report, Severity::Warning, QStringLiteral("电池安全"), QStringLiteral("低电量与严重低电量阈值均未设置"),
                       QStringLiteral("飞控无法依据电压或剩余容量触发分级电池保护。阈值应按电芯数量、化学体系、负载压降和安全返航余量设置。"),
                       QStringLiteral("BATT_LOW_VOLT/MAH=0，BATT_CRT_VOLT/MAH=0"));
        }
    }
    if (parameter(report, QStringLiteral("ARMING_CHECK"), 1) == 0) {
        addFinding(report, Severity::Warning, QStringLiteral("解锁安全"), QStringLiteral("全部解锁检查被关闭"),
                   QStringLiteral("飞控可能在传感器、定位或参数异常时仍允许解锁。除受控调试外不建议使用。"), QStringLiteral("ARMING_CHECK=0"));
    }
    for (Finding& finding : report.findings) finding.source = QStringLiteral("参数配置提示");
    appendMeasuredDiagnostics(report);
    const int warningCount = std::count_if(report.findings.cbegin(), report.findings.cend(), [](const Finding& item) { return item.severity >= Severity::Warning; });
    if (warningCount == 0) {
        addFinding(report, Severity::Info, QStringLiteral("总体"), QStringLiteral("未发现当前规则覆盖范围内的高风险项"),
                   QStringLiteral("仍需结合曲线、事件、任务目标和机型限制复核；自动分析不替代试飞安全评审。"), QStringLiteral("通用与机型专项规则已执行"));
    }
    std::sort(report.findings.begin(), report.findings.end(), [](const Finding& left, const Finding& right) {
        if (left.severity != right.severity) return left.severity > right.severity;
        return left.timeUs < right.timeUs;
    });
}

Severity messageSeverity(const QString& message)
{
    if (message.contains(QStringLiteral("failsafe"), Qt::CaseInsensitive) && QRegularExpression(QStringLiteral("cleared|resolved|ended|disabled|no failsafe|failsafe off"),QRegularExpression::CaseInsensitiveOption).match(message).hasMatch()) return Severity::Info;
    if (message.contains(QStringLiteral("crash"), Qt::CaseInsensitive) || message.contains(QStringLiteral("failsafe"), Qt::CaseInsensitive) || message.contains(QStringLiteral("error"), Qt::CaseInsensitive)) return Severity::Warning;
    if (message.contains(QStringLiteral("fail"), Qt::CaseInsensitive) || message.contains(QStringLiteral("unhealthy"), Qt::CaseInsensitive)) return Severity::Notice;
    return Severity::Info;
}

bool notableMessage(const QString& message)
{
    static const QStringList words = {
        QStringLiteral("Mission:"), QStringLiteral("Transition"), QStringLiteral("VTOL"), QStringLiteral("Land"),
        QStringLiteral("armed"), QStringLiteral("disarmed"), QStringLiteral("failsafe"), QStringLiteral("error"),
        QStringLiteral("Reached waypoint"), QStringLiteral("Set airspeed"), QStringLiteral("GPS"), QStringLiteral("EKF")
    };
    for (const QString& word : words) if (message.contains(word, Qt::CaseInsensitive)) return true;
    return false;
}

void processMessage(ParseState& state, const QString& type, const DecodedMessage& message)
{
    LogReport& report = state.report;
    if (message.timeUs > 0) {
        if (!report.logStartUs || message.timeUs < report.logStartUs) report.logStartUs = message.timeUs;
        report.logEndUs = std::max(report.logEndUs, message.timeUs);
        if (!state.firstDataUs) state.firstDataUs = message.timeUs;
    }

    if (type == QStringLiteral("PARM")) {
        const QString name = stringValue(message, {"Name"});
        if (!name.isEmpty()) {
            ParameterValue value;
            value.value = numberValue(message, {"Value"});
            if (message.numbers.contains(QStringLiteral("Default"))) {
                value.defaultValue = message.numbers.value(QStringLiteral("Default"));
                value.hasDefault = true;
            }
            report.parameters.insert(name, value);
        }
        return;
    }
    if (type == QStringLiteral("MSG")) {
        const QString text = stringValue(message, {"Message"});
        if (text.isEmpty()) return;
        if (report.messages.size() < 1200) report.messages.append(text);
        if (report.firmware.isEmpty()) {
            const QRegularExpression firmwarePattern(QStringLiteral("((?:ArduPlane|ArduCopter|ArduRover|ArduSub|AntennaTracker|Blimp)\\s+V?[^\\s,]+(?:\\s+\\([^)]*\\))?)"));
            const auto match = firmwarePattern.match(text);
            if (match.hasMatch()) report.firmware = match.captured(1);
        }
        if (report.board.isEmpty() && (text.contains(QStringLiteral("Pilot"), Qt::CaseInsensitive) || text.contains(QStringLiteral("Pixhawk"), Qt::CaseInsensitive))) report.board = text;
        if (notableMessage(text)) {
            const Severity severity = messageSeverity(text);
            addEvent(state, message.timeUs, QStringLiteral("飞控消息"), QStringLiteral("状态消息"), text, severity);
            if (severity >= Severity::Warning && text.contains("failsafe",Qt::CaseInsensitive)) ++report.failsafeCount;
        }
        if (!state.motionStartUs && (text.contains(QStringLiteral("Mission: 1"), Qt::CaseInsensitive) || text.contains(QStringLiteral("Takeoff"), Qt::CaseInsensitive))) state.motionStartUs = message.timeUs;
        return;
    }
    if (type == QStringLiteral("ARM")) {
        const bool armed = numberValue(message, {"ArmState"}) > 0.5;
        state.sawArmMessage = true;
        state.armed = armed;
        if (armed && !state.firstArmUs) state.firstArmUs = message.timeUs;
        if (!armed) state.lastDisarmUs = message.timeUs;
        addEvent(state, message.timeUs, QStringLiteral("飞行状态"), armed ? QStringLiteral("飞行器已解锁") : QStringLiteral("飞行器已上锁"),
                 QStringLiteral("ARM.ArmState（解锁状态）=%1").arg(armed ? 1 : 0), armed ? Severity::Notice : Severity::Info);
        return;
    }
    if (type == QStringLiteral("MODE")) {
        const int mode = qRound(numberValue(message, {"Mode", "ModeNum"}, -1));
        if (mode != state.lastMode) {
            state.lastMode = mode;
            addEvent(state, message.timeUs, QStringLiteral("飞行模式"), QStringLiteral("模式切换"),
                     QStringLiteral("MODE（飞行模式）=%1，原因码 Rsn=%2").arg(mode).arg(qRound(numberValue(message, {"Rsn"}, 0))));
            if (!state.motionStartUs && (mode == 10 || mode == 3 || mode == 13)) state.motionStartUs = message.timeUs;
        }
        return;
    }
    if (type == QStringLiteral("POS")) {
        const double latitude = numberValue(message, {"Lat"});
        const double longitude = numberValue(message, {"Lng"});
        const double altitude = numberValue(message, {"RelHomeAlt", "RelOriginAlt"}, std::numeric_limits<double>::quiet_NaN());
        if (finite(altitude)) appendSeries(state, QStringLiteral("altitude"), message.timeUs, altitude, 0);
        if (!state.motionStartUs && finite(altitude) && altitude > 3) state.motionStartUs = message.timeUs;
        if (std::abs(latitude) > 0.1 && std::abs(longitude) > 0.1 && (!state.lastPosRouteTime || message.timeUs - state.lastPosRouteTime >= 500000)) {
            state.lastPosRouteTime = message.timeUs;
            state.posRoute.append({message.timeUs, latitude, longitude, finite(altitude) ? altitude : 0});
        }
        return;
    }
    if (type == QStringLiteral("GPS")) {
        if (numberValue(message, {"I", "Inst", "Instance"}, 0) > 0) return;
        const int status = qRound(numberValue(message, {"Status"}, 3));
        const double speed = numberValue(message, {"Spd", "GSpd"}, std::numeric_limits<double>::quiet_NaN());
        const double satellites = numberValue(message, {"NSats"}, std::numeric_limits<double>::quiet_NaN());
        const double hdop = numberValue(message, {"HDop"}, std::numeric_limits<double>::quiet_NaN());
        if (status >= 3) appendSeries(state, QStringLiteral("groundspeed"), message.timeUs, speed, 0);
        appendSeries(state, QStringLiteral("satellites"), message.timeUs, satellites, 0);
        appendSeries(state, QStringLiteral("hdop"), message.timeUs, hdop, 0);
        if (!state.motionStartUs && finite(speed) && speed > 2) state.motionStartUs = message.timeUs;
        const double latitude = numberValue(message, {"Lat"});
        const double longitude = numberValue(message, {"Lng"});
        const double altitude = numberValue(message, {"Alt"});
        if (status >= 3 && std::abs(latitude) > 0.1 && std::abs(longitude) > 0.1 && (!state.lastGpsRouteTime || message.timeUs - state.lastGpsRouteTime >= 500000)) {
            state.lastGpsRouteTime = message.timeUs;
            state.gpsRoute.append({message.timeUs, latitude, longitude, altitude});
        }
        return;
    }
    if (type == QStringLiteral("BAT")) {
        const int instance = qRound(numberValue(message, {"Inst", "Instance"}, 0));
        BatterySummary& summary = battery(state, instance);
        ++summary.samples;
        const double voltage = numberValue(message, {"Volt", "Voltage"}, std::numeric_limits<double>::quiet_NaN());
        const double current = numberValue(message, {"Curr", "Current"}, std::numeric_limits<double>::quiet_NaN());
        const double consumedAh = numberValue(message, {"CurrTot"}, std::numeric_limits<double>::quiet_NaN());
        const double consumedWh = numberValue(message, {"EnrgTot"}, std::numeric_limits<double>::quiet_NaN());
        const double remaining = numberValue(message, {"RemPct"}, std::numeric_limits<double>::quiet_NaN());
        const double temperature = numberValue(message, {"Temp"}, std::numeric_limits<double>::quiet_NaN());
        const QString prefix = QStringLiteral("bat%1_").arg(instance);
        ensureSeries(state, prefix + QStringLiteral("voltage"), QStringLiteral("电池 %1 电压").arg(instance + 1), QStringLiteral("BAT.Volt"), QStringLiteral("V"));
        ensureSeries(state, prefix + QStringLiteral("current"), QStringLiteral("电池 %1 电流").arg(instance + 1), QStringLiteral("BAT.Curr"), QStringLiteral("A"));
        ensureSeries(state, prefix + QStringLiteral("consumed_ah"), QStringLiteral("电池 %1 累计耗电").arg(instance + 1), QStringLiteral("BAT.CurrTot"), QStringLiteral("Ah"));
        ensureSeries(state, prefix + QStringLiteral("consumed_wh"), QStringLiteral("电池 %1 累计能耗").arg(instance + 1), QStringLiteral("BAT.EnrgTot"), QStringLiteral("Wh"));
        ensureSeries(state, prefix + QStringLiteral("remaining"), QStringLiteral("电池 %1 剩余电量").arg(instance + 1), QStringLiteral("BAT.RemPct"), QStringLiteral("%"));
        ensureSeries(state, prefix + QStringLiteral("temperature"), QStringLiteral("电池 %1 温度").arg(instance + 1), QStringLiteral("BAT.Temp"), QStringLiteral("°C"));
        appendSeries(state, prefix + QStringLiteral("voltage"), message.timeUs, voltage);
        appendSeries(state, prefix + QStringLiteral("current"), message.timeUs, current);
        appendSeries(state, prefix + QStringLiteral("consumed_ah"), message.timeUs, consumedAh, 0);
        appendSeries(state, prefix + QStringLiteral("consumed_wh"), message.timeUs, consumedWh, 0);
        appendSeries(state, prefix + QStringLiteral("remaining"), message.timeUs, remaining, 0);
        appendSeries(state, prefix + QStringLiteral("temperature"), message.timeUs, temperature, 0);
        if (message.numbers.contains(QStringLiteral("H")) && numberValue(message, {"H"}) < 0.5) summary.healthy = false;
        return;
    }
    if (type == QStringLiteral("ARSP")) {
        state.arspSeen = true;
        const bool primary = !message.numbers.contains(QStringLiteral("Pri")) || numberValue(message, {"Pri"}) > 0.5;
        const bool healthy = !message.numbers.contains(QStringLiteral("H")) || numberValue(message, {"H"}) > 0.5;
        if (primary && healthy) {
            state.arspHealthy = true;
            appendSeries(state, QStringLiteral("airspeed"), message.timeUs, numberValue(message, {"Airspeed", "ASpd"}, std::numeric_limits<double>::quiet_NaN()), 0);
        }
        return;
    }
    if (type == QStringLiteral("ATT")) {
        appendSeries(state, QStringLiteral("roll"), message.timeUs, numberValue(message, {"Roll"}, std::numeric_limits<double>::quiet_NaN()), 0);
        appendSeries(state, QStringLiteral("pitch"), message.timeUs, numberValue(message, {"Pitch"}, std::numeric_limits<double>::quiet_NaN()), 0);
        return;
    }
    if (type == QStringLiteral("VIBE")) {
        const double vibration = std::max({numberValue(message, {"VibeX"}), numberValue(message, {"VibeY"}), numberValue(message, {"VibeZ"})});
        appendSeries(state, QStringLiteral("vibration"), message.timeUs, vibration, 0);
        const double clipping = std::max({numberValue(message, {"Clip0"}), numberValue(message, {"Clip1"}), numberValue(message, {"Clip2"})});
        appendSeries(state, QStringLiteral("clipping"), message.timeUs, clipping, 0);
        return;
    }
    if (type == QStringLiteral("XKF4") || type == QStringLiteral("NKF4")) {
        double maximum = 0;
        for (const char* name : {"SV", "SP", "SH", "SM", "SVT"}) {
            const double value = std::abs(numberValue(message, {name}, 0));
            maximum = std::max(maximum, value);
        }
        appendSeries(state, QStringLiteral("ekf_variance"), message.timeUs, maximum, 0);
        return;
    }
    if (type == QStringLiteral("QPOS")) {
        const int qposState = qRound(numberValue(message, {"State"}, -1));
        const double distance = numberValue(message, {"Dist"}, -1);
        const double targetSpeed = numberValue(message, {"TSpd"}, -1);
        appendSeries(state, QStringLiteral("qpos_distance"), message.timeUs, distance, 0);
        appendSeries(state, QStringLiteral("qpos_speed"), message.timeUs, targetSpeed, 0);
        if (qposState != state.lastQposState) {
            addEvent(state, message.timeUs, QStringLiteral("垂起着陆"), QStringLiteral("垂起状态切换"),
                     QStringLiteral("%1；距目标 %2 m，目标速度 %3 m/s").arg(qposStateName(qposState)).arg(distance, 0, 'f', 1).arg(targetSpeed, 0, 'f', 1));
            state.lastQposState = qposState;
            if (qposState == 3 && report.vtolPosition1Distance < 0) {
                report.vtolPosition1Distance = distance;
                report.vtolPosition1TimeUs = message.timeUs;
            }
            if (qposState == 4 && report.vtolPosition2Distance < 0) {
                report.vtolPosition2Distance = distance;
                report.vtolPosition2Speed = targetSpeed;
                report.vtolPosition2TimeUs = message.timeUs;
            }
        }
        return;
    }
    if (type == QStringLiteral("ERR")) {
        const bool active = numberValue(message, {"ECode"}) != 0;
        if (active) ++report.errorCount;
        addEvent(state, message.timeUs, QStringLiteral("系统错误"), active ? QStringLiteral("飞控错误触发") : QStringLiteral("飞控错误解除"),
                 QStringLiteral("Subsys（子系统）=%1，ECode（错误码）=%2").arg(qRound(numberValue(message, {"Subsys"}))).arg(qRound(numberValue(message, {"ECode"}))), active ? Severity::Warning : Severity::Info);
        return;
    }
    if (type == QStringLiteral("POWR")) {
        appendSeries(state, QStringLiteral("board_voltage"), message.timeUs, numberValue(message, {"Vcc"}, std::numeric_limits<double>::quiet_NaN()), 0);
        return;
    }
}

}

LogParser::LogParser(QObject* parent) : QObject(parent)
{
}

void LogParser::cancel()
{
    _cancelled = true;
}

void LogParser::parse(const QString& filePath)
{
    ParseState state;
    LogReport& report = state.report;
    report.diagnosticOptions = _options;
    report.filePath = filePath;
    report.fileName = QFileInfo(filePath).fileName();
    report.fileSize = QFileInfo(filePath).size();

    ensureSeries(state, QStringLiteral("altitude"), QStringLiteral("相对高度"), QStringLiteral("POS.RelHomeAlt"), QStringLiteral("m"));
    ensureSeries(state, QStringLiteral("groundspeed"), QStringLiteral("地速"), QStringLiteral("GPS.Spd"), QStringLiteral("m/s"));
    ensureSeries(state, QStringLiteral("airspeed"), QStringLiteral("空速"), QStringLiteral("ARSP.Airspeed"), QStringLiteral("m/s"));
    ensureSeries(state, QStringLiteral("roll"), QStringLiteral("横滚角"), QStringLiteral("ATT.Roll"), QStringLiteral("°"));
    ensureSeries(state, QStringLiteral("pitch"), QStringLiteral("俯仰角"), QStringLiteral("ATT.Pitch"), QStringLiteral("°"));
    ensureSeries(state, QStringLiteral("vibration"), QStringLiteral("最大轴向振动"), QStringLiteral("VIBE.VibeX/Y/Z"), QStringLiteral("m/s²"));
    ensureSeries(state, QStringLiteral("clipping"), QStringLiteral("IMU 削波累计"), QStringLiteral("VIBE.Clip0/1/2"), QStringLiteral("次"));
    ensureSeries(state, QStringLiteral("ekf_variance"), QStringLiteral("EKF 最大创新检验比"), QStringLiteral("XKF4/NKF4"), QString());
    ensureSeries(state, QStringLiteral("satellites"), QStringLiteral("卫星数量"), QStringLiteral("GPS.NSats"), QStringLiteral("颗"));
    ensureSeries(state, QStringLiteral("hdop"), QStringLiteral("GPS 水平精度因子"), QStringLiteral("GPS.HDop"), QString());
    ensureSeries(state, QStringLiteral("qpos_distance"), QStringLiteral("垂起着陆目标距离"), QStringLiteral("QPOS.Dist"), QStringLiteral("m"));
    ensureSeries(state, QStringLiteral("qpos_speed"), QStringLiteral("垂起着陆目标速度"), QStringLiteral("QPOS.TSpd"), QStringLiteral("m/s"));
    ensureSeries(state, QStringLiteral("board_voltage"), QStringLiteral("飞控供电电压"), QStringLiteral("POWR.Vcc"), QStringLiteral("V"));

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        report.error = QStringLiteral("无法打开日志文件。请检查文件权限或是否正被其他程序占用。");
        emit finished(report);
        return;
    }
    if (report.fileSize < 128) {
        report.error = QStringLiteral("文件过小，不是有效的 ArduPilot DataFlash 日志。");
        emit finished(report);
        return;
    }
    uchar* bytes = file.map(0, report.fileSize);
    if (!bytes) {
        report.error = QStringLiteral("无法映射日志文件。请确认系统有足够的虚拟内存。");
        emit finished(report);
        return;
    }

    const QSet<QString> detailedMessages = {
        "PARM","MSG","ARM","MODE","POS","GPS","GPS2","GPSB","BAT","ARSP","ATT","VIBE",
        "XKF4","NKF4","QPOS","ERR","POWR"
    };
    qint64 offset = 0;
    int lastProgress = -1;
    while (offset + 3 <= report.fileSize && !_cancelled.load() && !QThread::currentThread()->isInterruptionRequested()) {
        if (bytes[offset] != 0xA3 || bytes[offset + 1] != 0x95) {
            ++report.skippedBytes;
            ++offset;
            continue;
        }
        const int type = bytes[offset + 2];
        if (type == 0x80 && offset + 89 <= report.fileSize) {
            const uchar* payload = bytes + offset + 3;
            FormatDefinition format;
            const int describedType = payload[0];
            format.length = payload[1];
            format.name = asciiString(payload + 2, 4);
            format.format = asciiString(payload + 6, 16);
            format.columns = asciiString(payload + 22, 64).split(',', Qt::KeepEmptyParts);
            if (describedType > 0 && format.length >= 3 && !format.name.isEmpty()) {
                state.formats.insert(describedType, format);
                if (!report.availableMessageTypes.contains(format.name)) report.availableMessageTypes.append(format.name);
            }
            offset += 89;
        } else if (state.formats.contains(type)) {
            const FormatDefinition& format = state.formats[type];
            if (format.length < 3 || offset + format.length > report.fileSize) {
                ++offset;
                continue;
            }
            const DecodedMessage message = decode(bytes + offset + 3, format.length - 3, format);
            ++report.decodedRecords;
            if (message.timeUs > 0) {
                if (!report.logStartUs || message.timeUs < report.logStartUs) report.logStartUs = message.timeUs;
                report.logEndUs = std::max(report.logEndUs, message.timeUs);
            }
            appendRawFields(state, format, message);
            if (detailedMessages.contains(format.name)) processMessage(state, format.name, message);
            offset += format.length;
        } else {
            ++report.skippedBytes;
            ++offset;
        }

        const int currentProgress = static_cast<int>((offset * 86) / report.fileSize);
        if (currentProgress != lastProgress) {
            lastProgress = currentProgress;
            emit progress(currentProgress, QStringLiteral("正在读取 DataFlash 数据"));
        }
    }
    file.unmap(bytes);

    if (_cancelled.load() || QThread::currentThread()->isInterruptionRequested()) {
        report.cancelled = true;
        report.error = QStringLiteral("分析已取消。");
        emit finished(report);
        return;
    }
    if (state.formats.isEmpty() || report.decodedRecords == 0 || report.logEndUs <= report.logStartUs) {
        report.error = QStringLiteral("没有找到 FMT 格式定义。该文件不是有效的 ArduPilot .BIN 日志，或文件已经损坏。");
        emit finished(report);
        return;
    }

    for (const FormatDefinition& format : state.formats) {
        LogMessageSchema schema;
        schema.name = format.name;
        schema.chineseName = messageChineseName(format.name);
        const int fieldCount = std::min(format.columns.size(), format.format.size());
        for (int index = 0; index < fieldCount; ++index) {
            const QChar code = format.format.at(index);
            const QString field = format.columns.at(index).trimmed();
            schema.fields.append({field, fieldChineseName(format.name, field), code, code != 'n' && code != 'N' && code != 'Z' && code != 'a'});
        }
        report.messageSchemas.append(schema);
    }
    // Expose secondary sensor/EKF instances as separate selectable message groups.
    const auto baseSchemas = report.messageSchemas;
    QSet<QString> extraNames;
    for (const Series& series : report.series) {
        if (!series.messageType.contains('[') || extraNames.contains(series.messageType)) continue;
        extraNames.insert(series.messageType);
        const QString base = series.messageType.section('[', 0, 0);
        for (LogMessageSchema schema : baseSchemas) if (schema.name == base) {
            schema.name = series.messageType;
            schema.chineseName += QStringLiteral(" · 实例 %1").arg(series.messageType.section('[', 1).section(']', 0, 0));
            report.messageSchemas.append(schema);
            break;
        }
    }
    for (Series& series : report.series) {
        std::stable_sort(series.points.begin(), series.points.end(), [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
    }
    std::sort(report.messageSchemas.begin(), report.messageSchemas.end(), [](const LogMessageSchema& left, const LogMessageSchema& right) { return left.name < right.name; });

    emit progress(88, QStringLiteral("正在识别机型与飞行阶段"));
    if (report.firmware.isEmpty()) report.firmware = QStringLiteral("未记录固件版本");
    std::sort(report.availableMessageTypes.begin(), report.availableMessageTypes.end());
    detectVehicle(state);
    calculateMetrics(state);

    for (FlightEvent& event : report.events) {
        if (event.category == QStringLiteral("飞行模式")) {
            const QRegularExpression pattern(QStringLiteral("MODE（飞行模式）=(\\d+)"));
            const auto match = pattern.match(event.detail);
            if (match.hasMatch()) event.name = modeName(report.vehicleKind, match.captured(1).toInt());
        }
    }
    std::sort(report.events.begin(), report.events.end(), [](const FlightEvent& left, const FlightEvent& right) { return left.timeUs < right.timeUs; });

    emit progress(94, QStringLiteral("正在执行通用与机型专项诊断"));
    runDiagnostics(state);
    report.valid = true;
    emit progress(100, QStringLiteral("分析完成"));
    emit finished(report);
}
