# 手动控制与执行回执

## 四路输出

| actuator | GPIO | 开启PWM | 时长 |
|---|---|---|---|
| pump 补水泵 | 9 | 20kHz / 50% | 1～30000ms |
| fertilizer 施肥泵 | 11 | 20kHz / 50% | 1～30000ms |
| spray 农药泵 | 13 | 20kHz / 50% | 1～30000ms |
| grow_light 补光灯 | 15 | 20kHz / 100% | 0持续开启，正数≤600000ms |

初始化均为0%，已移除上电5秒测试函数、声明和调用，没有旧启用开关。所有PWM写入由plant_control任务执行，约20ms检查运行期限。关闭失败继续重试，failed不伪装成已关闭。

补水与施肥在板端互斥：另一泵输出开启、正在停止或仍有运行任务时，拒绝开启并回pump_interlock；关闭始终绕过互斥。后端同时检查板子状态与pending/running指令，避免心跳尚未反映已下发动作时同时开泵。

## 手动优先

手动on/off暂停对应自动模式，暂停状态持久保存；即使手动指令未送达，也不再生成该路自动动作。恢复自动需网页主动操作；两路定时泵从完整新间隔开始。暂停自动会请求关闭该路正在运行的自动泵，但不改变该路已开始的手动定时动作。

关闭全部在同一后端锁内先暂停四路，再提交四个独立off。断网时板端停止所有泵与手动灯，清除排队动作；自动补光继续由本地读数控制。离线时无法通过网页保证停止指令送达，需要物理停止方式。

## 接口与回执

POST /api/device/command只接外部source=manual、action=on/off。sent=true只是提交到MQTT，返回command_id；回执决定pending→running→done/rejected/failed，超时显示timeout/实际状态未知。驱动回执不代表实测泵流量或灯具亮度。

GET /api/device/control-log默认最近30条。模式动作auto_on/auto_off也有回执，由自动化接口内部生成，外部不能直接提交。新启动会话、过期、retain、非法类型/时长和重复ID由板端处理。完整报文见[mqtt_topics.md](mqtt_topics.md)。

代码/软件测试通过不替代实体负载联调。2026-11-02用户确认补光灯已实测、三路泵已接线但未实测运行；板上定时能力以timed_pumps_ready为准。启用步骤见[automation_control.md](automation_control.md)与[s3_partitions.md](s3_partitions.md)。本次后端轮换修复不要求重新烧录。
