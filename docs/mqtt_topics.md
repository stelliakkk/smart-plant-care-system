# MQTT 主题与报文格式

边缘端（ESP32）和后端之间只有一条通道：MQTT。
这份文档就是双方的**通信合同** —— 改任何一个字段名之前，先想想对面要不要一起改。

## 1. 主题命名

```
plant/{device_id}/{channel}
```

| 频道 | 方向 | 说明 |
|---|---|---|
| `telemetry` | 板子 → 后端 | 传感器数据，定期上报 |
| `status` | 板子 → 后端 | 心跳，用于判断在线/离线 |
| `cmd` | 后端 → 板子 | 执行器指令 |
| `cmd_ack` | 板子 → 后端 | 指令回执（执行了？被拒绝了？） |
| `vision` | 后端 → 板子 | AI 诊断结论（可选，让板子的屏幕显示） |

传感器/执行器节点编号为 `edge01`；视觉节点编号为 `cam01`。两者是独立设备，
后端分别订阅 `plant/edge01/#` 与 `plant/cam01/status`。

- 后端：`backend/.env` 的 `DEVICE_ID`
- 边缘端：`edge/main/app_config.h` 的 `PLANT_DEVICE_ID`

**这两处必须一致**，否则后端订阅不到板子的数据（表现为"板子在线但页面没数据"）。

后端订阅的通配主题：

```
plant/{device_id}/#
```

`#` 是 MQTT 的多层通配符，一条订阅就能覆盖全部频道。

## 2. QoS 与保留消息

| 频道 | QoS | 说明 |
|---|---|---|
| `telemetry` | 0 | 丢了就丢了，下一轮还有。用 QoS 1 只是白白增加流量 |
| `status` | 0 | 同上，心跳本来就会周期性重发 |
| `cmd` | 1 | **必须用 1**，指令不能丢。断线重连后 broker 会补发 |
| `cmd_ack` | 1 | 回执也不能丢，否则后端不知道命令到底执行了没 |

暂不使用 retained 消息。将来如果需要"前端一打开就看到最后一个状态"，
可以给 `status` 加 retain。

## 3. 报文格式

全部是 **UTF-8 编码的 JSON**。字段名统一用 `snake_case`。

### 3.1 `telemetry` —— 传感器上报

```json
{
  "device_id": "edge01",
  "ts": "2026-09-13T15:13:08",
  "soil_moisture": 44.0,
  "soil_ec": 1.2,
  "temperature": 24.1,
  "light_lux": 536.0
}
```

| 字段 | 类型 | 单位 | 说明 |
|---|---|---|---|
| `device_id` | string | — | 设备编号，缺省为 `edge01` |
| `ts` | string | ISO 8601 | 采集时间，**留空则用服务器接收时间** |
| `soil_moisture` | number | % | 土壤湿度 |
| `soil_ec` | number | mS/cm | 土壤盐分（反映肥力） |
| `temperature` | number | °C | 环境温度 |
| `light_lux` | number | lux | 光照强度 |

**每个测量值都允许为 `null`** —— 某个传感器还没接上时，其余数据要能正常入库。
后端对应的是 `app/schemas/telemetry.py` 里的 `TelemetryPayload`。

### 3.2 `status` —— 心跳

```json
{
  "device_id": "edge01",
  "online": true,
  "rssi": -69,
  "uptime_s": 3600,
  "fault": ""
}
```

`fault` 非空表示板子自检异常（例如"水泵驱动异常""传感器读取失败"）。
后端超过 **30 秒**收不到任何上行报文就判定为离线（见 `device_service.OFFLINE_TIMEOUT_SEC`）。

### 3.3 `cam01/status` —— 相机服务发现

```json
{
  "device_id": "cam01",
  "online": true,
  "rssi": -58,
  "uptime_s": 3600,
  "fault": "",
  "stream_url": "http://192.168.1.88/stream",
  "snapshot_url": "http://192.168.1.88/capture"
}
```

相机每 3 秒上报一次。热点重连会改变 IP，因此后端只信任最新一次状态中的 URL；
图片本体不走 MQTT。`stream_url` 是 MJPEG 实时流，`snapshot_url` 只由后端每分钟抓取
一次以记录帧元数据，JPEG 随即丢弃。

