# 飞析 3.0 开发说明

Qt 5.15.2 / MinGW 8.1 / C++17 的 Windows 离线 DataFlash 分析器。

从项目根目录执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\构建.ps1
powershell -ExecutionPolicy Bypass -File .\构建.ps1 -Verify
```

脚本默认使用 `F:\Qt\5.15.2\mingw81_64` 和 `F:\Qt\Tools\mingw810_64`；可通过 `-QtRoot`、`-MinGWRoot` 指定路径。脚本将源码复制到 `build/src`，使用 ASCII 相对参数运行 qmake，并修正旧 windres 对中文资源路径的兼容问题。成功后更新根目录 EXE。

`-Verify` 需要 Python（仅使用标准库）生成固定样例，随后执行 `--self-test 测试 验证/回归验证.json`。测试覆盖解析、规则时段、采样、实例分离、误报边界、取消、窗口联动、CSV、记录恢复和报告输出。截图使用 Windows 平台；Windows 的 Qt offscreen 插件默认没有完整字体渲染，不能据其无文字截图评价产品界面。

核心文件：

- `parser.cpp`：内存映射读取、格式解码、原始数值采样、实例分离、指标与配置提示。
- `diagnostics.cpp`：可追溯实测检查、持续区间与覆盖情况；按日志参数判定电池门限，不猜电芯。
- `guidance.cpp`：课程、证据解释与排查路径。
- `widgets.cpp`：绘图、分轴、屏幕像素范围内的极值绘制、缓存、真实样本游标与原子 CSV。
- `mainwindow.cpp`：工作台、门限配置、引导联动、记录与报告。
- `regression.cpp`：可重复回归；不以合成日志替代真实飞行验证。

绘图只在显示阶段压缩屏幕列内的点，保留每列首末点和极值；诊断、统计与 CSV 使用已解码的原始数值样本。未知单位不补造物理量。

新增规则应附可重复的正常/异常/缺失证据用例，并说明工况、单位、时间窗口和筛查门限。不能将“缺少数据”或“未触发某规则”等同于飞行正常。

前转换规则的状态定义已核对官方 Plane 4.5/4.6；同时要求 Trn、Ast、模式与机型证据，其他版本不猜测。所有符合条件的重复区间和飞控事件均保留。当前回归共 55 项。

官方 ArduPilot DataFlash `BAT.CurrTot` 的浮点原始值为 mAh，解码为 Ah 后才供曲线、CSV 和报告使用；`BAT.EnrgTot` 本身是 Wh，不做该换算。回归样例使用 Plane-4.6.0 的 BAT 字段布局与单位/倍率定义，覆盖两路电池、非零初始值及计数器复位。历史教学样例的合成电量曾以 Ah 写入 BAT 原始字段，原样保留，不再作为电量数值基准；请使用独立的 battery_units.BIN。此变更并不引入通用 FMTU/UNIT/MULT 换算。

从根目录执行 `powershell -ExecutionPolicy Bypass -File .\打包.ps1` 生成运行包；已有成品目录时用 `-Destination` 指定新目录。
