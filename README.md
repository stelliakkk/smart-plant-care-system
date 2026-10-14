# 基于大模型的智能植物养护系统

一个"能自己照顾植物"的完整系统：**ESP32 采集环境数据并执行动作，后端记录与决策，
大模型负责"看"植物有没有生病，网页端负责展示与控制。**

## 三端结构

```
edge/       两个独立 ESP-IDF 工程         —— S3 感知/执行；CAM 视觉节点预留
   ↕ MQTT
backend/    FastAPI + SQLite             —— 记录数据、闭环决策、调用 AI
   ↕ REST / WebSocket
frontend/   Vue 3 + Vite                 —— 实时监控、手动控制、诊断展示
```

三者可以**独立开发、独立运行**：没有硬件时用 `backend/scripts/sim_device.py`
冒充板子（`start_dev.bat -WithSim`）；后端没启动时前端会明确提示，不会白屏。

> ★ **别在接真板子的同时开着 `-WithSim`** —— 模拟设备每 3 秒往同一个
> `device_id` 灌一组假数据，会和板子的真数据混进同一张表，页面上的数字
> 就分不清真假了。调硬件时用不带参数的 `start_dev.bat`。

## 快速开始

### ★ 一键启动（日常就用这个）

双击 **`start_dev.bat`**，选择本机网络档案（默认有 `desk` 工位 WiFi 和 `debug` 手机热点）。它自动做四件事：

1. 校验电脑当前 Wi-Fi 与选择一致，生成两块板待编译的网络配置（含当前电脑 MQTT 地址）
2. 找到 Node 22（PATH 里的 v18 太老，Vite 6 跑不起来）—— **不用再手动改环境变量**
3. 依次拉起 MQTT broker（`0.0.0.0:1883`）、后端（`8000`）、前端（`5173`），各开一个带标题的窗口
4. 等前端就绪后**自动打开浏览器**，并把地址清楚地打印出来

```
  =====================================================
   启动完成 —— 把下面这个地址粘到浏览器里:

          http://localhost:5173

   手机/平板(需同一个 WiFi): http://192.168.202.59:5173
   ESP32 要连的 broker     : mqtt://192.168.202.59:1883
   后端接口文档            : http://127.0.0.1:8000/docs
  =====================================================
```

停止服务：双击 **`stop_dev.bat`**。它是**按端口**找进程的，并且只结束
python / node / cmd 这类进程 —— 别的软件占了端口只会提示并跳过，不会误杀。

可选参数：

| 命令 | 作用 |
|---|---|
| `start_dev.bat -WithSim` | 额外起一个**模拟设备**，没有硬件时页面也有数字在跳 |
| `start_dev.bat -NoBroker` | 不起 MQTT broker（只调前端样式时用，少一个窗口） |
| `start_dev.bat -NoBrowser` | 不自动打开浏览器 |
| `start_dev.bat -NetworkProfile <名称>` | 跳过菜单，使用 `scripts/network_profiles.local.json` 中对应的网络档案 |

新增网络时，编辑本机私有文件 `scripts/network_profiles.local.json`，添加一个条目，例如：

```json
{
  "apex": {
    "ssid": "apex",
    "password": "填写该 WiFi 密码"
  }
}
```

保留原有 `desk`、`debug` 条目，并把 `apex` 加在同一个 JSON 对象中（不要再套一层 `{}`）。重新运行 `start_dev.bat` 后，菜单会自动列出它；电脑先连上 `apex` 再选择该档案。该本机文件已加入 `.gitignore`，包含密码，不要提交到版本库。网络档案只生成 ESP32 编译配置；新增档案后还要分别重新编译、烧录需要切换网络的 ESP32。

> 已经在跑的服务会被自动识别并跳过，重复双击不会撞出"端口被占用"。

每次切换 Wi‑Fi 后，都要重新双击脚本选择对应档案，再分别重新烧录 ESP32-S3 与
ESP32-CAM。ESP32 仅支持 2.4 GHz；若工位路由器双频同名，电脑可继续在 5 GHz，
两块板会使用同名的 2.4 GHz。

### 手动启动（想单独调试某一端时用）

#### 1. 后端（必须最先启动）

