# 后端接口说明

基地址：`http://127.0.0.1:8000`

- 交互式文档（可以直接点击调试）：<http://127.0.0.1:8000/docs>
- 所有业务接口都在 `/api` 前缀下
- 请求与响应都是 **UTF-8 编码的 JSON**

前端开发时不用关心端口：`vite.config.js` 里有代理，写 `/api/xxx` 就会转发到 8000。

---

## 1. 健康检查

### `GET /api/health`

前端顶栏那个绿点用的就是它，同时也告诉你 MQTT 和 DeepSeek 配好了没。

```json
{
  "status": "ok",
  "app": "plant-care-backend",
  "device_id": "edge01",
  "mqtt_connected": true,
  "deepseek_configured": false,
  "vision_interval_sec": 0
}
```

| 字段 | 说明 |
|---|---|
| `mqtt_connected` | 后端连上 broker 了吗。false 时指令发不出去 |
| `deepseek_configured` | 有没有填 API Key |
| `vision_interval_sec` | 0 表示没开定时图像分析 |

---

## 2. 设备

### `GET /api/device/status`

查询边缘端在线状态。参数 `device_id`（可选，默认取配置里的）。

```json
{
  "device_id": "edge01",
  "online": true,
  "rssi": -69,
  "uptime_s": 3600,
  "last_seen": "2026-09-13T15:13:20.549591",
  "fault": ""
}
```

判定规则：超过 30 秒没收到任何上行报文就算离线。

### `POST /api/device/command`

下发执行器指令。

请求体：

```json
{
  "actuator": "pump",
  "action": "on",
  "duration_ms": 3000,
  "source": "manual"
}
```

- `actuator`：`pump` / `fertilizer` / `grow_light` / `spray`
- `action`：`on` / `off`
- `duration_ms`：0 ~ 600000，0 表示不自动关闭
- `source`：`manual`（前端手动）/ `auto`（阈值闭环）/ `ai`（AI 诊断触发）

响应：

```json
{
  "sent": true,
  "device_id": "edge01",
  "command": { "actuator": "pump", "action": "on", "duration_ms": 3000, "source": "manual" },
  "note": "sent 仅表示已发出; 实际执行结果以边缘端 cmd_ack 为准"
}
```

> **`sent: true` 不等于"已经执行"**，只表示报文成功发到了 broker。
> 边缘端可能因为本地安全策略（如喷雾今日次数已达上限）拒绝执行 ——
> 真实结果要看 `cmd_ack`。详见 `docs/mqtt_topics.md`。

`sent: false` 的常见原因：MQTT 未连接（broker 没启动）、或 `mqtt_connected` 为 false。

---

## 3. 传感器数据

### `GET /api/telemetry/latest`

最新一条读数。参数 `device_id`（可选）。

```json
{
  "available": true,
  "data": {
    "id": 8,
    "device_id": "edge01",
    "ts": "2026-09-13T15:13:20",
    "soil_moisture": 38.7,
    "soil_ec": 1.2,
    "temperature": 23.3,
    "light_lux": 1324.0
  }
}
```

还没有任何数据时返回 `{"available": false, "data": null}` ——
前端要判断 `available`，不要直接读 `data.id`。

### `GET /api/telemetry/history?limit=200&device_id=edge01`

历史读数，**按时间正序**返回（正好可以直接喂给图表库）。

```json
{
  "count": 2,
  "data": [
    { "id": 1, "ts": "2026-09-13T15:12:08", "soil_moisture": 33.5, "..." : "..." },
    { "id": 2, "ts": "2026-09-13T15:13:08", "soil_moisture": 44.0, "..." : "..." }
  ]
}
```

`limit` 默认 200，取的是"最近 limit 条"，但输出会翻回正序。

### `POST /api/telemetry/simulate`

手动写入一条数据。**没有硬件时调试前端的神器** —— 不用等板子上报。

请求体就是一份 `telemetry` 报文（字段可缺省，缺的存成 null）：

```json
{
  "device_id": "edge01",
  "soil_moisture": 33.5,
  "soil_ec": 1.1,
  "temperature": 25.2,
  "light_lux": 4200
}
```

响应：`{"saved": true, "id": 3}`

> 这个接口走的是和真实上报**完全相同**的入库流程，
> 所以它能存进去，说明 MQTT 那条路也没问题。

---

## 4. 相机实时画面与帧元数据

### `GET /api/vision/camera/status`

返回 `cam01` 的 MQTT 在线状态、后端是否已发现实时流地址，以及最近一次每分钟抓帧的
元数据。`stored` 始终为 `false`：当前阶段不保存 JPEG，也不调用 AI。

### `GET /api/vision/live`

后端代理 ESP32-CAM 的 MJPEG 流。相机离线、未上报地址或流连接失败时返回 `503` 和可读
错误；前端会据此显示离线/重连状态而不是白屏。

## 5. 图像分析

### `GET /api/vision/latest`

最近一次 AI 诊断结果。当前固定返回：

```json
{ "available": false, "data": null }
```

（阶段 5 会改成从 `vision_record` 表读最新一条）

### `POST /api/vision/analyze`

立刻抓拍并分析一次。当前未实现，返回：

```json
{ "analyzed": false, "reason": "图像分析尚未实现(阶段 5), 或摄像头不可用" }
```

---

## 6. WebSocket 实时推送

连接地址：`ws://127.0.0.1:8000/ws`（前端开发时用 `ws://localhost:5173/ws`，由 Vite 代理转发）

连上后服务端先发一条问候：

```json
{ "type": "hello", "data": { "msg": "已连接" } }
```

之后服务端会主动推送（**注意：这部分后端还没实现，前端接口已就绪**）：

| type | data | 说明 |
|---|---|---|
| `telemetry` | 一条读数 | 有新数据就推 |
| `vision` | 诊断结果 | 分析完成后推 |
| `status` | 设备状态 | 在线/离线变化时推 |

客户端可以发 `ping`，服务端回 `{"type":"pong"}`，用来保活。

前端实现见 `frontend/src/utils/ws.js`，**带自动重连**（退避重试 1s → 2s → … → 15s）。
断线时不要疯狂重试，那只会让服务器更难受。

---

## 7. 静态资源

当前相机抓帧不会写入任何图片文件，因此这里没有可访问的相机 JPEG。保留的
`GET /images/<文件名>` 静态挂载仅供未来明确启用“保存图片”后的诊断记录使用。

---

## 8. 错误约定

FastAPI 的标准错误格式：

```json
{ "detail": "错误说明" }
```

常见状态码：

| 状态码 | 含义 |
|---|---|
| 422 | 请求体字段不合法（比如 `duration_ms` 传了负数）。`detail` 里会指明是哪个字段 |
| 500 | 服务端异常，看后端日志 |
| 连接被拒绝 | 后端没启动，或端口被占用 |

前端的 `http.js` 已经把常见错误打到控制台了，联调时先看浏览器 F12 的 Console。
