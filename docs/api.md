# 当前HTTP接口

后端开发地址http://127.0.0.1:8000，Swagger /docs。前端localhost:5173经Vite代理/api和/images。设备在线与相机在线分别判定。

## 设备与自动控制

| 方法/路径 | 内容 |
|---|---|
| GET /api/health | 后端、MQTT、AI配置状态 |
| GET /api/device/status | S3在线、boot_id、control_ready、四路actuators、automation_ready、timed_pumps_ready、automation、light_lux/light_fault |
| POST /api/device/command | 手动on/off；actuator四选一、duration_ms整数、source=manual；返回sent和command_id，不等于实际执行 |
| GET /api/device/control-log?limit=30 | 最近指令与running/done/rejected/failed/timeout状态 |
| POST /api/device/emergency-stop | 暂停四路自动并分别下发off；离线无法保证指令送达 |
| GET /api/device/automation | 四路模式、板端确认、两路定时参数/下次时间/回执、光照、虫害计数、48小时冷却和事件 |
| POST /api/device/automation | {actuator:pump|fertilizer|spray|grow_light, enabled:boolean}；未配置定时参数时启用400 |
| POST /api/device/pump-schedule | {actuator:pump|fertilizer, interval_sec:60..604800, duration_ms:1..30000}，必须整数；保存后暂停 |

示例：保存清水演示补水间隔（示例数字不是实际植物养护建议）：

```json
{"actuator":"pump","interval_sec":600,"duration_ms":1000}
```

随后POST /api/device/automation {"actuator":"pump","enabled":true}，从恢复时刻重新等待600秒。关闭/保存/手动控制不修改另一泵配置；修改参数不会隐式启用。

## 传感器

GET /api/telemetry/latest返回available/data；GET history返回count/data（最近记录按时间正序）；POST simulate可插入模拟读数，但不会触发自动补水/施肥，也不能证明真实MQTT设备在线。soil_moisture为当前探头标定的相对指标，soil_ec/temperature真实固件当前为null；模拟值不代表已接入实际传感器。

每条读数返回data_source：device（设备上报）、simulated（模拟）、unknown（历史来源未记录）。POST simulate无论请求如何填写data_source，均强制保存为simulated。MQTT旧固件不带字段时按device保存；模拟脚本显式发送simulated。旧数据库在后端启动时自动增加此列，既有记录保留unknown。

总览数值旁显示来源小角标，历史表逐行显示来源；来源随记录刷新，不会把后续设备上报继续标记成模拟。device仅表示上报路径，不是测量准确性认证。

## 相机与分析

| 方法/路径 | 内容 |
|---|---|
| GET /api/vision/camera/status | CAM在线、拍照地址是否发现、分析忙、Key配置、间隔/保留天数；legacy latest_frame是历史元数据，不是当前照片 |
| POST /api/vision/analyze | 异步202；忙409；离线503；手动抓拍不触发喷洒 |
| GET /api/vision/latest | available/data/busy |
| GET /api/vision/history?limit=30&before_id=… | items/next_cursor，7天内按ID降序分页 |
| GET /api/vision/record/{id} | 单条照片与结果，过期/不存在404 |
| GET /api/vision/live | 已退役：相机在线410，离线503 |
| GET /images/{filename} | 已保存的JPEG |

## 错误与实时更新

字段不合法422，配置不满足400，API异常500；FastAPI错误detail可能为文字或字段错误列表。/ws仅hello、ping/pong和echo，未接业务广播；页面用HTTP轮询更新。

后端单实例、单worker。自动定时由后台每秒检查，不依赖浏览器保持打开；依赖后端与MQTT运行。周期内错过的动作不补发。
