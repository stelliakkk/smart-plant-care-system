# ESP32-S3 采集与执行节点

目标为ESP32-S3-DevKitC-1 v1.1 / WROOM-1-N8R8，当前构建记录使用ESP-IDF 6.0.1（组件要求>=6.0）。8 MiB Flash，自定义2 MiB factory应用分区，无OTA；PSRAM配置默认未开启。

## 当前结构

`main/plant_edge.c`编排初始化和采样。`components/`包含board_info、rgb_led、i2c_bus/light_sensor、共享ADC与soil_sensor/salinity_sensor、temperature_sensor、四路执行器、wifi_sta、mqtt_report和plant_control。

GPIO与供电说明见[接线文档](../../docs/hardware_wiring.md)。土壤与盐分共享ADC1；BH1750为GPIO5/6，DS18B20为GPIO8，执行器为9/11/13/15，板载RGB为38。

## 当前行为

- 开机四路执行器初始化为关闭，不再进行上电运行5秒测试。
- 循环读取光照、湿度、盐分原始值与温度，再延时1000ms；实际采样间隔包含传感器等待时间，不保证精确1Hz。
- MQTT目标每3秒上报；湿度/光照上传，盐分和温度目前固定上传null，本地仍有读取和日志。
- plant_control负责有效指令校验、ID去重、泵最长30秒、两泵互锁、断网停止和本地自动补光；模式保存NVS。
- 补光低于100 lux持续10秒开启，高于300 lux持续30秒关闭；无有效读数10秒关闭自动灯。自动补光断网继续。
- 两路定时安排与争用轮换由电脑后端完成；S3只执行限时动作。没有板端每日喷洒累计限制或48小时冷却，48小时规则目前由后端保存。

2026-11-02用户确认：实物电源正在使用，补光灯已实测；三路泵已接线并实现逻辑，运行效果待实测。当前板上是否包含定时能力以timed_pumps_ready心跳字段为准。

## 编译与烧录

先激活已安装的ESP-IDF环境，在本目录运行`idf.py build`。按实际串口运行`idf.py -p COMx flash monitor`，原生USB JTAG路径参考[分区与升级](../../docs/s3_partitions.md)。串口号以设备管理器为准，不固定为COM3/COM4。

网络参数由根目录启动脚本生成`main/generated_network_config.h`；本机标定在不提交版本库的`main/local_config.h`。修改网络参数需重新编译烧录。

本次后端调度轮换与前端来源角标不改固件行为，无需为此烧录。源码中的驱动组件通过各自`CMakeLists.txt`声明依赖；组件不反向引用main头文件。

## 历史开发记录

以下为2026-09-12/13早期RGB和工程迁移验证，保留当时日志与参数。其固件大小、分区余量、IDF版本、目录名和串口号仅适用于当时版本；当前状态以上述说明与现有源码为准。

## 实测验证记录（Windows + CP2102N，2026-09-12）

| 检查项 | 结果 |
|---|---|
| `idf.py build` | ✅ 通过，无 warning（日志里 5 条 `CMake Warning` 全部来自 IDF 自带的 esp_wifi / wpa_supplicant，与本工程代码无关） |
| 固件大小 | `0x32790` ≈ 207 KB；应用分区 1 MB，剩余约 80% |
| 依赖隔离 | ✅ `main/CMakeLists.txt` 已不含 `esp_driver_rmt`，RMT 细节成功收敛在 `rgb_led` 组件内 |
| `idf.py -p COM3 flash` | ✅ bootloader / 分区表 / app 全部写入并 `Hash of data verified` |
| 串口 115200 输出 | ✅ System Info 与每秒心跳均正常打印，TAG 前缀正确区分 3 个模块 |
| 空闲堆 | 388208 B ≈ 379 KB（内部 SRAM，PSRAM 未启用） |
| GPIO38 波形 | ✅ 彩灯模式下灯珠按 `红→绿→蓝→青→品红→黄→白→灭` 循环（人眼已确认） |
| 灭灯模式 | ✅ 烧录后日志为 `RGB LED = OFF (latched)`，灯熄灭并持续保持（人眼已确认） |

### 目录搬移 + 工程重命名后的回归验证（2026-09-13）

> **历史名称说明**：以下内容记录的是 2026-09-13 当时的迁移验证；其中
> `esp32_plant_edge` 是旧目录和旧工程名。当前工程已在后续整理中更名为
> `esp32s3_plant_edge`，以便与 ESP32-CAM 视觉工程明确区分。

工程由 `ESP32S3WROOM1/` 搬到 `pro/edge/esp32_plant_edge/`；
`project(ESP32S3WROOM1)` → `project(esp32_plant_edge)`；
`main/ESP32S3WROOM1.c` → `main/plant_edge.c`。

