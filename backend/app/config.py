# -*- coding: utf-8 -*-
"""
全局配置
---------------------------------------------------------------------
所有可变参数集中在这里, 优先从 .env 读取, 缺失时用下面的默认值。

为什么用 pydantic-settings:
    它在启动时就把配置解析成强类型对象, 类型写错 / 漏字段会立刻报错,
    而不是等运行到一半才炸 —— 这种"早失败"对调试非常友好。

路径约定:
    所有相对路径都以 backend/ 为基准解析成绝对路径,
    这样无论从哪个目录启动 uvicorn, 结果都一样。
"""
from functools import lru_cache
from pathlib import Path

from pydantic_settings import BaseSettings, SettingsConfigDict
from pydantic import Field

# 本文件在 backend/app/config.py -> 上两级就是 backend/
BASE_DIR = Path(__file__).resolve().parent.parent


class Settings(BaseSettings):
    model_config = SettingsConfigDict(
        env_file=BASE_DIR / ".env",
        env_file_encoding="utf-8",
        extra="ignore",          # .env 里有多余字段不报错
    )

    # ---------------- 应用 ----------------
    app_name: str = "plant-care-backend"
    app_host: str = "127.0.0.1"
    app_port: int = 8000
    log_level: str = "INFO"
    # 用逗号分隔的字符串而不是 list: pydantic-settings 会把 list 当 JSON 解析,
    # 写成 "a,b" 会直接报错, 用字符串 + 属性拆分最省心
    cors_origins: str = "http://localhost:5173,http://127.0.0.1:5173"

    # ---------------- 数据库 ----------------
    database_url: str = ""

    # ---------------- MQTT ----------------
    mqtt_host: str = "127.0.0.1"
    mqtt_port: int = 1883
    mqtt_username: str = ""
    mqtt_password: str = ""
    mqtt_client_id: str = "plant-care-backend"
    mqtt_keepalive: int = 60
    # ★ 测试环境开关: 置 True 后 MqttClient.start() 直接返回, 根本不连 broker。
    #
    # 为什么需要它(踩过的坑): MQTT 规范规定, broker 收到"相同 client_id"的
    # 新连接时必须踢掉旧连接。而 pytest 里的 TestClient(app) 会触发 lifespan
    # → 也去连 broker → 用的是同一个 client_id → 把正在运行的正式后端顶下线。
    # 更阴的是 broker 踢人时不发 DISCONNECT 报文, paho 察觉不到, 于是
    # connected 一直是 True, 永远不会自动重连 —— 表现就是"网页显示设备离线,
    # 但板子和 broker 都好着"。tests/conftest.py 会把这个开关打开。
    mqtt_disabled: bool = False
    # 看门狗: 连着但超过这么多秒一条报文都没收到, 就判定订阅失活并重新订阅。
    # 0 表示关闭看门狗。
    mqtt_watchdog_idle_sec: int = 90

    # ---------------- 边缘端设备 ----------------
    device_id: str = "edge01"

    # ---------------- DeepSeek ----------------
    deepseek_api_key: str = ""
    deepseek_base_url: str = "https://api.deepseek.com"
    deepseek_model: str = "deepseek-flash"
    deepseek_timeout: float = 60.0

    # ---------------- 摄像头 ----------------
    # USB 摄像头的旧预留参数；ESP32-CAM 接入后仍保留，避免破坏既有配置。
    camera_index: int = 0
    camera_width: int = 1280
    camera_height: int = 720
    # ESP32-CAM 视觉节点。它用 MQTT status 公布动态 IP 下的两个 HTTP 地址。
    camera_device_id: str = "cam01"
    camera_frame_interval_sec: int = 0  # Legacy metadata polling is no longer scheduled.
    camera_http_timeout_sec: float = 20.0
    vision_interval_sec: int = Field(default=300, ge=0)
    vision_retention_days: int = Field(default=7, ge=1)
    image_storage_dir: str = ""

    # ================= 派生属性 =================

    @property
    def cors_origin_list(self) -> list[str]:
        """把 'a,b,c' 拆成 ['a','b','c'], 自动去掉空白项。"""
        return [o.strip() for o in self.cors_origins.split(",") if o.strip()]

    @property
    def data_dir(self) -> Path:
        return BASE_DIR / "data"

    @property
    def image_dir(self) -> Path:
        """抓拍图片保存目录(自动创建)。"""
        d = Path(self.image_storage_dir).resolve() if self.image_storage_dir else self.data_dir / "images"
        d.mkdir(parents=True, exist_ok=True)
        return d

    @property
    def sqlalchemy_url(self) -> str:
        """
        最终使用的数据库连接串。
        .env 没写 DATABASE_URL 时, 默认指向 backend/data/plant.db。
        """
        if self.database_url.strip():
            return self.database_url.strip()
        db_file = self.data_dir / "plant.db"
        self.data_dir.mkdir(parents=True, exist_ok=True)
        # SQLite 需要正斜杠, Windows 的反斜杠会被当成转义符
        return f"sqlite:///{db_file.as_posix()}"


@lru_cache
def get_settings() -> Settings:
    """缓存单例, 避免每次导入都重新读 .env。"""
    return Settings()


settings = get_settings()
