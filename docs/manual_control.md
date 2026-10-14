# 网页手动控制实现与验证

## 行为

- S3 GPIO9 补水泵、GPIO11 盐水/施肥泵、GPIO13 农药泵沿用现有 PWM 驱动，开启占空比50%；GPIO15补光灯开启占空比100%。
- 三个泵单次时长1～30000ms；补光灯0表示保持开启，正数定时最多600000ms。
- 开机只初始化，默认不执行5秒硬件测试。需要旧测试时显式打开 main/app_config.h 的 APP_ACTUATOR_STARTUP_TEST_ENABLE 并重新编译。
- 独立控制任务20ms轮询本地截止时间，不依赖后端计时。Wi-Fi未连接或MQTT断开时，关闭泵与手动灯光并清除排队指令；自动补光继续本地运行。
- 单路正在运行时拒绝另一个开启指令，关闭指令可中止原定时任务。网页增加关闭全部输出和操作记录。
- 自动控制扩展见 `automation_control.md`：仅允许受规则约束的 source=auto 农药泵指令；AI不直接生成设备指令。

## 通信契约

沿用 `plant/edge01/cmd`（QoS1、不retain）及 `plant/edge01/cmd_ack`（QoS1、不retain）。

```json
{"command_id":"32位小写十六进制UUID", "boot_id":"8位小写十六进制", "actuator":"pump", "action":"on", "duration_ms":3000, "source":"manual", "deadline_uptime_ms":115000}
```

boot_id为本次S3启动会话；deadline_uptime_ms为板子上电单调时钟截止点。后端根据最近心跳uptime估算，指令15秒内有效，S3也限制最大未来窗口20秒；不依赖NTP。固件拒绝旧启动会话、过期、retain、非法类型和越界时长。

重复command_id重发既有回执，不重复启动输出。缓存满时拒绝新开启，关闭仍可处理；缓存不会驱逐有效期内或正在运行的记录。MQTT碎片先按长度/偏移完整重组，最大1024字节。

回执含command_id、boot_id、actuator、result、reason。result为running/done/rejected/failed：running表示驱动已设置开启，done表示已设置关闭。它们不是负载电流/流量反馈，不代表已实测水泵运转。

心跳增加boot_id、control_ready和actuators四路布尔状态。旧固件无control_ready时，网页提示先升级，不把“设备在线”误认为支持控制。

## 后端与网页

- POST /api/device/command：sent仅代表成功提交MQTT，并返回command_id。
- GET /api/device/control-log：返回最近30条已提交指令及回执状态；pending超过15秒或运行结束后超过15秒无回执，显示timeout和实际状态未知。补光灯持续开启等待关闭回执。
- POST /api/device/emergency-stop：下发四个独立关闭指令，分别追踪结果。此功能依赖网络，不能替代物理断电。
- 操作表actuator_command为新增表，由启动create_all创建；不修改旧表列，旧数据保留。
- 网页每2秒获取状态和回执。退出页面不会关闭持续开启的灯，MQTT断开会由板子关闭。

## 上线步骤与范围

1. 重启后端，创建新增表并订阅cmd_ack。
2. 烧录新版ESP32-S3固件。CAM不需要更新。
3. 保持S3和电脑在同一可互通网络，确认control_ready为true。
4. 以短时指令验证开启、到时关闭、提前关闭、断网停止与回执，再连接实际负载检查效果。

当前完成代码、接口审查与编译/软件测试；本次没有烧录，没有对实际泵/灯下发测试指令。运行中的旧后端与旧固件需要上述更新才能使用新控制功能。

## 审查重点

驱动调用只在控制任务中发生；回执使用MQTT enqueue，避免网络阻塞本地计时。关闭失败返回failed并保留未知/开启状态，不伪称已关闭；断网时继续重试关闭。后端先提交数据库再发布，避免快速回执到达时找不到记录；按设备主题、命令ID、boot_id、actuator关联，并忽略重复或倒序状态回执。测试使用隔离数据库、模拟MQTT，不会驱动硬件。

## 本次验证结果

后端34项测试通过，包括QoS1指令、截止时间、运行/完成回执、过期和乱序回执、旧固件拒绝、泵时长限制、持续补光、关闭全部输出、MQTT发送失败与超时未知状态。前端生产构建通过；S3使用ESP-IDF6.0.1编译通过，新增cJSON托管依赖适配IDF6，应用分区剩余约5%。
尚未对新版固件进行烧录或负载测试；软件回执不替代实际负载反馈。CAM、照片分析与其7天保存功能保持原接口。
