# ESP32-CAM 高清拍照与 DeepSeek

CAM为原始ESP32 / AI Thinker式OV2640，不是S3；节点cam01，只提供Wi-Fi HTTP /capture，UXGA 1600×1200、JPEG quality=12、一个PSRAM帧缓冲，丢弃两帧后取新帧。每3秒MQTT发布snapshot_url，stream_url为空；USB仅供电/下载/日志，不传网页图像。

后端默认每300秒下载JPEG，检查格式、≤8MB、≤2MP，保存backend/data/images/vision_*.jpg，调用官方DeepSeek图像接口。API Key仅存backend/.env，不进入前端/固件/数据库/日志。手动抓拍与定时分析串行；清理维护不会正常跳过每隔一次拍照。后端关闭时不拍照，恢复后不补历史照片。

配置：DEEPSEEK_API_KEY、DEEPSEEK_BASE_URL=https://api.deepseek.com、DEEPSEEK_MODEL=deepseek-flash、VISION_INTERVAL_SEC=300、VISION_RETENTION_DAYS=7、CAMERA_HTTP_TIMEOUT_SEC=20。更新.env后重启后端。模型实际图像支持以所用账号/官方接口为准，API失败保留已拍照片及可读错误。

任务状态capturing→analyzing→done/error；JSON结果含has_issue、diagnosis、visible_signs、possible_causes、advice、uncertainty、pest_status、pest_evidence。pest_status为detected/suspected/none/unknown，缺字段的旧结果视为unknown；不根据has_issue直接喷药。

照片和结果保留滚动7天，启动及每10分钟清理；仅删除本功能生成的vision_*.jpg。抓拍失败保留错误，分析失败保留照片。GET /api/vision/record/{id}可关联查看单条7天内记录，过期404；/images/{name}提供图片。

POST /api/vision/analyze异步202，忙时409、相机离线503。GET latest/history轮询；/live已退役，在线410、离线503，不建立MJPEG预览。当前控制页2秒轮询，照片页3秒轮询，不依赖WebSocket业务推送。

仅定时分析进入自动虫害规则，手动抓拍只展示结果。农药2秒/48小时规则见[automation_control.md](automation_control.md)。本轮定时补水/施肥不改CAM，CAM无需重烧；若电脑热点IP改变导致broker地址改变，则两块固件都需重新生成配置、编译和烧录，单独生成头文件不能更新板内程序。
