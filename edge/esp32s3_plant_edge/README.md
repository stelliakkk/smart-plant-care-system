# 智能植物养护系统 · 边缘端（ESP32-S3）

一个跑在 **ESP32-S3-DevKitC-1 v1.1** 上的 ESP-IDF v6.1 入门示例。

## 硬件信息（esptool 实测）

| 项目 | 值 |
|---|---|
| 开发板 | ESP32-S3-DevKitC-1 v1.1 |
| 模组 | ESP32-S3-WROOM-1-N8R8 |
| 芯片 | ESP32-S3 (QFN56)，silicon rev v0.2，双核 240 MHz + LP 核 |
| Flash | 8 MB，Quad（4 线），3.3 V |
| PSRAM | 8 MB 内封 Octal PSRAM（3.3 V），默认未开启 |
| 板载 RGB 灯 | WS2812 可寻址灯珠，接 **GPIO38** |
| USB 转串口 | CP2102N（VID_10C4 / PID_EA60），枚举为 `COM3` |
| 下载/日志口 | UART0（GPIO43 = TX，GPIO44 = RX） |

## 程序功能

1. 开机打印芯片信息、Flash 容量、WiFi STA MAC、复位原因、空闲堆、IDF 版本
2. 驱动板载 WS2812 RGB 灯（行为由 `RGB_LED_DEMO_ENABLE` 宏切换）
3. **每秒**读一次 BH1750 光照 + 电容式土壤湿度，打印到串口
4. **每 3 秒**通过 MQTT 上报给后端 → 入库 → 前端页面刷新

### 数据流（阶段 2 已打通）

```
传感器 → ESP32 采样 → MQTT plant/edge01/telemetry → 后端入库
      → 前端每 3 秒轮询 /api/telemetry/latest → 页面数字跟着动
```

盐分 / 温度两项**现在上报的是 `null`**（对应的传感器还没接）。后端允许任一字段为
`null`，所以不会因此丢掉整条数据；等传感器接入后，只要把 `main/plant_edge.c`
里 `publish_sample()` 中的 `NAN` 换成真实读数即可。

### 串口长什么样

```
I (331) I2C_BUS: Init I2C master bus: SCL=GPIO5, SDA=GPIO6, 100000 Hz
I (341) LIGHT: BH1750 found at 0x23
I (361) SOIL: 土壤探头就绪 (生效标定端点: 干燥 2200 mV / 湿润 1100 mV; 悬空应≈0%, 泡水应≈100%)
I (561) APP: light=   65.0 lux | soil=raw=2559 2188 mV   2.0 % | heap=385208 B
I (5761) MQTT: 已上报 {"device_id":"edge01","soil_moisture":2.0,"soil_ec":null,"temperature":null,"light_lux":65.0}
```

> 上面这行 `2188 mV` 是探头**悬空在空气里**的读数，标定端点取的就是它附近的 `2200`，
> 所以湿度显示 `2.0 %`（≈0%，正确）。若用驱动里的兜底端点 `2600/1300`，
> **同一个 2188 mV 会算出 31.7 %** —— 空气里却报"土有三分之一湿"。
> 标定步骤见 [../../docs/hardware_wiring.md](../../docs/hardware_wiring.md) 第 6.4 节。
>
> 采样行每秒一条（给人看），上报行每 3 秒一条（给后端看）—— 两个节奏刻意分开，
避免"采一次报一次"把数据库刷满。

## 烧录前必做：选择网络并保留本机标定

WiFi 密码和电脑 IP 由项目根目录的 `start_dev.bat` 统一管理；双击后选择 `desk` 或
`debug`，脚本会校验 Windows 当前 WiFi，并为本工程和 ESP32-CAM 同时生成网络配置。
之后必须重新 build + flash 两块板。土壤探头标定仍是本机私有配置：

```powershell
cd main
Copy-Item local_config.example.h local_config.h    # 仅填写土壤标定值
```

| 文件 | 是否提交 | 说明 |
|---|---|---|
| `main/local_config.example.h` | ✅ 提交 | 土壤标定模板 |
| `main/local_config.h` | ❌ 已在 `.gitignore` | 本机土壤标定真实值 |
| `main/generated_network_config.h` | ❌ 已在 `.gitignore` | 启动脚本生成的 WiFi/MQTT 配置 |