```powershell
cd "D:\Lab\Embedded Curriculum Design\pro\backend"
py -3.12 -m venv .venv                       # 首次
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt              # 首次
copy .env.example .env                       # 首次（然后把 DeepSeek Key 填进去）
python scripts\init_db.py                    # 首次
uvicorn app.main:app --reload
```

接口文档：<http://127.0.0.1:8000/docs>

> ⚠️ **不要用 ESP-IDF 的虚拟环境装后端依赖**，会污染 IDF 导致 `idf.py` 起不来。

#### 2. 前端

```powershell
# PATH 里的 node 是 v18，太老，需要切到已有的 Node 22
$env:Path = "D:\比赛\物联网设计\.tools\node-v22.23.1-win-x64;" + $env:Path

cd "D:\Lab\Embedded Curriculum Design\pro\frontend"
npm install --registry=https://registry.npmmirror.com   # 首次
npm run dev
```

打开 <http://localhost:5173> —— 顶栏三个小圆点（后端 / MQTT / 设备）都变绿就说明通了。

#### 3. 边缘端（有硬件时）

> **接线先看 [docs/hardware_wiring.md](docs/hardware_wiring.md)** —— 引脚分配、逐线对应、常见坑、接完怎么自检都在那。

```powershell
. 'C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1'
cd "D:\Lab\Embedded Curriculum Design\pro\edge\esp32s3_plant_edge"
idf.py -p COM3 flash monitor
```

#### 没有硬件？用虚拟设备顶替

```powershell
cd "D:\Lab\Embedded Curriculum Design\pro\backend"
.\.venv\Scripts\Activate.ps1

# 本机没装 Mosquitto 时，先用纯 Python 的 broker 顶上
python scripts\dev_broker.py

# 再开一个终端，冒充 ESP32 持续上报数据
python scripts\sim_device.py --interval 3
```

虚拟设备发的报文格式与真实固件一致，将来换成真板子时后端和前端**一行都不用改**。

## 跑测试（后端）

```powershell
cd "D:\Lab\Embedded Curriculum Design\pro\backend"
.\.venv\Scripts\python.exe -m pytest tests -q
```

> ★ **测试不会碰到真板子的 MQTT 连接。**
> `backend/tests/conftest.py` 会在导入任何 `app.*` 之前，先把
> `MQTT_DISABLED=1` 和一个独立的 `MQTT_CLIENT_ID` 塞进环境变量。
>
> 原因是一个**真实踩过的坑**：MQTT 协议用 `client_id` 判重，同一个 id
> 再次连上来时 broker 会把**旧连接踢掉**。早先测试直接在进程内起
> `TestClient`，它会触发 lifespan → 用和后端**完全一样**的
> `client_id=plant-care-backend` 去连 broker → **正在运行的后端被静默顶下线**。
> 更隐蔽的是：纯 Python 的 amqtt broker 踢人时**不发 DISCONNECT 报文**，
> 于是 paho 的 `connected` 标志一直是 `True`，永不触发自动重连 ——
> 现象就是"pytest 跑完，页面上的设备永远离线了，重启后端才好"。
>
> 现在测试进程根本不连 broker（`MQTT_DISABLED=1` 时 `start()` 直接返回），
> 从物理上杜绝了这件事。`conftest.py` 里还有一个 autouse 的会话级夹具，
> 一旦隔离失效会立刻断言失败，防止将来有人不小心改回去。

## 文档

| 文档 | 内容 |
|---|---|
| [docs/architecture.md](docs/architecture.md) | 整体架构、数据流、职责划分、**安全设计原则** |
| [docs/hardware_wiring.md](docs/hardware_wiring.md) | ★ **接线表**：引脚分配、逐线对应、常见坑、接完怎么自检 |
| [docs/api.md](docs/api.md) | 全部 HTTP 接口与 WebSocket 协议 |
| [docs/mqtt_topics.md](docs/mqtt_topics.md) | MQTT 主题命名与报文格式（两端通信合同） |
| [backend/README.md](backend/README.md) | 后端目录结构、调试技巧 |
| [frontend/README.md](frontend/README.md) | 前端环境要求、页面说明、开发代理 |
| [edge/esp32s3_plant_edge/README.md](edge/esp32s3_plant_edge/README.md) | ESP32-S3 硬件信息、烧录步骤、实测记录 |
| [edge/esp32cam_vision_edge/README.md](edge/esp32cam_vision_edge/README.md) | ESP32-CAM 实时视觉节点、供电与烧录说明 |

