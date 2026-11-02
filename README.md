# 基于大模型的智能植物养护系统

ESP32-S3 采集环境数据并执行泵灯控制，ESP32-CAM 提供植物照片；后端保存数据、处理指令回执和视觉分析，网页显示读数、控制模式与分析结果。

## 项目结构

```text
edge/esp32s3_plant_edge/   ESP-IDF：传感器、四路PWM、本地补光、泵限时与互锁
edge/esp32cam_vision_edge/ ESP-IDF：OV2640高清抓拍、HTTP照片、MQTT服务发现
backend/                  FastAPI + SQLite + MQTT + 后台定时任务
frontend/                 Vue 3 + Vite：总览、历史、控制、植物照片与AI建议
docs/                     架构、接口、接线、自动控制与分区说明
scripts/                  Windows开发环境启停与网络配置
```

S3 与后端通过 MQTT 通信，照片通过 HTTP 下载，网页业务状态通过 HTTP 轮询更新。WebSocket 目前提供连接与 ping/pong，不推送业务数据。

## 当前功能与实测范围（2026-11-02）

| 项目 | 当前状态 |
|---|---|
| 实物电源与补光灯 | 用户确认12V输入、电源电路正在使用；补光灯已实测 |
| 三路泵 | 已接线并实现运行逻辑，尚未实测运行；后续先做短时清水测试 |
| 遥测 | 上传光照、土壤相对湿度；盐分原始值与DS18B20温度只在本地读取，EC与温度上传为空 |
| 手动控制 | 四路开启/关闭、指令ID与回执、单次泵最长30秒、补水与施肥互斥 |
| 自动补光 | S3本地：低于100 lux持续10秒开，高于300 lux持续30秒关；断网继续本地判断 |
| 自动补水/施肥 | 定时清水演示；必填间隔和时长，保存后暂停，主动恢复后开始计时 |
| 定时泵争用 | 同时到期时优先上次定时提交的另一泵；轮次持久保存，忙/离线时跳过，不补发 |
| 视觉 | CAM提供1600×1200 JPEG；后端默认每5分钟抓拍分析，照片与结果保留7天 |
| 自动喷洒 | 连续两次有效定时分析明确虫害且有依据时下发2秒；后端保守冷却48小时 |
| 数据来源 | 总览数值与历史记录区分“设备”“模拟”“来源未记录” |

相对湿度不是绝对含水率，EC不等于肥力；当前补水/施肥不依据AI、湿度或EC决定动作。没有流量、电流和水位反馈，驱动回执表示输出设置结果。板端已有限时、互锁和断网停止；尚未实现喷洒累计上限或板端48小时冷却。

## 启动开发环境

日常双击 `start_dev.bat`，选择电脑当前连接的网络档案。脚本生成两块板的待编译网络头文件，启动 MQTT broker（1883）、后端（8000）、前端（5173），并打开网页。

| 命令 | 用途 |
|---|---|
| `start_dev.bat` | 接真实硬件调试 |
| `start_dev.bat -WithSim` | 同时启动遥测模拟脚本 |
| `start_dev.bat -NoBroker` | 跳过broker |
| `start_dev.bat -NoBrowser` | 不自动打开网页 |
| `start_dev.bat -NetworkProfile <名称>` | 指定网络档案 |
| `stop_dev.bat` | 按端口检查并停止开发服务 |

网页：[http://localhost:5173](http://localhost:5173)；后端接口：[http://127.0.0.1:8000/docs](http://127.0.0.1:8000/docs)。手机需与电脑处于可互通局域网，使用电脑实际IP和5173端口。

首次安装：

```powershell
# 项目根目录；使用独立后端环境，不要安装到ESP-IDF环境
py -3.12 -m venv backend/.venv
backend/.venv/Scripts/python.exe -m pip install -r backend/requirements.txt -r backend/requirements-dev.txt
Copy-Item backend/.env.example backend/.env
Copy-Item scripts/network_profiles.local.example.json scripts/network_profiles.local.json
# 填写本机网络档案；AI Key仅填写在backend/.env
cd frontend
npm install
```

前端使用Node 20+（推荐22）；启动脚本会查找可用版本。Wi-Fi档案按模板在同一个JSON对象中添加；私有档案、`.env`、生成的网络头文件均不提交版本库。

每次更换网络导致固件Wi-Fi/MQTT配置变化时，重新运行启动脚本，再分别编译、烧录需要更新配置的板子。生成头文件不会改变已经烧录的固件。ESP32使用2.4GHz Wi-Fi。

手动启动后端和前端：

```powershell
# backend目录
.venv/Scripts/python.exe scripts/init_db.py
.venv/Scripts/python.exe -m uvicorn app.main:app --host 127.0.0.1 --port 8000
# frontend目录，另开终端
npm run dev
```

## 模拟数据与历史兼容

网页“模拟上报一条数据”和 `backend/scripts/sim_device.py` 都将读数标为 `simulated`。小角标来自每条记录本身；后续设备数据到达后自动显示“设备”。模拟脚本仅提供遥测与在线状态，不模拟新版控制协议；执行器模拟验证由后端隔离测试完成。

旧数据库启动时自动增加 `data_source` 列，原记录保留，来源显示“来源未记录”。历史真值与模拟值无法可靠追溯，不自动猜测。旧S3固件无需增加字段，缺省按设备上报处理。

实物调试请停止 `-WithSim`：当前仍使用同一设备编号与主题，来源角标能区分读数，但不能消除模拟心跳对设备状态的干扰。

## 软件验证与启用

```powershell
# backend目录
.venv/Scripts/python.exe -m pytest tests -q
# frontend目录
npm run build
```

测试使用临时SQLite、模拟MQTT/HTTP和模拟时钟，不连接正式broker，不驱动实物或调用付费AI。本次调度轮换和来源角标只更新后端/前端，无需为此烧录固件。重启后端、刷新网页后生效；定时泵仍需板端报告 `timed_pumps_ready=true`。

## 文档

入口：[docs/README.md](docs/README.md)。

- [系统架构](docs/architecture.md)、[接线与实测边界](docs/hardware_wiring.md)
- [手动控制](docs/manual_control.md)、[四路自动控制](docs/automation_control.md)
- [HTTP接口](docs/api.md)、[MQTT契约](docs/mqtt_topics.md)
- [照片与AI分析](docs/camera_vision_demo.md)、[S3分区与升级](docs/s3_partitions.md)

尚未实现：业务WebSocket推送、历史曲线、标定后的湿度闭环补水/EC施肥、OTA。传感器开机识别失败的自动重试、温度上传和板端累计喷洒限制仍需后续处理。