### 3.4 `cmd` —— 执行器指令

```json
{
  "actuator": "pump",
  "action": "on",
  "duration_ms": 3000,
  "source": "manual"
}
```

| 字段 | 取值 | 说明 |
|---|---|---|
| `actuator` | `pump` / `fertilizer` / `grow_light` / `spray` | 补水泵 / 施肥泵 / 补光灯 / 喷雾 |
| `action` | `on` / `off` | — |
| `duration_ms` | 0 ~ 600000 | 计划运行时长；`0` 表示"不自动关，等下次指令" |
| `source` | `manual` / `auto` / `ai` | 谁下的令：前端手动 / 阈值闭环 / AI 诊断 |

> ### ⚠️ 这条指令只是"意图"
>
> 后端说"开 3 秒"，**不代表板子真会开 3 秒**。板子有自己的安全规则：
> 水泵连续运行上限、两次启动的最小间隔、喷雾每天次数上限、缺水保护、看门狗……
> 超出限制时板子**拒绝执行**并回报 `rejected`。
>
> 为什么不让后端来管安全？因为**断网时后端就失联了**。
> 如果"水泵最多抽 10 秒"写在后端，一旦网络断了，水泵可能一直抽下去，
> 把植物淹死甚至烧坏水泵。**安全逻辑必须在设备本地。**

### 3.5 `cmd_ack` —— 指令回执

```json
{
  "actuator": "pump",
  "result": "done",
  "reason": "已运行 3000 ms",
  "ts": "2026-09-13T15:13:22"
}
```

`result` 三种取值：

| 值 | 含义 |
|---|---|
| `done` | 执行完成 |
| `rejected` | 被本地安全策略拒绝（`reason` 里写清楚原因，如"今日喷雾次数已达上限"） |
| `failed` | 尝试执行但失败（如"水泵电流异常"） |

### 3.6 `vision` —— AI 诊断结论（可选）

```json
{
  "has_issue": true,
  "issue_type": "虫害",
  "description": "叶片背面可见密集白色小点，疑似蚜虫",
  "advice": "隔离该植株，使用低毒杀虫剂喷洒叶背",
  "need_spray": true
}
```

`need_spray` 为真时，后端会走 `control_service.decide_from_vision()` 下发喷雾指令。

## 4. 用命令行手动收发（调试用）

装好 Mosquitto 后自带这两个工具：

```powershell
# 订阅全部上行报文, 实时看板子在发什么
mosquitto_sub -h 127.0.0.1 -t "plant/edge01/#" -v

# 手动发一条传感器数据
mosquitto_pub -h 127.0.0.1 -t "plant/edge01/telemetry" -m "{\"device_id\":\"edge01\",\"soil_moisture\":18.5,\"light_lux\":900}"

# 手动下发一条补水指令
mosquitto_pub -h 127.0.0.1 -t "plant/edge01/cmd" -m "{\"actuator\":\"pump\",\"action\":\"on\",\"duration_ms\":3000,\"source\":\"manual\"}"
```

如果 broker 开了账号验证，加上 `-u 用户名 -P 密码`。

## 5. 联调顺序建议

遇到"数据上不来"时，从下往上逐段排查，别一上来就怀疑代码：

1. **broker 起着吗** —— `mosquitto_sub` 能不能连上
2. **板子在发吗** —— `mosquitto_sub -t "plant/#" -v` 有没有刷屏
3. **后端订阅了吗** —— 看后端日志有没有 `已订阅上行主题: plant/edge01/#`
4. **主题对得上吗** —— 两端的 `device_id` 是否一致（最常见的问题）
5. **字段解析过了吗** —— 后端日志有没有"上报数据格式不合法, 已丢弃"

## 2026-10 网页控制扩展

新版 S3 的 cmd/cmd_ack 加入 command_id、boot_id、deadline_uptime_ms、running 回执与状态快照，完整协议及限制见 [manual_control.md](manual_control.md)。旧的不含会话/截止时间的指令不再用于实际设备控制；相机及传感器上行主题保持不变。
