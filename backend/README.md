# 后端服务（FastAPI + SQLite + MQTT）

承接S3遥测、维护在线状态与指令回执，调度两路定时泵，下载CAM照片并调用DeepSeek分析，向网页提供API。自动化按单实例、单worker运行。

## 启动

从项目根目录运行 `start_dev.bat` 可一并启动broker、后端和前端。手动启动：

```powershell
# backend目录；使用独立.venv，不要使用ESP-IDF虚拟环境
.venv/Scripts/python.exe -m pip install -r requirements.txt -r requirements-dev.txt
# 首次准备.env并填写需要的配置
Copy-Item .env.example .env
.venv/Scripts/python.exe scripts/init_db.py
.venv/Scripts/python.exe -m uvicorn app.main:app --host 127.0.0.1 --port 8000
```

接口文档：[http://127.0.0.1:8000/docs](http://127.0.0.1:8000/docs)。Key仅存在`.env`，不提供给前端或设备。

## 结构与职责

| 目录 | 内容 |
|---|---|
| app/api | 请求、响应与字段校验 |
| app/schemas | 遥测、指令、设备与分析的数据契约 |
| app/services | 指令生命周期、自动调度、遥测存储、照片与AI分析 |
| app/db | ORM模型、Session、启动建表与兼容迁移 |
| app/mqtt | 收发、重连、消息处理函数注册 |
| app/tasks | 每秒定时泵检查、定时照片分析、离线与保留期清理 |
| scripts | 初始化数据库、开发broker、遥测模拟 |
| tests | 隔离数据库、模拟时钟/MQTT/HTTP的回归测试 |

`control_service.py`保留早期阈值建议示例，未接入实际执行；当前规则在`automation_service.py`与S3本地`plant_control`。

## 当前规则

- 补水/施肥为定时清水演示，双重互斥，离线/忙时跳过不补发；同时到期轮换优先，轮次持久保存。
- 补光由S3本地阈值和持续时间控制，后端同步模式与记录。
- 连续两次有效定时照片明确虫害且有依据时触发2秒喷洒，后端保存48小时冷却；手动照片不触发。
- 抓拍与分析已实现，默认每5分钟一次，照片与结果保留7天。`CameraFrame`是旧元数据记录，当前照片保存在`VisionRecord`对应文件中。
- `/ws`目前只有hello、ping/pong和echo；业务数据仍通过HTTP轮询。

## 遥测来源与旧库

每条`sensor_reading`保存`data_source`：设备`device`、模拟`simulated`、来源未记录`unknown`。模拟HTTP接口强制标记；模拟脚本也携带字段。旧固件缺省按device保存。

`init_db()`会为旧表增加来源列，旧记录保留unknown，不删除、不猜测来源。升级后重启后端，网页刷新即可显示角标；模拟与实物并用同一设备编号仍会交替刷新，实物调试请停止模拟脚本。

## 测试

```powershell
.venv/Scripts/python.exe -m pytest tests -q
```

`tests/conftest.py`在导入应用前禁用MQTT，指定独立client_id、临时数据库和图片目录，禁用付费AI与定时拍照。测试不会向实物发布指令。

接口详情见[HTTP接口](../docs/api.md)、[MQTT契约](../docs/mqtt_topics.md)、[自动控制](../docs/automation_control.md)。
