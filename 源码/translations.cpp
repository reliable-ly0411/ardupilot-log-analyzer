#include "translations.h"

#include <QMap>
#include <QStringList>

namespace {

const QMap<QString, QString>& exactParameters()
{
    static const QMap<QString, QString> values = {
        {"AHRS_EKF_TYPE", "姿态航向参考系统使用的 EKF 类型"},
        {"AIRSPEED_CRUISE", "巡航空速"},
        {"AIRSPEED_MAX", "最大空速"},
        {"AIRSPEED_MIN", "最小空速"},
        {"ARMING_CHECK", "解锁前安全检查项目"},
        {"ARSPD_TYPE", "空速传感器类型"},
        {"ARSPD_USE", "空速传感器用途"},
        {"BATT_ARM_VOLT", "允许解锁的最低电池电压"},
        {"BATT_CAPACITY", "电池标称容量"},
        {"BATT_CRT_MAH", "严重低电量剩余容量阈值"},
        {"BATT_CRT_VOLT", "严重低电量电压阈值"},
        {"BATT_FS_CRT_ACT", "严重低电量失效保护动作"},
        {"BATT_FS_LOW_ACT", "低电量失效保护动作"},
        {"BATT_LOW_MAH", "低电量剩余容量阈值"},
        {"BATT_LOW_VOLT", "低电量电压阈值"},
        {"BATT_MONITOR", "电池监测器类型"},
        {"BATT_VOLT_MULT", "电池电压校准系数"},
        {"EK3_ENABLE", "启用 EKF3"},
        {"EK3_SRC1_POSXY", "EKF3 主源水平位置来源"},
        {"EK3_SRC1_POSZ", "EKF3 主源垂直位置来源"},
        {"EK3_SRC1_VELXY", "EKF3 主源水平速度来源"},
        {"EK3_SRC1_VELZ", "EKF3 主源垂直速度来源"},
        {"FENCE_ACTION", "电子围栏触发动作"},
        {"FENCE_ENABLE", "启用电子围栏"},
        {"FRAME_CLASS", "多旋翼机架类别"},
        {"FRAME_TYPE", "多旋翼机架布局"},
        {"FS_GCS_ENABLE", "地面站失联保护"},
        {"FS_LONG_ACTN", "遥控长时间失联动作"},
        {"FS_SHORT_ACTN", "遥控短时间失联动作"},
        {"GPS_TYPE", "主 GPS 接收机类型"},
        {"LAND_SPEED", "降落下降速度"},
        {"LAND_SPEED_HIGH", "高空阶段下降速度"},
        {"LOG_BITMASK", "日志记录项目掩码"},
        {"MOT_BAT_VOLT_MAX", "电机补偿电池最高电压"},
        {"MOT_BAT_VOLT_MIN", "电机补偿电池最低电压"},
        {"MOT_SPIN_ARM", "解锁后的电机最低转速"},
        {"MOT_SPIN_MAX", "电机最大有效输出"},
        {"MOT_SPIN_MIN", "电机开始稳定旋转的输出"},
        {"Q_APPROACH_DIST", "允许固定翼进近的起始距离"},
        {"Q_ASSIST_ANGLE", "大姿态偏差触发旋翼辅助的角度"},
        {"Q_ASSIST_SPEED", "低空速触发旋翼辅助的门限"},
        {"Q_ENABLE", "启用垂直起降固定翼功能"},
        {"Q_FRAME_CLASS", "垂起旋翼机架类别"},
        {"Q_FRAME_TYPE", "垂起旋翼机架布局"},
        {"Q_FW_LND_APR_RAD", "垂起固定翼着陆进近盘旋半径"},
        {"Q_LAND_FINAL_ALT", "进入最终垂直下降的高度"},
        {"Q_OPTIONS", "垂起功能选项位掩码"},
        {"Q_RTL_ALT", "垂起返航高度"},
        {"Q_RTL_MODE", "垂起返航模式选择"},
        {"Q_TAILSIT_ENABLE", "启用尾座式垂起"},
        {"Q_TILT_ENABLE", "启用倾转旋翼"},
        {"Q_TRANS_DECEL", "固定翼转旋翼时采用的减速度"},
        {"Q_TRANS_FAIL", "前转换超时限制"},
        {"Q_TRANS_FAIL_ACT", "前转换失败后的动作"},
        {"Q_TRANSITION_MS", "前转换最短持续时间"},
        {"Q_WP_ACCEL", "旋翼位置控制加速度"},
        {"Q_WP_SPEED", "旋翼位置控制水平速度"},
        {"RTL_ALTITUDE", "返航目标高度"},
        {"RTL_RADIUS", "返航盘旋半径"},
        {"TECS_SINK_MAX", "TECS 最大下降率"},
        {"TECS_SPDWEIGHT", "TECS 速度与高度控制权重"},
        {"TECS_TIME_CONST", "TECS 高度响应时间常数"},
        {"THR_MAX", "最大油门输出"},
        {"THR_MIN", "最小油门输出"},
        {"THR_SLEWRATE", "油门变化速率限制"},
        {"WP_LOITER_RAD", "航点盘旋半径"},
        {"WP_RADIUS", "航点到达判定半径"},
    };
    return values;
}

const QVector<QPair<QString, QString>>& prefixes()
{
    static const QVector<QPair<QString, QString>> values = {
        {"Q_", "垂直起降系统"}, {"BATT", "电池系统"}, {"ARSPD", "空速系统"},
        {"AIRSPEED", "空速控制"}, {"GPS", "卫星定位"}, {"EK", "扩展卡尔曼滤波"},
        {"AHRS", "姿态航向参考"}, {"INS", "惯性传感器"}, {"COMPASS", "罗盘"},
        {"BARO", "气压计"}, {"RNGFND", "测距传感器"}, {"SERVO", "舵机输出"},
        {"RC", "遥控输入"}, {"MOT", "电机控制"}, {"ATC", "姿态控制"},
        {"PSC", "位置控制"}, {"TECS", "固定翼能量控制"}, {"NAVL1", "固定翼航迹控制"},
        {"LAND", "着陆控制"}, {"RTL", "返航控制"}, {"WP", "航点导航"},
        {"FS", "失效保护"}, {"FENCE", "电子围栏"}, {"TERRAIN", "地形跟随"},
        {"LOG", "日志记录"}, {"BRD", "飞控硬件"}, {"SERIAL", "串口"},
        {"CAN", "CAN 总线"}, {"ESC", "电调遥测"}, {"ARMING", "解锁检查"},
        {"FLTMODE", "飞行模式"}, {"THR", "油门控制"}, {"PTCH", "俯仰控制"},
        {"RLL", "横滚控制"}, {"YAW", "偏航控制"}, {"ALT", "高度控制"},
        {"CAM", "相机"}, {"MNT", "云台"}, {"ADSB", "ADS-B 避障"},
        {"OSD", "屏幕显示"}, {"GCS", "地面站链路"}, {"NET", "网络"},
        {"EFI", "发动机电喷"}, {"RPM", "转速传感器"}, {"ICE", "燃油发动机"},
        {"FRAME", "机架配置"}, {"SCHED", "任务调度"}, {"STAT", "飞行统计"}
    };
    return values;
}

QString tokenMeaning(const QString& token)
{
    static const QMap<QString, QString> values = {
        {"ENABLE", "启用开关"}, {"TYPE", "类型"}, {"CLASS", "类别"}, {"FRAME", "机架"},
        {"OPTIONS", "功能选项"}, {"OPTION", "功能选项"}, {"FUNCTION", "输出功能"},
        {"VOLT", "电压"}, {"CURR", "电流"}, {"MAH", "容量"}, {"CAPACITY", "容量"},
        {"LOW", "低阈值"}, {"CRT", "严重阈值"}, {"MAX", "最大值"}, {"MIN", "最小值"},
        {"ALT", "高度"}, {"SPEED", "速度"}, {"SPD", "速度"}, {"ACCEL", "加速度"},
        {"DECEL", "减速度"}, {"RATE", "速率"}, {"TIME", "时间"}, {"TIMEOUT", "超时"},
        {"FAIL", "失败保护"}, {"FS", "失效保护"}, {"ACT", "触发动作"}, {"ACTION", "触发动作"},
        {"ANGLE", "角度"}, {"RAD", "半径"}, {"RADIUS", "半径"}, {"DIST", "距离"},
        {"PWM", "PWM 脉宽"}, {"TRIM", "中位值"}, {"REVERSED", "反向设置"},
        {"P", "比例增益 P"}, {"I", "积分增益 I"}, {"D", "微分增益 D"}, {"FF", "前馈增益"},
        {"FILT", "滤波频率"}, {"FLTT", "目标滤波频率"}, {"FLTE", "误差滤波频率"},
        {"THR", "油门"}, {"PITCH", "俯仰"}, {"ROLL", "横滚"}, {"YAW", "偏航"},
        {"TEMP", "温度"}, {"MONITOR", "监测器"}, {"COUNT", "数量"}, {"MASK", "位掩码"},
        {"PRIMARY", "主传感器"}, {"USE", "使用方式"}, {"AUTO", "自动控制"},
        {"RTL", "返航"}, {"LAND", "着陆"}, {"CRUISE", "巡航"}, {"TAKEOFF", "起飞"},
        {"TRANS", "转换"}, {"ASSIST", "辅助"}, {"WP", "航点"}, {"LOITER", "盘旋"}
    };
    return values.value(token);
}

}