标定端点只对**你这支探头**
负责，换个探头就得重标（步骤见 [../../docs/hardware_wiring.md](../../docs/hardware_wiring.md)
第 6.4 节）。而 ESP-IDF 不允许组件反向 include `main/` 的头文件，
所以端点是由 `main/plant_edge.c` 用 `soil_sensor_init(&soil_cal)`
**当参数传进驱动**的。

没有生成网络配置或 `local_config.h` 也能编过 —— 编译时会给一条 `#warning`，
WiFi / MQTT 或土壤标定端点会退回兜底值，固件只跑本地采样，连不上网。

> ⚠️ 连线不通时先查这两条：
> 1. **热点必须是 2.4GHz**（ESP32 看不到 5GHz，会一直 `reason=201`）；
> 2. 切换 WiFi 或热点重开后，重新运行 `start_dev.bat` 选择网络；它会读取电脑当前 IP，
>    两块板都要**重新 build + flash**。

### RGB 灯演示开关 `RGB_LED_DEMO_ENABLE`

在 `main/app_config.h` 里：

```c
#define RGB_LED_DEMO_ENABLE     0
```

> 重构后这个宏从 `main/plant_edge.c` 搬到了 `main/app_config.h`：
> **应用行为**类的配置集中放一处；而**驱动参数**（数据脚 GPIO 号、RMT 时基、
> 灯珠个数）留在 `components/rgb_led/rgb_led.c` 内部 —— 应用层不该知道自己
> 底下用的是 RMT 还是别的外设。

| 值 | 行为 |
|---|---|
| `1` | 彩色循环演示：每 500 ms 换色 `红 → 绿 → 蓝 → 青 → 品红 → 黄 → 白 → 灭`，心跳里带当前灯色 |
| `0` | **灭灯模式（当前默认）**：开机发一帧全 0 把灯锁存为"灭"，之后不再有任何动画，只保留每秒心跳 |

改完重新 `idf.py -p COM3 flash` 即可，两种模式共用同一份代码，不需要找回旧版本。

> 实现上故意用 `if` 而不是 `#if` 分流：`#if 0` 会把另一段代码整个跳过、编译器不检查它，
> 以后把宏改成 `1` 可能突然冒出一堆错误；用 `if` 则两个分支都参与编译检查，改宏值一定编得过。


## 目录结构

```
esp32s3_plant_edge/
├── CMakeLists.txt              # 顶层工程定义（组件依赖顺序由 IDF 自动解析）
├── sdkconfig.defaults          # 板级默认配置（Flash 8MB 等）
├── capture_serial.py           # 辅助脚本：抓几秒串口日志（见文末"调试小技巧"）
├── README.md
├── components/                 # 可复用模块（IDF 自动把这里当额外组件搜索路径）
│   ├── board_info/             # 板级信息：芯片 / Flash / MAC / 复位原因
│   │   ├── CMakeLists.txt
│   │   ├── include/board_info.h
│   │   └── board_info.c
│   ├── rgb_led/                # WS2812 灯驱动：位时序 / GRB 字节序 / RMT 通道
│   ├── i2c_bus/                # I2C 主机总线：SCL=GPIO5 / SDA=GPIO6 / 100kHz
│   │   └── 所有 I2C 器件共用这一条总线，别人只来"挂设备"
│   ├── light_sensor/           # BH1750 光照驱动（自写协议，不引外部依赖）
│   ├── soil_sensor/            # 电容式土壤湿度：ADC + 曲线校准 + 标定换算
│   ├── wifi_sta/               # WiFi Station 连接（自动重连）
│   └── mqtt_report/            # MQTT 上报：telemetry + status 两类报文
└── main/                       # 应用编排层
    ├── CMakeLists.txt
    ├── idf_component.yml       # 外部依赖清单（v6.1 的 MQTT 靠它自动下载）
    ├── app_config.h            # 应用行为配置（采样/上报周期、设备编号…）
    ├── generated_network_config.h # 启动脚本生成（.gitignore，不提交）
    ├── local_config.example.h  # 本机私有配置模板（提交）
    ├── local_config.h          # 土壤标定真实值（.gitignore，不提交）
    └── plant_edge.c            # app_main()：调模块、跑主循环
```

