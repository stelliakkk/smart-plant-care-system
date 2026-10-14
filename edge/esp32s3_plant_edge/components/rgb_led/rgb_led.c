/*
 * =====================================================================
 *  板载 WS2812 可寻址 RGB 灯驱动 (基于 RMT 外设)
 * =====================================================================
 *  目标开发板 : ESP32-S3-DevKitC-1 v1.1 (模组 ESP32-S3-WROOM-1)
 *  对外接口   : rgb_led_init() / rgb_led_set() / rgb_led_off()
 *
 *  ---------------------------------------------------------------
 *  硬件避坑指南 (这几条是新手最容易卡住的地方):
 *
 *  [1] 灯珠数据脚是 GPIO38
 *      DevKitC-1 v1.0 是 GPIO48, v1.1 改成了 GPIO38。
 *      原因: v1.1 用的 ESP32-S3R8V 把 VDD_SPI 设成了 1.8V, 同电源域的
 *      GPIO47/GPIO48 也就变成 1.8V 电平, 所以厂家把灯挪到了 3.3V 域的 38。
 *
 *  [2] 这颗灯是"可寻址灯珠" (WS2812 协议), 不是普通 LED
 *      普通 LED 用 gpio_set_level() 给高电平就亮; 而 WS2812 要把颜色编码
 *      成一种单总线波形, 靠"高电平持续时间"区分 0 和 1, 精度要求 0.3us
 *      级别, 软件延时 (vTaskDelay / esp_rom_delay_us) 根本做不到, 必须用
 *      RMT 外设:
 *          bit 0 -> 高 0.3us + 低 0.9us
 *          bit 1 -> 高 0.9us + 低 0.3us
 *          复位   -> 低电平 > 50us
 *
 *  [3] 字节顺序是 G-R-B (绿在前), 不是常见的 R-G-B
 *      写反了就会出现"想要红色却亮了绿色"。
 *
 *  [4] 灯珠内部有锁存电路, 会一直保持最后一次收到的颜色!
 *      所以"停止发数据"和"复位芯片"都不会让它灭 —— 必须主动发一帧全 0
 *      (即 rgb_led_off()), 把它的内部记忆改写成"全灭"。
 *
 *  [5] 不要再用 gpio_set_level() 去拉这个脚
 *      rmt_enable() 之后 GPIO38 已归 RMT 外设管理, 手工拉会和 RMT 打架。
 *
 *  [6] 发送完必须等待
 *      rmt_transmit() 是"立即返回、后台发送", 而像素缓冲是栈上局部数组,
 *      不等待函数就返回会导致发出发送一半的脏数据, 灯会闪错色。
 * =====================================================================
 */

#include "freertos/FreeRTOS.h"   /* portMAX_DELAY */
#include "esp_check.h"           /* ESP_RETURN_ON_ERROR / ESP_RETURN_ON_FALSE */
#include "esp_log.h"
#include "driver/rmt_tx.h"

#include "rgb_led.h"

static const char *TAG = "RGB_LED";

/* ------------------------------ 驱动参数 ------------------------------ */

/* 板载 WS2812 数据脚。换到别的板子只改这一个宏 */
#define RGB_LED_GPIO            38

/* 板上只有 1 颗灯珠 */
#define RGB_LED_COUNT           1

/* RMT 时基 = 10MHz, 即 1 tick = 0.1us
 * WS2812 要求 0.3us 级精度, 所以时基必须足够高 */
#define RMT_RESOLUTION_HZ       10000000

/* RMT 硬件 FIFO 块大小 (符号数)。适当调大可以让灯更稳定不闪 */
#define RMT_MEM_BLOCK_SYMBOLS   64

/* ------------------------- WS2812 位时序定义 ------------------------- */

/* 逻辑 0: 高电平 0.3us + 低电平 0.9us */
static const rmt_symbol_word_t WS2812_BIT0 = {
    .level0 = 1, .duration0 = 0.3 * RMT_RESOLUTION_HZ / 1000000,
    .level1 = 0, .duration1 = 0.9 * RMT_RESOLUTION_HZ / 1000000,
};

/* 逻辑 1: 高电平 0.9us + 低电平 0.3us */
static const rmt_symbol_word_t WS2812_BIT1 = {
    .level0 = 1, .duration0 = 0.9 * RMT_RESOLUTION_HZ / 1000000,
    .level1 = 0, .duration1 = 0.3 * RMT_RESOLUTION_HZ / 1000000,
};

/* 帧尾复位信号: 低电平 50us, 拆成两个符号字段拼够时长 */
static const rmt_symbol_word_t WS2812_RESET = {
    .level0 = 0, .duration0 = RMT_RESOLUTION_HZ / 1000000 * 50 / 2,
    .level1 = 0, .duration1 = RMT_RESOLUTION_HZ / 1000000 * 50 / 2,
};

