/*
 * =====================================================================
 *  BH1750 数字光照传感器驱动 (I2C)
 * =====================================================================
 *  目标开发板 : ESP32-S3-DevKitC-1 v1.1
 *  常见模块   : GY-302 (板上已焊好上拉电阻与去耦电容, 插线即用)
 *  I2C 地址   : 0x23 (ADDR 悬空/接地) 或 0x5C (ADDR 接 VCC)
 *
 *  ---------------------------------------------------------------
 *  为什么自己写驱动, 不去组件市场下一个?
 *    BH1750 的协议就 3 条命令 + 读 2 个字节 + 除以 1.2, 一共不到 50 行。
 *    为这点代码引一个外部依赖, 反而多出"版本/下载/兼容"的心智负担。
 *    自己写一遍, 你才算真的看懂它是怎么工作的。
 *
 *  ---------------------------------------------------------------
 *  协议速查(来自 BH1750FVI 数据手册):
 *    写 0x01 → 上电(Power On), 器件进入待机等待测量命令
 *    写 0x10 → 连续高分辨率模式: 每 ~120ms 自动更新一次内部数据
 *    写 0x20 → 单次高分辨率模式: 测一次就自动断电, 结果保留可读
 *    读 2 字节 → 高字节在前, 换算公式: lux = 原始值 / 1.2
 *
 *  本驱动选"单次高分辨率模式"(0x20):
 *    每次读都触发一次全新测量, 拿到的永远是新鲜值, 不会读到几秒前的
 *    陈旧数据; 代价是每次要等约 180ms —— 对每秒采样一次毫无压力。
 *
 *  ---------------------------------------------------------------
 *  避坑指南:
 *   [1] 单次测量最多要等 180ms 才能读!
 *       手册写明测量时间"典型 120ms、最大 180ms"。等短了会读到上次的
 *       残留值甚至全 0, 别图快把它改成 50ms。
 *   [2] 读回来是"高字节在前"的大端序。高低字节写反的话,
 *       500 lux 会变成 128000 lux 这种离谱数字。
 *   [3] BH1750 是 3.3V 器件。GY-302 板上带 LDO, 接 5V 也能工作;
 *       但裸芯片必须 3.3V, 否则 I2C 电平对不上、通信会随机失败。
 *   [4] probe 不到时先查 ADDR: 接 VCC 时地址是 0x5C, 不是 0x23。
 * =====================================================================
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_log.h"

#include "i2c_bus.h"
#include "light_sensor.h"

static const char *TAG = "LIGHT";

/* ------------------------------ 驱动参数 ------------------------------ */

/* 7 位从机地址。ADDR 接 VCC 的模块要改成 0x5C */
#define BH1750_ADDR               0x23

/* BH1750 命令字 */
#define BH1750_CMD_POWER_ON       0x01
#define BH1750_CMD_ONETIME_H_RES  0x20

/* 单次测量等待时间(ms): 手册典型 120ms / 最大 180ms, 取最大值最稳 */
#define BH1750_MEASURE_WAIT_MS    180

/* I2C 传输超时(ms): 100kHz 下传 2 字节不到 1ms, 给 100ms 绰绰有余 */
#define BH1750_XFER_TIMEOUT_MS    100

/* 换算系数: 数据手册给定的 lux = raw / 1.2 */
#define BH1750_LUX_DIVISOR        1.2f

/* ------------------------------ 内部状态 ------------------------------ */

/* 器件句柄: 初始化后一直复用 */
static i2c_master_dev_handle_t s_dev = NULL;

/* ------------------------------ 驱动实现 ------------------------------ */

bool light_sensor_is_ready(void)
{
    return s_dev != NULL;
}

esp_err_t light_sensor_init(void)
{
    /* 防呆: 重复初始化会重复往总线上挂同一个地址, 白白漏内存 */
    if (s_dev != NULL) {
        ESP_LOGW(TAG, "already initialized, skip");
        return ESP_OK;
    }

    i2c_master_bus_handle_t bus = i2c_bus_get_handle();
    ESP_RETURN_ON_FALSE(bus != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "I2C bus not ready, call i2c_bus_init() first");

    /* 先"探活": 发一个只带地址的空帧, 器件在位才会回 ACK。
     * 这一步把"接线/上拉/地址不对"和"程序写错"当场区分开 ——
     * 报错会直接指向硬件, 省掉大量瞎猜时间。 */
    esp_err_t err = i2c_master_probe(bus, BH1750_ADDR, BH1750_XFER_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BH1750 不在 0x%02X (%s)。请检查: SDA=GPIO6 / SCL=GPIO5 是否接反、"
                      "VCC 是否接在 3.3V、ADDR 是否被接到了 VCC(那样地址会变成 0x5C)",
                 BH1750_ADDR, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "BH1750 found at 0x%02X", BH1750_ADDR);

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BH1750_ADDR,
        .scl_speed_hz = I2C_BUS_SPEED_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &s_dev), TAG,
                        "add BH1750 to I2C bus failed");

    /* 上电(0x01)。按手册, 单次测量命令本身也会唤醒器件; 这里显式发一次
     * 是为了让它从"上电后待机"进入确定状态, 顺便验证命令也能被 ACK。 */
    uint8_t cmd = BH1750_CMD_POWER_ON;
    err = i2c_master_transmit(s_dev, &cmd, 1, BH1750_XFER_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "power on BH1750 failed: %s", esp_err_to_name(err));
        i2c_master_bus_rm_device(s_dev);   /* 回滚: 别把半残的器件留在总线上 */
        s_dev = NULL;
        return err;
    }

    ESP_LOGI(TAG, "BH1750 ready (单次高分辨率模式, 1 lx, 每次约 %d ms)",
             BH1750_MEASURE_WAIT_MS);
    return ESP_OK;
}

esp_err_t light_sensor_read_lux(float *out_lux)
{
    ESP_RETURN_ON_FALSE(out_lux != NULL, ESP_ERR_INVALID_ARG, TAG, "out_lux is NULL");
    ESP_RETURN_ON_FALSE(s_dev != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "not initialized, call light_sensor_init() first");

    /* 1) 触发一次单次高分辨率测量 */
    uint8_t cmd = BH1750_CMD_ONETIME_H_RES;
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_dev, &cmd, 1, BH1750_XFER_TIMEOUT_MS),
                        TAG, "send one-time measure command failed");

    /* 2) 等测量完成。手册写明"典型 120ms / 最大 180ms",
     *    等短了会读到上一次的残留值甚至全 0。 */
    vTaskDelay(pdMS_TO_TICKS(BH1750_MEASURE_WAIT_MS));

    /* 3) 读回 2 字节。BH1750 是"高字节在前"的大端序, 别写反 */
    uint8_t buf[2] = { 0, 0 };
    ESP_RETURN_ON_ERROR(i2c_master_receive(s_dev, buf, sizeof(buf), BH1750_XFER_TIMEOUT_MS),
                        TAG, "read BH1750 data failed");

    uint16_t raw = (uint16_t)((buf[0] << 8) | buf[1]);

    /* 4) 换算成 lux。单次高分辨率模式下 1 个计数 = 1/1.2 lux */
    *out_lux = (float)raw / BH1750_LUX_DIVISOR;
    return ESP_OK;
}