QString parameterCategory(const QString& name)
{
    for (const auto& item : prefixes()) {
        if (name.startsWith(item.first)) return item.second;
    }
    return QStringLiteral("其他系统");
}

QString parameterChineseName(const QString& name)
{
    const auto exact = exactParameters().constFind(name);
    if (exact != exactParameters().constEnd()) return exact.value();

    QStringList meanings;
    const QString category = parameterCategory(name);
    for (const QString& token : name.split('_', Qt::SkipEmptyParts)) {
        const QString meaning = tokenMeaning(token);
        if (!meaning.isEmpty() && meaning != category && !meanings.contains(meaning)) meanings.append(meaning);
    }
    if (meanings.isEmpty()) return category + QStringLiteral("参数");
    return category + QStringLiteral(" · ") + meanings.join(QStringLiteral(" · "));
}

QString messageChineseName(const QString& name)
{
    static const QMap<QString, QString> names = {
        {"ACC", "加速度计"}, {"ACC2", "第二加速度计"}, {"ACC3", "第三加速度计"},
        {"ADSB", "ADS-B 航空器目标"}, {"AHR2", "备用姿态航向"}, {"AIS", "船舶自动识别目标"},
        {"ARM", "解锁状态"}, {"ARSP", "空速传感器"}, {"ATT", "姿态"}, {"ATUN", "自动调参"},
        {"BARO", "气压计"}, {"BAT", "电池状态"}, {"BCL", "电芯电压"}, {"BCN", "信标定位"},
        {"CAM", "相机触发"}, {"CAN", "CAN 总线"}, {"CAND", "CAN 数据帧"}, {"CMD", "任务指令"},
        {"CTUN", "控制调节"}, {"DSF", "数据闪存状态"}, {"EFI", "发动机电喷"},
        {"ERR", "内部错误"}, {"ESC", "电调遥测"}, {"EV", "飞行事件"},
        {"FILE", "文件传输"}, {"FMT", "消息格式定义"}, {"FMTU", "消息单位定义"},
        {"FLOW", "光流"}, {"GPA", "GPS 精度"}, {"GPS", "卫星定位"}, {"GPS2", "第二卫星定位"},
        {"GYR", "陀螺仪"}, {"GYR2", "第二陀螺仪"}, {"GYR3", "第三陀螺仪"},
        {"HOME", "返航点"}, {"IMU", "惯性测量单元"}, {"IMU2", "第二惯性测量单元"}, {"IMU3", "第三惯性测量单元"},
        {"MAG", "磁力计"}, {"MAG2", "第二磁力计"}, {"MAG3", "第三磁力计"},
        {"MAV", "MAVLink 链路统计"}, {"MODE", "飞行模式"}, {"MOTB", "电机控制器状态"},
        {"MNT", "云台状态"}, {"MSG", "飞控文本消息"}, {"MULT", "数值倍率定义"},
        {"NKF1", "EKF2 状态"}, {"NKF2", "EKF2 偏置"}, {"NKF3", "EKF2 创新"},
        {"NKF4", "EKF2 创新检验比"}, {"NKF5", "EKF2 导航状态"}, {"NTUN", "导航调节"},
        {"ORGN", "坐标原点"}, {"PARM", "飞控参数"}, {"PIDR", "横滚 PID"},
        {"PIDP", "俯仰 PID"}, {"PIDY", "偏航 PID"}, {"PIDA", "高度 PID"},
        {"PM", "系统性能"}, {"POS", "融合位置"}, {"POWR", "飞控电源"}, {"PRX", "接近传感器"},
        {"QPOS", "垂起位置控制"}, {"QTUN", "垂起控制调节"}, {"RATE", "角速度控制"},
        {"RCIN", "遥控输入"}, {"RCOU", "舵机/电机输出"}, {"RFND", "测距传感器"},
        {"RPM", "转速"}, {"SRTL", "智能返航路径"}, {"STAT", "飞行统计"},
        {"TECS", "固定翼能量控制"}, {"TERR", "地形数据"}, {"TRIG", "相机触发状态"},
        {"UNIT", "单位定义"}, {"VIBE", "机体振动"},
        {"XKF1", "EKF3 状态"}, {"XKF2", "EKF3 偏置"}, {"XKF3", "EKF3 创新"},
        {"XKF4", "EKF3 创新检验比"}, {"XKF5", "EKF3 导航状态"},

        {"AETR", "固定翼舵面与油门输出"}, {"AIS1", "AIS 船舶动态报告"},
        {"AIS4", "AIS 基站报告"}, {"AIS5", "AIS 船舶静态信息"}, {"AISR", "AIS 接收数据"},
        {"ANG", "角度控制状态"}, {"AOA", "迎角与侧滑角"},
        {"ATRP", "自动调参控制参数"}, {"ATSC", "姿态控制缩放"},
        {"BARD", "气压计动态压力"}, {"CAFD", "CAN 文件数据"},
        {"CANF", "CAN 数据帧"}, {"CANS", "CAN 总线统计"},
        {"CSRV", "自定义舵机输出"}, {"CTRL", "飞控控制状态"},
        {"DCM", "方向余弦矩阵姿态"}, {"DMS", "数字消息服务"},
        {"DSTL", "深度与着陆状态"}, {"EAHR", "外部姿态航向系统"},
        {"EDT2", "电调遥测扩展数据"}, {"FNCE", "电子围栏状态"},
        {"GPYW", "GPS 航向"}, {"GRAW", "GNSS 原始观测数据"},
        {"GRXH", "GNSS 接收机报文头"}, {"GRXS", "GNSS 接收机状态"},
        {"IOMC", "IO 协处理器状态"}, {"ISBD", "卫星通信数据"},
        {"ISBH", "卫星通信报文头"}, {"LAND", "着陆控制状态"},
        {"MAVC", "MAVLink 命令"}, {"MCU", "主控芯片状态"},
        {"MISE", "任务执行状态"}, {"MON", "系统监视器"},
        {"NKY0", "EKF2 偏航估计"}, {"NKY1", "EKF2 偏航估计状态"},
        {"OABR", "避障弯曲路径"}, {"OADJ", "避障目标调整"},
        {"OAVG", "避障速度规划"}, {"OF", "光流数据"}, {"OFG", "光流陀螺补偿"},
        {"PIQA", "加速度控制器状态"}, {"PIQP", "俯仰控制器状态"},
        {"PIQR", "横滚控制器状态"}, {"PIQY", "偏航控制器状态"},
        {"PL", "有效载荷状态"}, {"PRXR", "接近传感器距离"},
        {"PSCD", "位置控制下降状态"}, {"PSCE", "位置控制东向状态"},
        {"PSCN", "位置控制北向状态"}, {"PSOD", "位置控制下降输出"},
        {"PSOE", "位置控制东向输出"}, {"PSON", "位置控制北向输出"},
        {"PSOT", "位置控制器综合状态"}, {"RAD", "无线电链路状态"},
        {"RALY", "备降点"}, {"RASH", "空速传感器回放头"},
        {"RASI", "空速传感器回放数据"}, {"RBCH", "信标回放头"},
        {"RBCI", "信标回放数据"}, {"RBOH", "气压计回放头"},
        {"RBRH", "气压计原始回放头"}, {"RBRI", "气压计原始回放数据"},
        {"RCI2", "遥控输入扩展"}, {"RCO2", "舵机输出通道 15 至 28"},
        {"RCO3", "舵机输出通道 29 至 32"}, {"REPH", "测距仪回放头"},
        {"REV2", "事件扩展数据"}, {"REV3", "事件扩展状态"}, {"REVH", "事件回放头"},
        {"REY3", "EKF3 回放事件"}, {"RFRF", "测距仪回放数据"},
        {"RFRH", "测距仪回放头"}, {"RFRN", "测距仪回放导航数据"},
        {"RGPH", "GPS 回放头"}, {"RGPI", "GPS 回放数据"}, {"RGPJ", "GPS 精度回放数据"},
        {"RISH", "惯导回放头"}, {"RISI", "惯导回放数据"},
        {"RMGH", "磁力计回放头"}, {"RMGI", "磁力计回放数据"},
        {"ROFH", "光流回放头"}, {"RRNH", "测距导航回放头"}, {"RRNI", "测距导航回放数据"},
        {"RSLL", "仿真位置回放"}, {"RSO2", "舵机输出回放扩展"},
        {"RSO3", "舵机输出回放附加数据"}, {"RSSI", "遥控信号强度"},
        {"RVOH", "视觉里程计回放头"}, {"RWA2", "轮速回放扩展"},
        {"RWA3", "轮速回放附加数据"}, {"RWOH", "轮速里程计回放头"},
        {"SA", "传感器辅助状态"}, {"SBPH", "Septentrio 接收机报文头"},
        {"SBRE", "Septentrio 接收机事件"}, {"SBRH", "Septentrio 接收机状态"},
        {"SBRM", "Septentrio 接收机测量数据"}, {"SCR", "Lua 脚本状态"},
        {"SIM", "仿真器状态"}, {"STAK", "任务栈使用情况"},
        {"TEC2", "TECS 扩展状态"}, {"TEC3", "TECS 能量状态"},
        {"TILT", "倾转旋翼状态"}, {"TSIT", "尾座式垂起状态"}, {"TSYN", "时间同步状态"},
        {"UART", "串口运行统计"}, {"UBX1", "u-blox 导航状态"}, {"UBX2", "u-blox 干扰监测"},
        {"VER", "固件版本信息"}, {"VISO", "视觉里程计状态"},
        {"VISP", "外部视觉位置"}, {"VISV", "外部视觉速度"},
        {"VSTB", "垂起姿态稳定状态"}, {"WDOG", "看门狗复位信息"},
        {"WENC", "轮式编码器"}, {"WINC", "轮速输入"},
        {"XKQ", "EKF3 姿态四元数"}, {"XKT", "EKF3 时间同步"},
        {"XKTV", "EKF3 测试比"}, {"XKV1", "EKF3 速度创新"},
        {"XKV2", "EKF3 速度创新方差"}, {"XKY0", "EKF3 偏航估计"},
        {"XKY1", "EKF3 偏航估计状态"}
    };
    const auto exact = names.constFind(name);
    if (exact != names.constEnd()) return exact.value();
    if (name.startsWith(QStringLiteral("ESC"))) return QStringLiteral("电调遥测");
    if (name.startsWith(QStringLiteral("BAT"))) return QStringLiteral("电池状态");
    if (name.startsWith(QStringLiteral("GPS"))) return QStringLiteral("卫星定位");
    if (name.startsWith(QStringLiteral("XKF"))) return QStringLiteral("EKF3 数据");
    if (name.startsWith(QStringLiteral("NKF"))) return QStringLiteral("EKF2 数据");
    if (name.startsWith(QStringLiteral("PID"))) return QStringLiteral("PID 控制数据");
    return QStringLiteral("日志数据");
}