## ★ 一条最重要的设计纪律

**执行器的安全策略必须实现在边缘端本地，不能依赖后端。**

后端下发的指令只是"意图"。断网时后端就失联了，如果"水泵最多抽 10 秒"
这条规则写在后端，一旦网络断了水泵可能一直抽下去 —— 把植物淹死，甚至烧坏水泵。

所以：板子自己管水泵最长运行时间、喷雾每日次数上限、执行器互锁、看门狗；
后端只负责决策、记录、以及在页面上诚实地标注"已下发 ≠ 已执行"。

详见 [docs/architecture.md](docs/architecture.md) 第 4 节。

## 硬件清单

| 部件 | 型号 / 备注 |
|---|---|
| 主控 | ESP32-S3-DevKitC-1 v1.1（模组 ESP32-S3-WROOM-1-N8R8） |
| 板载 RGB | WS2812，GPIO38 |
| 串口 | CP2102N，枚举为 COM3（可能因插拔变化） |
| 光照传感器 | BH1750 / GY-302，I2C 地址 `0x23`，SCL=**GPIO5**、SDA=**GPIO6** |
| 土壤湿度 | 电容式（teljesky v2.0 类），AOUT→**GPIO4**（ADC1_CH3） |
| 执行器 | 补水泵 / 施肥泵 / 补光灯 / 喷雾（待接入） |
| 摄像头 | AI Thinker 风格 ESP32-CAM（OV2640；独立 `cam01` 视觉节点，阶段 5 接入） |

## 当前进度

已完成 **阶段 0（三端骨架）** 和 **阶段 1（真实传感器上线，全链路打通）**。

### 阶段 1 实测记录的链路（2026-09-13，真板子跑通）

```
BH1750 光照 + 电容式土壤探头
  → ESP32-S3 每秒采样
  → MQTT plant/edge01/telemetry（每 3 秒，QoS 0）
  → 后端入库 SQLite
  → 前端每 3 秒轮询 /api/telemetry/latest → 页面数字跟着动
```

实机证据：

| 检查项 | 结果 |
|---|---|
| BH1750 探活 | ✅ `LIGHT: BH1750 found at 0x23` |
| 土壤 ADC | ✅ `SOIL: 土壤探头: GPIO4 = ADC1 通道 3`，曲线拟合校准已启用 |
| WiFi | ✅ 曾在 Debug 热点实测连通；热点 IP 变化或切换网络时，重新运行 `start_dev.bat` 选择档案并重烧两块板即可 |
| MQTT | ✅ `MQTT: 已连上 broker, device_id=edge01`，RSSI −27 dBm |
| 串口逐秒读数 | ✅ `light=  79.2 lux \| soil=raw=2559 2188 mV   2.0 %`（标定后；标定前同一读数显示 31.7%） |
| 数据库真实记录 | ✅ `(41, 'edge01', 31.8, None, None, 79.2)` |
| `/api/device/status` | ✅ `online:true, rssi:-27` |
| 土壤标定复标 | ✅ 端点由兜底 2600/1300 改为实测 **2200/1100** 后，同一支悬空探头 **31.7% → 2.0%**（步骤见 [docs/hardware_wiring.md](docs/hardware_wiring.md) 第 6.4 节） |
| 空闲堆 | ✅ 265 KB，长期稳定不降 |

> 盐分和温度**当前入库为 `NULL`**（对应的传感器还没接）—— 后端允许任一字段为空，
> 这是刻意的设计：先把链路打通，传感器到货后把 `NAN` 换成真实读数即可。

后续阶段（每处都在代码里留了 `TODO(阶段 X)`）：
WebSocket 推送 → 阈值闭环控制 → 前端图表 → 执行器 + 指令回执 → 摄像头 + AI 诊断。

完整路线图见 [docs/architecture.md](docs/architecture.md) 第 7 节。
