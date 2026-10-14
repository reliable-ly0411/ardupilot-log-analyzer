#pragma once

#include <QMap>
#include <QMetaType>
#include <QPointF>
#include <QString>
#include <QVector>

enum class VehicleKind {
    Unknown,
    FixedWing,
    QuadPlane,
    Copter,
    Rover,
    Boat,
    Sub,
    Tracker,
    Blimp
};

enum class Severity {
    Info = 0,
    Notice = 1,
    Warning = 2,
    Critical = 3
};

struct Finding {
    Severity severity = Severity::Info;
    QString category;
    QString title;
    QString detail;
    QString evidence;
    qint64 timeUs = 0;
    qint64 endTimeUs = 0;
    QString source;
};

struct FlightEvent {
    qint64 timeUs = 0;
    QString category;
    QString name;
    QString detail;
    Severity severity = Severity::Info;
};

struct GeoSample {
    qint64 timeUs = 0;
    double latitude = 0;
    double longitude = 0;
    double relativeAltitude = 0;
};

struct Series {
    QString key;
    QString title;
    QString english;
    QString unit;
    QVector<QPointF> points;
    QString messageType;
    QString fieldName;
};

struct LogFieldSchema {
    QString name;
    QString chineseName;
    QChar formatCode;
    bool numeric = false;
};

struct LogMessageSchema {
    QString name;
    QString chineseName;
    QVector<LogFieldSchema> fields;
};

struct ParameterValue {
    double value = 0;
    double defaultValue = 0;
    bool hasDefault = false;
};

struct BatterySummary {
    int instance = 0;
    double minVoltage = 1e100;
    double maxCurrent = 0;
    double consumedAh = 0;
    double consumedWh = 0;
    double minRemainingPercent = 101;
    double maxTemperature = -1e100;
    bool healthy = true;
    int samples = 0;
};

struct RuleCoverage {
    QString title;
    bool available = false;
    QString detail;
};

struct DiagnosticOptions {
    double vibration = 60.0;
    double ekfRatio = 1.0;
    double rollError = 15.0;
    double pitchError = 10.0;
    double trackingDuration = 1.0;
    double motorDuration = 3.0;
};

struct LogReport {
    bool valid = false;
    bool cancelled = false;
    bool demo = false;
    qint64 decodedRecords = 0;
    qint64 skippedBytes = 0;
    DiagnosticOptions diagnosticOptions;
    QVector<RuleCoverage> coverage;
    QString error;
    QString filePath;
    QString fileName;
    qint64 fileSize = 0;
    QString firmware;
    QString board;
    VehicleKind vehicleKind = VehicleKind::Unknown;
    QString vehicleName;
    QString layoutName;
    QString confidence;

    qint64 logStartUs = 0;
    qint64 logEndUs = 0;
    qint64 flightStartUs = 0;
    qint64 flightEndUs = 0;
    bool flightWindowInferred = false;

    double distanceMeters = 0;
    double maxRelativeAltitude = -1e100;
    double minRelativeAltitude = 1e100;
    double maxGroundSpeed = 0;
    double maxAirSpeed = 0;
    double maxRoll = 0;
    double maxPitch = 0;
    double maxVibration = 0;
    double maxEkfVariance = 0;
    double maxHdop = 0;
    int minSatellites = 999;
    quint64 maxClipCount = 0;
    int errorCount = 0;
    int failsafeCount = 0;

    double vtolPosition1Distance = -1;
    double vtolPosition2Distance = -1;
    double vtolPosition2Speed = -1;
    qint64 vtolPosition1TimeUs = 0;
    qint64 vtolPosition2TimeUs = 0;

    QMap<QString, ParameterValue> parameters;
    QVector<BatterySummary> batteries;
    QVector<GeoSample> route;
    QVector<Series> series;
    QVector<LogMessageSchema> messageSchemas;
    QVector<FlightEvent> events;
    QVector<Finding> findings;
    QStringList messages;
    QStringList availableMessageTypes;
};

inline QString severityName(Severity severity)
{
    switch (severity) {
    case Severity::Critical: return QStringLiteral("严重");
    case Severity::Warning: return QStringLiteral("警告");
    case Severity::Notice: return QStringLiteral("关注");
    default: return QStringLiteral("正常");
    }
}

inline QString vehicleKindName(VehicleKind kind)
{
    switch (kind) {
    case VehicleKind::FixedWing: return QStringLiteral("固定翼");
    case VehicleKind::QuadPlane: return QStringLiteral("垂直起降固定翼");
    case VehicleKind::Copter: return QStringLiteral("多旋翼/直升机");
    case VehicleKind::Rover: return QStringLiteral("无人车");
    case VehicleKind::Boat: return QStringLiteral("无人船");
    case VehicleKind::Sub: return QStringLiteral("潜航器");
    case VehicleKind::Tracker: return QStringLiteral("天线跟踪器");
    case VehicleKind::Blimp: return QStringLiteral("飞艇");
    default: return QStringLiteral("未知类型");
    }
}

Q_DECLARE_METATYPE(LogReport)