QString fieldChineseName(const QString& message, const QString& field)
{
    if (message.startsWith(QStringLiteral("PID"))) {
        static const QMap<QString, QString> pid = {
            {"Tar","目标值"},{"Act","实际值"},{"Err","控制误差"},{"P","比例项输出"},
            {"I","积分项输出"},{"D","微分项输出"},{"FF","前馈项输出"},{"DFF","微分前馈输出"},
            {"Dmod","微分抑制系数"},{"SRate","输出变化速率"},{"Flags","控制器状态标志"}
        };
        if (pid.contains(field)) return pid.value(field);
    }
    if (message == QStringLiteral("RATE")) {
        static const QMap<QString, QString> rate = {
            {"RDes","目标横滚角速度"},{"R","实际横滚角速度"},{"ROut","横滚控制输出"},
            {"PDes","目标俯仰角速度"},{"P","实际俯仰角速度"},{"POut","俯仰控制输出"},
            {"YDes","目标偏航角速度"},{"Y","实际偏航角速度"},{"YOut","偏航控制输出"},
            {"ADes","目标垂直加速度"},{"A","实际垂直加速度"},{"AOut","垂直加速度控制输出"},{"AOutSlew","垂直输出变化速率"}
        };
        if (rate.contains(field)) return rate.value(field);
    }
    if (message == QStringLiteral("TECS")) {
        static const QMap<QString, QString> tecs = {
            {"h","估算高度"},{"hdem","目标高度"},{"dh","估算升降率"},{"dhdem","目标升降率"},
            {"sp","估算空速"},{"spdem","目标空速"},{"dsp","估算空速变化率"},{"dspdem","目标空速变化率"},
            {"th","油门需求"},{"ph","俯仰需求"},{"hin","高度控制积分项"},{"f","飞行阶段"},
            {"pmin","最小俯仰限制"},{"pmax","最大俯仰限制"}
        };
        if (tecs.contains(field)) return tecs.value(field);
    }
    if (message == QStringLiteral("NTUN")) {
        static const QMap<QString, QString> navigation = {
            {"Dist","航点距离"},{"TBrg","目标方位"},{"NavBrg","导航方位"},{"AltE","高度误差"},
            {"XT","横向航迹误差"},{"XTi","横向误差积分"},{"AsE","空速误差"},
            {"TLat","目标纬度"},{"TLng","目标经度"},{"TAW","目标高度（航点）"},
            {"TAT","目标高度（TECS）"},{"TAsp","目标空速"}
        };
        if (navigation.contains(field)) return navigation.value(field);
    }
    if (message == QStringLiteral("QTUN")) {
        static const QMap<QString, QString> quad = {
            {"ThI","油门输入"},{"ABst","空速辅助状态"},{"ThO","油门输出"},{"ThH","悬停油门"},
            {"DAlt","目标高度"},{"Alt","实际高度"},{"BAlt","气压高度"},{"DCRt","目标爬升率"},
            {"CRt","实际爬升率"},{"TMix","油门混控"},{"Trn","转换状态"},{"Ast","姿态辅助状态"}
        };
        if (quad.contains(field)) return quad.value(field);
    }
    if (message == QStringLiteral("BAT") && field == QStringLiteral("SH")) return QStringLiteral("电池健康百分比");
    if (message == QStringLiteral("VIBE") && field == QStringLiteral("Clip")) return QStringLiteral("IMU 削波累计");
    if (message == QStringLiteral("VIBE") && field == QStringLiteral("IMU")) return QStringLiteral("IMU 实例");
    if (message == QStringLiteral("RCIN") && field.startsWith(QLatin1Char('C')) && field.mid(1).toInt() > 0) return QStringLiteral("遥控输入通道 %1").arg(field.mid(1));
    if ((message == QStringLiteral("RCOU") || message.startsWith(QStringLiteral("RCO"))) && field.startsWith(QLatin1Char('C')) && field.mid(1).toInt() > 0) return QStringLiteral("舵机/电机输出通道 %1").arg(field.mid(1));
    static const QMap<QString, QString> names = {
        {"TimeUS", "系统启动后的时间"}, {"Lat", "纬度"}, {"Lng", "经度"},
        {"Alt", "高度"}, {"RelHomeAlt", "相对返航点高度"}, {"RelOriginAlt", "相对 EKF 原点高度"},
        {"Spd", "地速"}, {"GSpd", "地速"}, {"GCrs", "地面航向"}, {"NSats", "卫星数量"}, {"HDop", "水平定位精度因子"},
        {"Status", "定位状态"}, {"GWk", "GPS 周数"}, {"GMS", "GPS 周内毫秒"}, {"VZ", "垂直速度"},
        {"Volt", "电压"}, {"Curr", "电流"}, {"CurrTot", "累计耗电量"}, {"EnrgTot", "累计能耗"},
        {"VoltR", "静置估算电压"}, {"RemPct", "剩余电量百分比"}, {"Temp", "温度"}, {"Res", "内阻"},
        {"VibeX", "X 轴振动"}, {"VibeY", "Y 轴振动"}, {"VibeZ", "Z 轴振动"},
        {"Clip0", "第一 IMU 削波累计"}, {"Clip1", "第二 IMU 削波累计"}, {"Clip2", "第三 IMU 削波累计"},
        {"Roll", "横滚角"}, {"Pitch", "俯仰角"}, {"Yaw", "偏航角"},
        {"DesRoll", "目标横滚角"}, {"DesPitch", "目标俯仰角"}, {"DesYaw", "目标偏航角"},
        {"Airspeed", "空速"}, {"DiffPress", "差压"}, {"RawPress", "原始差压"}, {"Offset", "零点偏移"},
        {"State", "状态"}, {"Dist", "距离"}, {"TSpd", "目标速度"}, {"TAcc", "目标加速度"}, {"OShoot", "是否冲过目标"},
        {"Mode", "模式编号"}, {"ModeNum", "模式编号别名"}, {"Rsn", "切换原因"}, {"ArmState", "解锁状态"},
        {"Name", "名称"}, {"Value", "当前值"}, {"Default", "默认值"}, {"Message", "文本消息"},
        {"C1", "输出通道 1"}, {"C2", "输出通道 2"}, {"C3", "输出通道 3"}, {"C4", "输出通道 4"},
        {"C5", "输出通道 5"}, {"C6", "输出通道 6"}, {"C7", "输出通道 7"}, {"C8", "输出通道 8"},
        {"AccX", "X 轴加速度"}, {"AccY", "Y 轴加速度"}, {"AccZ", "Z 轴加速度"},
        {"GyrX", "X 轴角速度"}, {"GyrY", "Y 轴角速度"}, {"GyrZ", "Z 轴角速度"},
        {"MagX", "X 轴磁场"}, {"MagY", "Y 轴磁场"}, {"MagZ", "Z 轴磁场"},
        {"Press", "气压"}, {"Vcc", "飞控供电电压"}, {"VServo", "舵机供电电压"},
        {"Subsys", "错误子系统"}, {"ECode", "错误码"}, {"Id", "事件编号"}, {"Inst", "实例编号"},
        {"SV", "速度创新检验比"}, {"SP", "位置创新检验比"}, {"SH", "高度创新检验比"},
        {"SM", "磁场创新检验比"}, {"SVT", "空速创新检验比"}, {"Flags", "状态标志位"},
        {"I", "传感器实例"}, {"SampleUS", "采样时间"}, {"US", "时间戳"},
        {"Dt", "采样周期"}, {"Health", "健康状态"}, {"H", "健康状态"},
        {"Pri", "主传感器标志"}, {"Heading", "航向角"},
        {"Hor_vel", "水平速度"}, {"Ver_vel", "垂直速度"},
        {"ICAO_address", "ICAO 航空器地址"}, {"Squark", "应答机代码"},
        {"Ail", "副翼输出"}, {"Elev", "升降舵输出"}, {"Thr", "油门输出"},
        {"Rudd", "方向舵输出"}, {"Flap", "襟翼输出"}, {"Steer", "转向输出"},
        {"Q1", "四元数分量 1"}, {"Q2", "四元数分量 2"},
        {"Q3", "四元数分量 3"}, {"Q4", "四元数分量 4"},
        {"AOA", "迎角"}, {"SSA", "侧滑角"}, {"ArmChecks", "解锁检查结果"},
        {"Forced", "强制操作标志"}, {"Method", "操作方式"},
        {"Instance", "实例编号"}, {"Target", "目标值"}, {"Actual", "实际值"},
        {"Error", "误差"}, {"Output", "控制输出"}, {"Active", "启用状态"},
        {"Quality", "数据质量"}, {"Count", "数量"}, {"Total", "累计值"},
        {"Version", "版本号"}, {"Reset", "复位状态"}, {"Reason", "原因码"}
    };
    const auto exact = names.constFind(field);
    if (exact != names.constEnd()) return exact.value();
    if (field.startsWith(QLatin1Char('C')) && field.mid(1).toInt() > 0) return QStringLiteral("输入/输出通道 %1").arg(field.mid(1));
    if (field.startsWith(QStringLiteral("Des"))) return QStringLiteral("目标值");
    if (field.endsWith(QStringLiteral("Err"), Qt::CaseInsensitive)) return QStringLiteral("控制误差");
    if (field.endsWith(QStringLiteral("Rate"), Qt::CaseInsensitive)) return QStringLiteral("变化速率");
    return messageChineseName(message) + QStringLiteral("相关数据");
}

