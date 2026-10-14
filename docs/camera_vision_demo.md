# ESP32-CAM 高清拍照与 DeepSeek 分析

本次功能适用于 demo：植物品种未知，全天每 300 秒拍照一次，AI 提供观察与结构化虫害结果；自动喷洒扩展见 `automation_control.md`。

## 三端协作

- CAM：1600×1200（UXGA）、JPEG quality=12、单 PSRAM 帧缓冲。只提供 `/capture`；请求时先丢弃两帧，再返回新帧。仍每 3 秒通过 `plant/cam01/status` 发布 IP、在线状态与 snapshot_url。
- 后端：统一调度拍照、JPEG 校验、照片落盘、DeepSeek 官方图像分析、记录查询和清理。手动和定时任务共用互斥锁，只有定时结果进入自动规则，手动抓拍不触发喷洒。
- 前端：取消 MJPEG 预览。展示最新高清照片及对应结果，3 秒轮询任务状态，支持最近 7 天分页历史和点击原图。
- S3：自动控制需烧录新版 S3 固件。CAM 使用 cam01，S3 仍使用 edge01；相机可以独立在线。

## 配置与启动

在 `backend/.env` 设置（密钥只存在后端；不要提交或发送给前端/固件）：

```dotenv
DEEPSEEK_API_KEY=填写你的真实密钥
DEEPSEEK_BASE_URL=https://api.deepseek.com
DEEPSEEK_MODEL=deepseek-flash
DEEPSEEK_TIMEOUT=60
VISION_INTERVAL_SEC=300
VISION_RETENTION_DAYS=7
CAMERA_HTTP_TIMEOUT_SEC=20
```

使用后端独立虚拟环境安装 `backend/requirements.txt` 中的依赖；新增 Pillow 用于校验 JPEG 格式与尺寸。
更新配置后重启后端。定时拍摄从后端启动后 5 分钟开始；关机时不拍摄，恢复后不补历史任务。缺少 Key 时照片仍保存，记录显示配置提示。

照片：`backend/data/images/vision_*.jpg`。结果与错误：现有 SQLite 的 vision_record 表；复用原有字段，无需破坏旧数据库结构。raw_response 保存规范化任务状态、尺寸和分析内容，不保存密钥、请求图片 base64 或提供方错误原文。

数据保留滚动 7 天，历史接口过滤过期记录；后端启动时以及每 10 分钟清理过期照片和记录。遗留 metadata 表的过期 CAM 记录一并清理。清理只删除本功能生成的 vision_*.jpg，不清理其他文件。异常中断的 pending 任务转为失败，可重新拍摄。

## API 契约

- `GET /api/vision/camera/status`：online、capture_available、analysis_busy、ai_configured、capture_interval_sec、retention_days。stream_available 固定 false。
- `POST /api/vision/analyze`：成功受理返回 202、accepted=true；已有任务返回409；CAM离线返回503。结果由轮询 latest 获取，避免长 AI 请求超过网页超时。
- `GET /api/vision/latest`：available、data、busy。data 包含 id、ts、image_url、status、error、width、height、byte_size、result。
- `GET /api/vision/history?limit=30&before_id=...`：items 与 next_cursor；按 id 降序分页。
- status：capturing → analyzing → done/error。分析失败仍保留已保存照片。result 含 has_issue（true/false/null）、diagnosis、visible_signs、possible_causes、advice、uncertainty。
- `/images/<filename>`：照片访问；取消的 `/api/vision/live` 返回410（相机离线时503），不建立无限图像流连接。

## 验证边界

测试使用隔离临时数据库/图片目录、真实 1600×1200 测试 JPEG 和模拟官方 HTTP 响应，禁止连接正式 MQTT 或调用付费 API。验证成功链路、离线、无密钥、401/429/500、异常 JSON、互斥、分页、保留和恢复。

本次仅编译 CAM 固件，尚未烧录。板上旧固件仍会拍低分辨率图片；后端兼容其 `/capture`，记录实际尺寸。1600×1200 实拍效果、现场曝光/对焦和真实 DeepSeek 账号调用须在后续烧录与配置后验证。

## 代码审查结论

- 固件仅保留 capture 接口，清空 stream_url；避免旧无限流占用单 HTTP 服务线程。
- 不改变 S3 MQTT topic 或命令格式；相机独立状态使用 cam01。
- 不增加已有 SQLite 表的列，旧库可由 create_all 安全复用。
- 后端照片下载直连局域网，不经过系统代理；设置超时、8 MB 上限、JPEG 校验及尺寸检查。
- DeepSeek 错误不返回原始响应体；API Key 不进入照片、数据库、前端或固件。
- 手动请求先受理再后台处理，避免网页10秒超时；任务互斥覆盖手动、定时和清理。
- 关闭实时流接口，不再建立会一直阻塞的上游 HTTP 请求。
- 本 demo 后端请以单实例、单 worker 运行，任务锁为进程内锁。
- 验证：后端23项测试通过（有两项第三方弃用提示，无测试失败）；前端生产构建通过；ESP-IDF CAM 固件编译通过，分区约余29%。未烧录、未进行真实付费API调用。