| 检查项 | 结果 |
|---|---|
| 全仓搜索旧名 | ✅ 无任何 `ESP32S3WROOM1` 残留（含 `sdkconfig`） |
| `idf.py build` | ✅ 1083/1083 任务全部完成；CMake configure 耗时 61.8 s |
| 产物名 | ✅ `build/esp32_plant_edge.bin`，208,736 B（`0x32790`），与改名前一模一样 |
| 分区余量 | ✅ 应用分区 1 MB，剩余 `0xcd870` ≈ 80% |
| `idf.py -p COM3 flash` | ✅ `Hash of data verified`，写入 2.9 s（576 kbit/s） |
| 串口 `Project name` | ✅ `app_init: Project name: esp32_plant_edge` ← **改名生效的铁证** |
| 组件识别 | ✅ 组件路径正确指向 `pro/edge/esp32_plant_edge/{components/board_info, components/rgb_led, main}` |
| 运行行为 | ✅ 1 s 心跳、空闲堆 388208 B，与搬移前逐项一致（无回归） |
| 串口噪声 | 复位瞬间偶尔夹带几个乱码字节（如 `pm install`），属正常现象，可忽略 |

> 搬移目录时若报 *being used by another process*，是 **VS Code 把这个目录当工作区根打开了**，
> 它对目录持有句柄，Windows 就搬不动。先把工作区切走（或关掉 VS Code）再搬。

### 模块化重构（单文件 → 组件化）实测

| 检查项 | 结果 |
|---|---|
| 重构前 | 1 个 349 行的 `main/plant_edge.c`，驱动、板级信息、业务全混在一起 |
| 重构后 | `main/plant_edge.c` 约 120 行（只剩 `app_main` + 两个模式函数）<br>新增 `components/rgb_led/`、`components/board_info/`、`main/app_config.h` |
| 编译 | ✅ `idf.py build` 成功，无新增 warning |
| 运行行为 | ✅ 与重构前逐行一致，**只有日志 TAG 前缀变化**（`APP:` → `BOARD:` / `RGB_LED:`） |
| 心跳周期 | ✅ 1 s（实测时间戳 1339 / 2339 / 3339 / 4339 / 5339 / 6339 / 7339 ms） |
| 空闲堆 | ✅ 388208 B，与重构前完全相同（拆分没有引入额外内存开销） |
| LED 状态 | ✅ 仍为"熄灭并锁存"，行为未变 |

### 怎么确认灯真的亮了（串口日志 ≠ 灯亮）

串口日志只能证明"程序在按计划发数据"。板子上那颗 RGB 灯位于 **USB 口与 EN 按钮之间**：
彩灯模式（`RGB_LED_DEMO_ENABLE 1`）下每 500 ms 换一次颜色，灭灯模式下则一直熄灭。
若日志正常但灯的表现不对，按顺序排查：

1. **板子版本**：v1.1 用 GPIO38，老版 v1.0 是 GPIO48。改 `components/rgb_led/rgb_led.c` 里的 `RGB_LED_GPIO` 宏后重烧。
2. **USB 线/口**：WS2812 峰值电流较大，劣质线材供电不足会导致灯不亮或闪烁异常。
3. **别同时手工拉这个引脚**：`rmt_enable()` 之后 GPIO38 已归 RMT 外设管，
   再调 `gpio_set_level(GPIO38, ...)` 会和 RMT 打架。
4. **别删掉 `rmt_tx_wait_all_done()`**：`pixels` 是栈上局部数组，
   `rmt_transmit()` 只是把指针交给后台，不等待发完就 return 会发出发送一半的脏数据。

### 调试小技巧

```powershell
# 只看串口日志，不重新烧录（退出按 Ctrl + ]）
idf.py -p COM3 monitor

# 本工程自带的小脚本：先复位芯片，再抓 8 秒完整日志
# （用 pyserial，ESP-IDF 的 python 环境里已自带，无需另装）
python capture_serial.py COM3 8

# 第 3 个参数传 0 = 不复位，只监听当前输出
python capture_serial.py COM3 5 0
```

PowerShell 里也能直接读端口（不想装串口助手时用）：

```powershell
$sp = New-Object System.IO.Ports.SerialPort('COM3',115200,'None',8,'One')
$sp.ReadTimeout = 600; $sp.Open()
$sp.RtsEnable=$true; Start-Sleep -Milliseconds 150; $sp.RtsEnable=$false   # 拉 RTS 复位，抓完整开机日志
$sw=[Diagnostics.Stopwatch]::StartNew()
while($sw.Elapsed.TotalSeconds -lt 6){ try{ Write-Host $sp.ReadLine() }catch{} }
$sp.Close()
```

> `monitor` 一次只能被一个程序占用 COM3，抓日志前记得先关掉串口助手/其他的 monitor 窗口。