QString modeName(VehicleKind kind, int mode)
{
    static const QMap<int, QString> plane = {
        {0,"MANUAL 手动"},{1,"CIRCLE 盘旋"},{2,"STABILIZE 自稳"},{3,"TRAINING 训练"},
        {4,"ACRO 特技"},{5,"FBWA 增稳"},{6,"FBWB 增稳定高"},{7,"CRUISE 巡航"},
        {8,"AUTOTUNE 自动调参"},{10,"AUTO 自动任务"},{11,"RTL 返航"},{12,"LOITER 定点盘旋"},
        {13,"TAKEOFF 起飞"},{14,"AVOID_ADSB ADS-B避障"},{15,"GUIDED 引导"},{17,"QSTABILIZE 垂起自稳"},
        {18,"QHOVER 垂起悬停"},{19,"QLOITER 垂起定点"},{20,"QLAND 垂起降落"},
        {21,"QRTL 垂起返航"},{22,"QAUTOTUNE 垂起自动调参"},{23,"QACRO 垂起特技"},{24,"THERMAL 热气流盘旋"}
    };
    static const QMap<int, QString> copter = {
        {0,"STABILIZE 自稳"},{1,"ACRO 特技"},{2,"ALT_HOLD 定高"},{3,"AUTO 自动任务"},
        {4,"GUIDED 引导"},{5,"LOITER 定点"},{6,"RTL 返航"},{7,"CIRCLE 绕圈"},
        {9,"LAND 降落"},{11,"DRIFT 漂移"},{13,"SPORT 运动"},{14,"FLIP 翻滚"},
        {15,"AUTOTUNE 自动调参"},{16,"POSHOLD 定点保持"},{17,"BRAKE 刹车"},{18,"THROW 抛飞"},
        {19,"AVOID_ADSB ADS-B避障"},{20,"GUIDED_NOGPS 无GPS引导"},{21,"SMART_RTL 智能返航"},
        {22,"FLOWHOLD 光流保持"},{23,"FOLLOW 跟随"},{24,"ZIGZAG 折线"},{25,"SYSTEMID 系统辨识"},
        {26,"AUTOROTATE 自转着陆"},{27,"AUTO_RTL 自动返航"}
    };
    const QMap<int, QString>& map = (kind == VehicleKind::Copter) ? copter : plane;
    return map.value(mode, QStringLiteral("模式 %1").arg(mode));
}

