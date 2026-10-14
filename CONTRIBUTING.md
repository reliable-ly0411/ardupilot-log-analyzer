# 贡献指南

欢迎提交 Issue 和 Pull Request。请在提交前运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\构建.ps1 -Verify
```

新增诊断规则时，请同时提供正常、异常、缺字段或边界样例，并注明单位、门限、时间窗口和适用固件版本。不要在公开 Issue 中上传包含个人信息、坐标、设备序列号或其他敏感数据的真实日志。
