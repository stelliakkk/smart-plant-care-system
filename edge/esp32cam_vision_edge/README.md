# ESP32-CAM 高清拍照节点

AI Thinker 风格 ESP32-CAM + OV2640，目标芯片为原始 ESP32。
当前配置：1600×1200（UXGA）、JPEG quality 12、单 PSRAM 帧缓冲，无实时预览。

`GET /capture` 丢弃两帧后返回一张新 JPEG。CAM 每 3 秒向 `plant/cam01/status` 发布 Wi-Fi 状态与 `http://<CAM-IP>/capture`，stream_url 为空。
后端每 300 秒抓拍、保存照片、调用 DeepSeek；手动按钮执行相同流程。照片与结果保留 7 天，API Key 只放在后端 `.env`。

CAM 不通过烧录座 USB 输出图像。USB 用于供电、UART 烧录与日志；图像通过 Wi-Fi/HTTP 传输。
S3（edge01）负责传感器和执行器，CAM（cam01）负责照片。两者与后端处于可互通的局域网。

从根目录 start_dev.bat 选择网络后，main/generated_network_config.h 会更新 Wi-Fi 与 MQTT 地址。必须重新编译并烧录才能更新板上配置。

编译：在 ESP-IDF 环境、本目录执行 `idf.py build`。
后续烧录：确认 GPIO0 接 GND 后按 RST，执行 `idf.py -p COMx flash`；完成后断开 GPIO0 与 GND 再复位。

稳定供电建议 5V/1A；不要用 S3 的 3.3V 引脚给 CAM 供电。
完整接口、配置、测试与限制见 ../../docs/camera_vision_demo.md（项目根目录 docs 下）。
