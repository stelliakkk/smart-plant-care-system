# 后端服务（Python + FastAPI）

基于大模型的植物养护系统的**中间层**：承接边缘端（ESP32）的传感器数据，
向前端（Web / 手机）提供接口，并负责调用 DeepSeek 做图像分析。

## 快速开始

> ⚠️ **不要用 ESP-IDF 的虚拟环境装依赖**，会污染 IDF 导致 `idf.py` 跑不起来。
> 必须在 `backend/.venv` 里操作。

```powershell
cd "D:\Lab\Embedded Curriculum Design\pro\backend"

# 1. 建独立虚拟环境（用系统自带 Python 3.13）
& "$env:LOCALAPPDATA\Programs\Python\Python313\python.exe" -m venv .venv

# 2. 激活
.\.venv\Scripts\Activate.ps1

# 3. 装依赖
pip install -r requirements.txt

# 4. 准备配置（然后把 DeepSeek Key 填进去）
copy .env.example .env

# 5. 建表 + 插一株示例植物
python scripts\init_db.py

# 6. 启动
uvicorn app.main:app --reload
```

打开 <http://127.0.0.1:8000/docs> 就能看到自动生成的接口文档，可直接点击调试。

## 目录结构

```
backend/
├── app/
│   ├── main.py          FastAPI 入口：注册路由、CORS、启动/关闭钩子
│   ├── config.py        读 .env 的全局配置（路径统一转绝对路径）
│   ├── core/logging.py  统一日志格式
│   ├── api/             接口层：只负责收发数据
│   ├── schemas/         数据契约（Pydantic）= 前后端约定的字段
│   ├── services/        业务层：真正的逻辑
│   ├── db/              SQLAlchemy 引擎 / Session / ORM 模型
│   ├── mqtt/            与 ESP32 通信（断线自动重连，连不上不崩）
│   └── tasks/           APScheduler 定时任务
├── scripts/
│   ├── sim_device.py    ★ 虚拟设备：没有硬件时冒充 ESP32 发数据
│   └── init_db.py       建表 + 示例数据
├── data/                SQLite 数据库 + 抓拍图片（不进版本库）
├── tests/
├── requirements.txt
└── .env.example
```

## 四层职责（改代码前先想清楚该放哪层）

| 层 | 目录 | 只做这些事 |
|---|---|---|
| 接口层 | `api/` | 解析请求、调 service、返回结果。**不写业务逻辑** |
| 业务层 | `services/` | 真正的逻辑：入库、阈值判断、闭环决策、调 DeepSeek |
| 数据层 | `db/` | ORM 模型与 Session，不写业务规则 |
| 通信层 | `mqtt/` | 只负责收发报文，不写业务规则 |

## 接口清单

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/api/health` | 健康检查（前端首页的"后端已连通"就是它） |
| GET | `/api/device/status` | 边缘端在线状态 |
| POST | `/api/device/command` | 下发执行器指令 |
| GET | `/api/telemetry/latest` | 最新一条传感器读数 |
| GET | `/api/telemetry/history` | 历史读数（画曲线用） |
| POST | `/api/telemetry/simulate` | 手动塞一条数据（调试前端很方便） |
| GET | `/api/vision/latest` | 最近一次 AI 诊断结果 |
| POST | `/api/vision/analyze` | 立刻抓拍并分析一次 |
| WS | `/ws` | 实时推送 |

## 调试技巧

**没有硬件时怎么联调？** 两个终端各跑一个：

```powershell
# 终端 A —— 先启动 Mosquitto（本机已装好）
cd D:\SmartAgriBrainRuntime\mosquitto
.\mosquitto.exe -c mosquitto.conf

# 终端 B —— 启动后端
cd "D:\Lab\Embedded Curriculum Design\pro\backend"
.\.venv\Scripts\Activate.ps1
uvicorn app.main:app --reload

# 终端 C —— 启动虚拟设备
cd "D:\Lab\Embedded Curriculum Design\pro\backend"
.\.venv\Scripts\Activate.ps1
python scripts\sim_device.py --interval 3
```

虚拟设备发的报文格式和真实 ESP32 固件一致，所以将来用真板子替换它时，
后端和前端一行都不用改。

## 当前进度

骨架已完成，可以跑通"**虚拟设备 → MQTT → 后端 → 数据库**"这条链路。
下面这些还是空壳（代码里都留了 `TODO(阶段 X)`）：

- 阈值闭环决策（`services/control_service.py`）
- 图像抓拍与 DeepSeek 调用（`services/vision_service.py`）
- WebSocket 主动推送
- 数据入库后的实时推送给前端