### 组件之间的依赖方向（单向，不成环）

```
main ──┬─> board_info
       ├─> rgb_led
       ├─> i2c_bus ──> light_sensor     (light_sensor 通过 i2c_bus 挂设备)
       ├─> soil_sensor                  (独立，不用 I2C)
       ├─> wifi_sta
       └─> mqtt_report ──> wifi_sta     (上报 status 时要读 RSSI)
```

> `main/` 的头文件（比如 `app_config.h`）**不能**被 `components/` 里的组件 include ——
> ESP-IDF 禁止"组件依赖 main"。所以 `wifi_sta_init()` 是**把 SSID/密码当参数传进去**的，
> 而不是自己去读配置。这既绕开了限制，也让驱动层不必知道"连哪个网"。

> `components/` 是 ESP-IDF 默认的额外组件搜索路径，**不需要**在顶层
> `CMakeLists.txt` 里写 `EXTRA_COMPONENT_DIRS`，头文件也不需要相对路径：
> 组件自己声明 `INCLUDE_DIRS "include"` 之后，直接 `#include "rgb_led.h"` 即可。

## 编译与烧录

先激活 ESP-IDF 环境（每次新开终端都要做一次）：

```powershell
. 'C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1'
```

然后：

```powershell
cd 'D:\Lab\Embedded Curriculum Design\pro\edge\esp32s3_plant_edge'
idf.py build                    # 只编译
idf.py -p COM3 flash            # 只烧录
idf.py -p COM3 monitor          # 只看串口日志（退出按 Ctrl + ]）
idf.py -p COM3 flash monitor    # 烧录 + 看日志
```

如果 `COM3` 变了（换 USB 口/重新插拔可能导致），用下面命令查：

```powershell
[System.IO.Ports.SerialPort]::GetPortNames()
```

### ⚠ Windows 上千万别这样看编译日志

```powershell
idf.py build 2>&1 | Select-Object -Last 20     # ❌ 会「假死」，别用
```

`Select-Object -Last N` 必须等上游管道**全部结束**，才知道"最后 N 行"是哪 N 行。
结果是：整个编译期间**屏幕不刷新、日志文件也一个字不写**。首次全量编译
（CMake configure 就要 ~62 s，ninja 还要跑 1000+ 个任务）看起来就像卡死了 ——
其实它一直在正常干活。

正确姿势有两个：

```powershell
# 姿势 1: 另存一份日志, 稍后用「另一条命令」看它的尾巴
idf.py build 2>&1 | Tee-Object -FilePath build.log
#   → 等这条跑完(或另开一个终端), 再执行:
Get-Content build.log -Tail 20

# 姿势 2: 直接读 ESP-IDF 自己逐步落盘的日志（★ 推荐, 实时更新, 不受管道影响）
Get-ChildItem build\log\idf_py_stdout_output_* | ForEach-Object {
    "--- $($_.Name) ---"; Get-Content $_.FullName -Tail 6 }
```

> 同理，**不要在同一条命令里**"发起编译 + `Start-Sleep 90` 干等"。
> 那 90 秒是纯浪费：让长任务自己跑，用独立的短命令轮询才对。

烧录/抓日志前记得关掉串口助手和别的 `monitor` 窗口 —— 一次性 `monitor` 会独占 `COM3`。

## 串口预期输出（当前为灭灯模式 `RGB_LED_DEMO_ENABLE=0`）