/**
 * @brief RMT "简单编码器"回调: 把 1 个字节展开成 8 个 WS2812 位符号
 *
 * 发送一整帧时, RMT 驱动会反复调用本函数, 直到 *done 被置为 true。
 *
 * @param data             待发送的原始字节 (即 GRB 像素缓冲)
 * @param data_size        原始字节总数
 * @param symbols_written  已经写进 FIFO 的符号数 (1 字节 = 8 个符号)
 * @param symbols_free     FIFO 里还能容纳几个符号
 * @param symbols          [输出] 编码好的符号写到这里
 * @param done             [输出] 置 true 表示整帧编码结束
 * @param arg              用户参数 (本驱动不用)
 *
 * @return 本次实际写入的符号个数
 */
static size_t ws2812_encode_cb(const void *data, size_t data_size,
                               size_t symbols_written, size_t symbols_free,
                               rmt_symbol_word_t *symbols, bool *done, void *arg)
{
    /* 编 1 个字节要占 8 个符号位置; 空间不足就返回 0, 让驱动稍后再调 */
    if (symbols_free < 8) {
        return 0;
    }

    /* 由"已写符号数 / 8"反推当前编到第几个字节 */
    size_t data_pos = symbols_written / 8;
    const uint8_t *bytes = (const uint8_t *)data;

    if (data_pos < data_size) {
        size_t n = 0;
        /* WS2812 是 MSB first, 先发最高位 */
        for (int mask = 0x80; mask != 0; mask >>= 1) {
            symbols[n++] = (bytes[data_pos] & mask) ? WS2812_BIT1 : WS2812_BIT0;
        }
        return n;
    }

    /* 所有颜色字节都发完了, 补一个复位信号并结束本帧 */
    symbols[0] = WS2812_RESET;
    *done = true;
    return 1;
}

/* ------------------------------ 驱动实现 ------------------------------ */

/* RMT 发送通道和编码器句柄, 初始化后一直复用 */
static rmt_channel_handle_t s_led_chan = NULL;
static rmt_encoder_handle_t s_led_encoder = NULL;

esp_err_t rgb_led_init(void)
{
    /* 防呆: 重复初始化会重复创建 RMT 通道, 白白泄漏内存 */
    if (s_led_chan != NULL) {
        ESP_LOGW(TAG, "already initialized, skip");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Init WS2812 RGB LED on GPIO%d", RGB_LED_GPIO);

    /* 一个 RMT TX 通道 = 一路可以独立发送波形的外设 */
    rmt_tx_channel_config_t tx_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,      /* 用默认时钟源 */
        .gpio_num = RGB_LED_GPIO,            /* 波形从哪个脚输出 */
        .mem_block_symbols = RMT_MEM_BLOCK_SYMBOLS,
        .resolution_hz = RMT_RESOLUTION_HZ,  /* 10MHz, 1 tick = 0.1us */
        .trans_queue_depth = 4,              /* 后台可排队的发送事务数 */
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_cfg, &s_led_chan),
                        TAG, "create RMT TX channel failed");

    /* 挂上前面写的编码器回调, 之后交给 RMT 驱动自动调用 */
    rmt_simple_encoder_config_t enc_cfg = {
        .callback = ws2812_encode_cb,
    };
    ESP_RETURN_ON_ERROR(rmt_new_simple_encoder(&enc_cfg, &s_led_encoder),
                        TAG, "create simple encoder failed");

    /* 最后使能通道, 使能后 GPIO38 就由 RMT 接管了 */
    ESP_RETURN_ON_ERROR(rmt_enable(s_led_chan), TAG, "enable RMT TX channel failed");
    return ESP_OK;
}

esp_err_t rgb_led_set(uint8_t red, uint8_t green, uint8_t blue)
{
    /* 没初始化就发数据会直接崩, 这里提前拦住并给出明确错误码 */
    ESP_RETURN_ON_FALSE(s_led_chan != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "not initialized, call rgb_led_init() first");

    /* WS2812 每个灯珠 24 bit = 3 字节 */
    uint8_t pixels[RGB_LED_COUNT * 3];

    /* 关键: WS2812 的字节顺序是 G-R-B, 不是 R-G-B!
     * 这里若把 red 写在 [0] 位置, 想要红色就会变成绿色 */
    for (int i = 0; i < RGB_LED_COUNT; i++) {
        pixels[i * 3 + 0] = green;
        pixels[i * 3 + 1] = red;
        pixels[i * 3 + 2] = blue;
    }

    rmt_transmit_config_t tx_cfg = {
        .loop_count = 0,   /* 0 = 只发送一次, 不循环 */
    };

    /* rmt_transmit() 是"立即返回、后台发送", 所以下面必须等待发完,
     * 否则 pixels 是栈上数组, 函数一返回内存就可能被覆盖, 灯会闪错色 */
    ESP_RETURN_ON_ERROR(rmt_transmit(s_led_chan, s_led_encoder, pixels, sizeof(pixels), &tx_cfg),
                        TAG, "RMT transmit failed");
    return rmt_tx_wait_all_done(s_led_chan, portMAX_DELAY);
}

esp_err_t rgb_led_off(void)
{
    return rgb_led_set(0, 0, 0);
}
