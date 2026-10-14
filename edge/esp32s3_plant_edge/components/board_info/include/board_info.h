/*
 * 板级信息模块
 * ---------------------------------------------------------------------
 * 负责把"这块板子是谁、装了多少 Flash、网卡地址是多少、上次为什么重启"
 * 这类开机自检信息打印出来。与具体业务无关, 可以直接搬到别的工程复用。
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 打印芯片 / Flash / MAC / 复位原因 / 空闲堆等开机信息
 *
 * 输出统一使用英文, 避免某些串口助手用 GBK 解码时把中文显示成乱码。
 */
void board_info_print(void);

#ifdef __cplusplus
}
#endif
