# 当前MQTT通信契约

| 主题 | 方向 | QoS / retain |
|---|---|---|
| plant/edge01/telemetry | S3→后端 | 0 / false |
| plant/edge01/status | S3→后端 | 0 / false |
| plant/edge01/cmd | 后端→S3 | 1 / false |
| plant/edge01/cmd_ack | S3→后端 | 1 / false |
| plant/cam01/status | CAM→后端 | 0 / false |

后端按这些精确主题注册处理函数，当前不发布AI vision控制主题。QoS1可能重复，不能假设一次且仅一次；动作必须关联指令ID、会话、期限并去重，断网不能假设能补发有效动作。

## 上行

telemetry含device_id、soil_moisture、soil_ec、temperature、light_lux。缺测null；当前盐分/温度始终null，湿度是标定后的相对百分比，不是经实测验证的绝对含水量。

可选data_source取device/simulated/unknown；旧固件缺省按device保存。backend/scripts/sim_device.py显式发送simulated；后端网页模拟接口也强制标记simulated。unknown用于迁移前无来源信息的历史记录，不能推断为真实测量。模拟脚本仅用于遥测/在线状态展示，不模拟新版控制回执、自动模式或板端互锁；控制规则测试使用backend/tests下的隔离模拟。

S3心跳含online、rssi、uptime_s、fault、boot_id、control_ready、actuators四路布尔；automation_ready、automation四路布尔、timed_pumps_ready、light_lux、light_fault。timed_pumps_ready缺失/false时后端禁止新的定时泵命令，但兼容旧固件原有功能。

CAM每3秒发布device_id=cam01和snapshot_url=http://实际IP/capture，stream_url为空。图片走HTTP；后端每300秒定时拍照、保存并分析，不再每分钟仅抓元数据。

## 指令

```json
{"command_id":"0123456789abcdef0123456789abcdef","boot_id":"1234abcd","actuator":"pump","action":"on","duration_ms":1000,"source":"auto","deadline_uptime_ms":115000}
```

command_id为32位小写十六进制UUID，boot_id为本次S3的8位小写十六进制会话。deadline_uptime_ms是板端单调时钟期限，后端通常设15秒有效；板端限制最大未来窗口20秒，拒绝旧会话、过期、retain、非法数字/字段及>1024字节报文。

- 手动：source=manual，四路on/off，泵1～30000ms，灯0持续或≤600000ms。
- 自动：source=auto，仅pump/fertilizer短时on（1～30000ms），或spray/on/2000ms；对应板端自动模式必须已启用，自动远程灯指令拒绝，灯用本地传感器。
- 模式：source=manual，action=auto_on/auto_off，duration_ms=0，由后端自动化接口内部产生，四路支持并保存NVS。
- 后端外部/command只允许手动on/off，自动执行必须由内部规则生成；原source=ai不是固件可执行来源。
- 两泵互斥涵盖手动/自动，板端拒绝另一泵正运行/停止的开启指令，reason=pump_interlock。

## 回执

```json
{"command_id":"0123456789abcdef0123456789abcdef","boot_id":"1234abcd","actuator":"pump","result":"running","reason":"output_on"}
```

running=驱动设置开启；done=关闭或模式改变完成；rejected=明确拒绝；failed=驱动/持久化失败。后端pending/running超时显示timeout/实际状态未知。没有电流/流量反馈，回执不能证明负载实际运动。关闭失败保留开启/未知状态并重试；重复ID重放既有回执，不重复启动。

## 联调

查看上行可用mosquitto_sub -h 127.0.0.1 -t "plant/#" -v。实际控制通过网页/API，不使用旧缺少command_id/boot_id/期限的示例报文。若两板MQTT超时，先核对电脑WLAN实际IP、固件内broker地址、监听1883与防火墙；网络头文件生成后需重新编译烧录才生效。
