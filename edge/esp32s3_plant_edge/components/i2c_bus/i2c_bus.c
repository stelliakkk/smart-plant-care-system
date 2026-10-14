/*
 * =====================================================================
 *  I2C 主机总线 (给所有 I2C 传感器共用)
 * =====================================================================
 *  目标开发板 : ESP32-S3-DevKitC-1 v1.1 (模组 ESP32-S3-WROOM-1)
 *  占用引脚   : SCL = GPIO5, SDA = GPIO6
 *
 *  ---------------------------------------------------------------
 *  硬件避坑指南:
 *
 *  [1] 为什么挑 5/6 这两个脚?
 *      ESP32-S3 的 GPIO 不是每个都能随便用, 派脚前必须先排雷:
 *        GPIO26~32  —— 接模组内部 Flash/PSRAM, 碰了直接起不来;
 *        GPIO33~37  —— 被模组内部占用, 同样禁用;
 *        GPIO19/20  —— 原生 USB(D-/D+), 板子上就引到 USB 口了;
 *        GPIO0/3/45/46 —— strapping 脚, 上电瞬间的电平决定启动模式,
 *                          外接器件可能把它拉错, 导致进不了正常启动;
 *        GPIO43/44  —— UART0, 我们全靠它看串口日志。
 *      GPIO5/6 不在上面任何一条"雷区"里, 拿来当 I2C 最安全。
 *
 *  [2] I2C 必须有上拉电阻, 否则总线永远是低的
 *      I2C 两根线是"开漏"结构: 器件只能把线拉低, 想让它变高得靠上拉
 *      电阻。多数成品模块(GY-302 这类)板上已焊了 4.7k 上拉, 直接用就行;
 *      如果你买的是光板芯片, 必须自己补电阻。
 *      下面开的 enable_internal_pullup 是 ESP32 内部弱上拉(约 45k),
 *      只够应急, 不能指望它替代外部上拉。
 *
 *  [3] 速率故意取 100kHz 而不是 400kHz
 *      BH1750 支持到 400kHz, 但杜邦线 + 面包板这种接法寄生电容大,
 *      400kHz 下容易偶发 NACK(读着读着突然失败)。100kHz 慢一点但稳得多,
 *      而对"每秒读一次光照"这种需求, 100kHz 快得用不完。
 *
 *  [4] SDA / SCL 接反是最经典的新手错误
 *      现象是"probe 不到设备", 报错信息一模一样。接线前先对一遍。
 * =====================================================================
 */

#include "esp_check.h"
#include "esp_log.h"

#include "i2c_bus.h"

static const char *TAG = "I2C_BUS";

/* ------------------------------ 驱动参数 ------------------------------ */

/* 两根线占用的 GPIO。换接线只改这两个宏 */
#define I2C_BUS_SCL_GPIO        5
#define I2C_BUS_SDA_GPIO        6

/* 总线速率见 i2c_bus.h 的 I2C_BUS_SPEED_HZ (挂总线的传感器也要用) */

/* 毛刺滤波: 短于这个时钟数的干扰脉冲会被滤掉, 官方推荐值 7 */
#define I2C_BUS_GLITCH_CNT      7

/* ------------------------------ 驱动实现 ------------------------------ */

/* 总线句柄: 初始化后一直复用, 所有 I2C 器件共用这一条总线 */
static i2c_master_bus_handle_t s_bus = NULL;

esp_err_t i2c_bus_init(void)
{
    /* 防呆: 重复建总线会重复占 I2C 外设, 白白漏内存 */
    if (s_bus != NULL) {
        ESP_LOGW(TAG, "already initialized, skip");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Init I2C master bus: SCL=GPIO%d, SDA=GPIO%d, %d Hz",
             I2C_BUS_SCL_GPIO, I2C_BUS_SDA_GPIO, I2C_BUS_SPEED_HZ);

    i2c_master_bus_config_t cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = -1,                       /* -1 = 让驱动自动挑一个空闲的 I2C 控制器 */
        .scl_io_num = I2C_BUS_SCL_GPIO,
        .sda_io_num = I2C_BUS_SDA_GPIO,
        .glitch_ignore_cnt = I2C_BUS_GLITCH_CNT,
        /* 内部弱上拉: 模块自带 4.7k 时它无害, 用手焊光板芯片时能救急 */
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg, &s_bus), TAG,
                        "create I2C master bus failed");
    return ESP_OK;
}

i2c_master_bus_handle_t i2c_bus_get_handle(void)
{
    return s_bus;
}