```
I (xxx) BOARD: ==================== System Info ====================
I (xxx) BOARD: Target chip  : esp32s3 (silicon rev v0.2)
I (xxx) BOARD: CPU          : 2 x Xtensa LX7 @ 160 MHz (max 240 MHz)
I (xxx) BOARD: Chip feature : WiFi BLE
I (xxx) BOARD: PSRAM        : disabled (module has 8 MB Octal PSRAM; see sdkconfig.defaults)
I (xxx) BOARD: Flash size   : 8 MB
I (xxx) BOARD: WiFi STA MAC : 14:C1:9F:38:AC:00
I (xxx) BOARD: Reset reason : POWERON   (power-up or EN/RST button pressed)
I (xxx) BOARD: Free heap    : xxxxx bytes
I (xxx) BOARD: IDF version  : v6.1
I (xxx) BOARD: =====================================================
I (xxx) RGB_LED: Init WS2812 RGB LED on GPIO38
I (xxx) APP: === LED demo mode OFF: RGB LED latched to OFF ===
I (xxx) APP: uptime =    1 s | free heap = 388208 B | RGB LED = OFF (latched)
I (xxx) APP: uptime =    2 s | free heap = 388208 B | RGB LED = OFF (latched)
I (xxx) APP: uptime =    3 s | free heap = 388208 B | RGB LED = OFF (latched)
```

> **日志前缀就是模块名（TAG）**，据此就能判断消息来自哪个模块：
> `BOARD` ← `components/board_info/`、`RGB_LED` ← `components/rgb_led/`、
> `APP` ← `main/`。模块化之后排查问题更省事：看到前缀就知道该翻哪个文件。

把宏改成 `RGB_LED_DEMO_ENABLE 1` 重烧后，最后几行变成：

```
I (xxx) APP: === LED demo mode ON: color changes every 500 ms ===
I (xxx) APP: uptime =    1 s | free heap = 388208 B | LED = GREEN   (R=  0 G=255 B=  0)
```

## 关键知识点（踩坑记录）

### 1. 板载 RGB 灯不能用 `gpio_set_level()` 点亮

它是 WS2812 **可寻址灯珠**，颜色要用单总线波形编码，靠"高电平持续时间"区分 0 和 1：

| 位 | 高电平 | 低电平 |
|---|---|---|
| `0` | 0.3 µs | 0.9 µs |
| `1` | 0.9 µs | 0.3 µs |
| 复位 | 低电平 > 50 µs | — |

精度要求 0.3 µs，软件延时（`vTaskDelay`、`esp_rom_delay_us`）都做不到，必须用 **RMT 外设**。
本工程用 `rmt_new_simple_encoder()` + 编码回调，把每个字节展开成 8 个 RMT 符号。

### 2. WS2812 的字节顺序是 GRB，不是 RGB

代码里必须写成：

```c
pixels[0] = green;
pixels[1] = red;
pixels[2] = blue;
```

写反了会"想要红色却亮绿色"。

### 3. GPIO38 而不是 GPIO48

v1.0 是 GPIO48，v1.1 改成了 GPIO38。原因：v1.1 用的芯片把 `VDD_SPI` 设为 1.8 V，
同电源域的 GPIO47/GPIO48 也变成 1.8 V 电平，所以灯挪到了 3.3 V 域。

### 4. 8 线 PSRAM 模组的 GPIO35/36/37 不可用

本模组内封 Octal PSRAM，占用了 GPIO35/36/37，不能当普通 IO 用。

### 5. 想"关掉"WS2812 必须主动发一帧全 0，不能只是不发数据

WS2812 灯珠内部有**锁存电路 + 恒流驱动**，会一直保持最后一次收到的 24 bit 颜色：

| 做法 | 结果 |
|---|---|
| 删掉 `rgb_led_set()` 调用 | ❌ 灯保持上一次的颜色（多半还是亮的） |
| 按 EN 复位 / 重新烧录 | ❌ 复位瞬间 GPIO38 没有有效波形，锁存内容不变 |
| 拔 USB 断电 | ✅ 会灭，但这不是软件能控制的做法 |
| `rgb_led_set(0, 0, 0)` | ✅ **正确做法**：发一帧全 0，把灯珠内部记忆改写成"全灭" |

所以灭灯模式里这句话是**必须**的：

```c
ESP_ERROR_CHECK(rgb_led_off());   /* 发一帧全 0 → 灯珠锁存为"灭" */
```

