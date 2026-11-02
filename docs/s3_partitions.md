# ESP32-S3 应用分区扩容

当前S3为8 MiB Flash；使用edge/esp32s3_plant_edge/partitions.csv自定义单应用分区。sdkconfig与sdkconfig.defaults均启用CONFIG_PARTITION_TABLE_CUSTOM，保持新建构建与现有构建一致。

| 分区 | 起始地址 | 大小 | 结束地址（不包含） |
|---|---|---|---|
| 分区表 | 0x8000 | 所在扇区0x1000 | 0x9000 |
| nvs | 0x9000 | 0x6000 | 0xf000 |
| phy_init | 0xf000 | 0x1000 | 0x10000 |
| factory | 0x10000 | 0x200000（2 MiB） | 0x210000 |

旧应用分区1 MiB→2 MiB，NVS、PHY和应用起始地址不变，保留现有NVS设置的布局；本轮未擦除设备或烧录。其余Flash没有分配为文件系统或OTA分区，不代表可直接存放文件。照片保存在电脑后端，不占S3应用分区。

## 编译与后续烧录

在S3目录运行ESP-IDF6.0.1的idf.py build，构建会检查分区重叠、Flash边界及固件大小。编译后build/partition_table/partition-table.bin是新2 MiB表。

后续COM4原生USB JTAG烧录应包括bootloader、分区表和应用，例如OpenOCD board/esp32s3-builtin.cfg执行program_esp_bins . flasher_args.json verify reset exit（工作目录build）。不要只写应用而保留板内旧1 MiB分区表；也不需要为本扩容主动erase_flash。烧录并校验后确认启动、联网和timed_pumps_ready，再启用清水演示。

当前固件约0.953 MiB，2 MiB应用分区约剩1.05 MiB（52%），精确大小以最终构建为准。清理上电测试是源码整理，旧测试此前已被链接器剔除，扩容带来的余量来自分区增大。

当前仍是单factory应用，没有OTA；将来无线升级需重新规划双应用分区与otadata，不能仅打开一个选项。NVS布局保留不代表意外断电/硬件故障下数据绝对不会丢失。