QString qposStateName(int state)
{
    static const QMap<int, QString> names = {
        {0,"NONE 未进入垂起位置控制"},{1,"APPROACH 固定翼进近"},{2,"AIRBRAKE 空中减速"},
        {3,"POSITION1 第一位置控制阶段"},{4,"POSITION2 第二位置控制阶段"},
        {5,"LAND_DESCEND 垂直下降"},{6,"LAND_ABORT 中止着陆"},{7,"LAND_FINAL 最终下降"},
        {8,"LAND_COMPLETE 着陆完成"}
    };
    return names.value(state, QStringLiteral("未知状态 %1").arg(state));
}

QString formatDuration(qint64 usec)
{
    if (usec <= 0) return QStringLiteral("--");
    const qint64 total = usec / 1000000;
    const qint64 hours = total / 3600;
    const qint64 minutes = (total % 3600) / 60;
    const qint64 seconds = total % 60;
    return hours > 0
        ? QStringLiteral("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QLatin1Char('0')).arg(seconds, 2, 10, QLatin1Char('0'))
        : QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
}

QString formatClock(qint64 timeUs, qint64 originUs, bool milliseconds)
{
    if (timeUs < 0 || originUs < 0) return QStringLiteral("--");
    const qint64 difference=timeUs-originUs;
    const qint64 absolute=qAbs(difference),seconds=absolute/1000000;
    const qint64 hours=seconds/3600,minutes=(seconds%3600)/60;
    QString time=hours>0?QStringLiteral("%1:%2:%3").arg(hours).arg(minutes,2,10,QLatin1Char('0')).arg(seconds%60,2,10,QLatin1Char('0')):QStringLiteral("%1:%2").arg(minutes).arg(seconds%60,2,10,QLatin1Char('0'));
    if(milliseconds)time+=QStringLiteral(".%1").arg((absolute%1000000)/1000,3,10,QLatin1Char('0'));
    return (difference<0?QStringLiteral("T-"):QStringLiteral("T+"))+time;
}

QString formatFileSize(qint64 bytes)
{
    if (bytes >= 1024LL * 1024 * 1024) return QStringLiteral("%1 GB").arg(bytes / double(1024LL * 1024 * 1024), 0, 'f', 2);
    if (bytes >= 1024LL * 1024) return QStringLiteral("%1 MB").arg(bytes / double(1024LL * 1024), 0, 'f', 1);
    return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
}