> 顺带一提：烧录过程中 esptool 会把芯片复位进下载模式，那几秒 GPIO38 没有信号，
> 灯会**短暂保持上一次的颜色**，直到新固件跑起来发出那帧全 0 才熄灭。这是正常现象，不是 bug。


## 模块化设计

### 分层与依赖方向

```
                     main/                          ← 编排层：决定"干什么"
               ┌──────────────────┐
               │   app_main()     │
               └────────┬─────────┘
                        │ 调用
          ┌─────────────┴─────────────┐
          ▼                           ▼
  components/rgb_led/        components/board_info/   ← 驱动/服务层：决定"怎么干"
   （WS2812 波形）              （板级信息打印）
```

依赖**单向向下**：`main` 依赖组件，组件之间互不依赖（`rgb_led` 和 `board_info`
之间没有任何 `#include`）。这样任何一个组件都能整包复制到别的工程。

### 两个组件的职责边界

| 组件 | 对外接口 | 内部实现（不外露） |
|---|---|---|
| `rgb_led` | `rgb_led_init()`<br>`rgb_led_set(r, g, b)`<br>`rgb_led_off()` | 数据脚 GPIO 号、RMT 时基、位时序常量、编码回调、RMT 句柄 |
| `board_info` | `board_info_print()` | 输出格式、复位原因字符串表 |

原则是：**只暴露"能做什么"，隐藏"怎么做"。**
打开 `rgb_led.h`，里面看不到任何一个 `rmt_` 开头的符号。将来若要换成 SPI 接口的
APA102，只要新的 `rgb_led.c` 仍然提供这三个函数，`main` 一行都不用改。

### 为什么用 `PRIV_REQUIRES` 而不是 `REQUIRES`

`PRIV_REQUIRES` 表示"只有我自己需要这个依赖"，**不会传染**给依赖我的组件。
`main` 用了 `rgb_led`，但 `rgb_led.h` 没有暴露任何 RMT 类型，所以
`esp_driver_rmt` 只需声明在 `rgb_led` 组件内部，`main` 根本不用知道它存在。

验证办法：重构后 `main/CMakeLists.txt` 里已经看不到 `esp_driver_rmt` 了 ——
**这行 CMake 就是"分层成功"的证据**。

### 如何新增一个模块（三步）

```powershell
# 1) 建目录
mkdir components/xxx/include
```

```c
/* 2) 写 xxx.h（只放接口声明）和 xxx.c（实现放这里，细节用 static 藏起来） */
```

```cmake
# 3) components/xxx/CMakeLists.txt
idf_component_register(SRCS "xxx.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES 它需要的其他组件)
```

最后在 `main/CMakeLists.txt` 的 `PRIV_REQUIRES` 里加上 `xxx`，就能
`#include "xxx.h"` 了。

### 下一批模块（本工程是"智能植物养护系统"的地基）

| 层次 | 组件 | 职责 |
|---|---|---|
| 通信 | `wifi_manager` | STA 连接、断线重连、IP 事件 |
| 通信 | `mqtt_client` | 上报遥测、订阅控制指令 |
| 服务 | `nvs_store` | 持久化 WiFi 配置、阈值、喷药计数 |
| 驱动 | `sensor_soil` / `sensor_env` | 土壤湿度 / 盐分 / 温度 / 光照 |
| 驱动 | `actuator_pump` / `actuator_light` / `actuator_spray` | 水泵 / 补光灯 / 喷雾 |
| 业务 | `plant_ctrl` | 闭环控制 + 安全保护 |

> 提前记一条设计纪律：**安全逻辑必须放在边缘端本地**。
> 例如"泵单次最多转 N 秒""喷雾一天最多 K 次"这类保护要写在 `plant_ctrl` 里，
> 而不是只靠后端下发的指令 —— 断网时系统也必须是安全的。

## 后续可做

- **开启 PSRAM**：把 `sdkconfig.defaults` 里 `CONFIG_SPIRAM*` 三行取消注释，然后
  `idf.py fullclean ; idf.py build`（首次建议单独验证能否正常启动）。
- **按键输入**：板上 BOOT 键接 GPIO0，注意它是 strapping 引脚，有 ~45 kΩ 内部弱上拉。

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
