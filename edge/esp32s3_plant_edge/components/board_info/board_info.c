/*
 * 板级信息打印
 * =====================================================================
 * 内容: 芯片型号/核心数/硅片版本/特性、PSRAM 状态、Flash 容量、
 *       WiFi STA MAC、复位原因、空闲堆、IDF 版本
 *
 * 注意 PSRAM 这一项的判断方式:
 *   不能用 esp_chip_info() 的 CHIP_FEATURE_EMB_PSRAM 位来判断!
 *   那位表示"PSRAM 已被程序启用", 而不是"模组焊了 PSRAM",
 *   直接看编译期配置 CONFIG_SPIRAM 结论才最准确。
 * =====================================================================
 */

#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_mac.h"
#include "esp_flash.h"

#include "board_info.h"

static const char *TAG = "BOARD";

/* PSRAM 状态字符串 */
#if CONFIG_SPIRAM
#define PSRAM_STATUS_STR        "enabled (8 MB Octal, 3.3V)"
#else
#define PSRAM_STATUS_STR        "disabled (module has 8 MB Octal PSRAM; see sdkconfig.defaults)"
#endif

/**
 * @brief 把复位原因枚举转成可读字符串
 */
static const char *reset_reason_str(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON:   return "POWERON   (power-up or EN/RST button pressed)";
    case ESP_RST_EXT:       return "EXT       (external reset pin)";
    case ESP_RST_SW:        return "SW        (esp_restart called)";
    case ESP_RST_PANIC:     return "PANIC     (exception or abort)";
    case ESP_RST_INT_WDT:   return "INT_WDT   (interrupt watchdog timeout)";
    case ESP_RST_TASK_WDT:  return "TASK_WDT  (task watchdog timeout)";
    case ESP_RST_WDT:       return "WDT       (other watchdog reset)";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP (wake up from deep sleep)";
    case ESP_RST_BROWNOUT:  return "BROWNOUT  (supply voltage dropped)";
    case ESP_RST_SDIO:      return "SDIO      (reset by SDIO)";
    default:                return "UNKNOWN";
    }
}

void board_info_print(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);   /* 传 NULL 表示主 Flash */

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    ESP_LOGI(TAG, "==================== System Info ====================");
    ESP_LOGI(TAG, "Target chip  : %s (silicon rev v%d.%d)",
             CONFIG_IDF_TARGET, chip.revision / 100, chip.revision % 100);
    ESP_LOGI(TAG, "CPU          : %d x Xtensa LX7 @ %d MHz (max 240 MHz)",
             chip.cores, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    ESP_LOGI(TAG, "Chip feature :%s%s%s",
             (chip.features & CHIP_FEATURE_WIFI_BGN) ? " WiFi" : "",
             (chip.features & CHIP_FEATURE_BLE) ? " BLE" : "",
             (chip.features & CHIP_FEATURE_IEEE802154) ? " 802.15.4" : "");
    ESP_LOGI(TAG, "PSRAM        : %s", PSRAM_STATUS_STR);
    ESP_LOGI(TAG, "Flash size   : %u MB", (unsigned)(flash_size / (1024 * 1024)));
    ESP_LOGI(TAG, "WiFi STA MAC : %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "Reset reason : %s", reset_reason_str(esp_reset_reason()));
    ESP_LOGI(TAG, "Free heap    : %u bytes", (unsigned)esp_get_free_heap_size());
    ESP_LOGI(TAG, "IDF version  : %s", esp_get_idf_version());
    ESP_LOGI(TAG, "=====================================================");
}
