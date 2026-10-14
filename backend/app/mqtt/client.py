# -*- coding: utf-8 -*-
"""
MQTT 客户端
---------------------------------------------------------------------
设计要点(每一条都是为了"别把后端搞挂"):

  1. connect_async + loop_start
       网络不通时不会抛异常, paho 会自动按 reconnect_delay_set 的节奏重连。
       后端服务照常启动, 只是日志里提示 MQTT 未连接。

  2. 回调里绝不往外抛异常
       回调跑在 paho 自己的网络线程里, 一旦抛异常整个网络循环就死了,
       后面再也收不到消息 —— 所以回调里所有逻辑都包 try/except。

  3. 收到的消息先进内存队列
       网络线程只负责"收", 业务处理由别处取走, 两者解耦。

  4. 线程安全
       connected 状态和队列都用锁保护, 因为它们跨线程读写。

  5. ★ 看门狗(解决"连上了却收不到"的隐形故障)
       真实故障记录(2026-09-13): 有人跑 pytest, 测试进程里的
       TestClient(app) 触发了 lifespan → 用同一个 client_id 连 broker
       → 按 MQTT 规范, broker 必须踢掉旧连接 → 正式后端被踢下线。
       但踢人时 broker 不一定发 DISCONNECT 报文, paho 于是察觉不到,
       _connected 一直保持 True, 不会触发自动重连 —— 表现就是
       "网页显示设备离线, 可板子和 broker 都活得好好的"。

       看门狗的做法很朴素: 只要"连着但长时间一条报文都没收到",
       就当作订阅已失活, 主动重新订阅一次(订阅是幂等操作, 重复无害)。
       这样任何同类隐形故障都能在 mqtt_watchdog_idle_sec 秒内自愈。

注意: paho-mqtt 2.x 必须指定 CallbackAPIVersion, 否则报错。
"""
import json
import logging
import threading
import time
from collections import deque
from collections.abc import Callable
from typing import Any

from app.config import settings
from app.mqtt import topics

logger = logging.getLogger(__name__)

try:
    import paho.mqtt.client as mqtt

    _PAHO_AVAILABLE = True
except ImportError:  # 没装 paho-mqtt 时后端仍能跑(只是没有 MQTT 功能)
    mqtt = None  # type: ignore[assignment]
    _PAHO_AVAILABLE = False


MessageHandler = Callable[[str, dict[str, Any]], None]


class MqttClient:
    def __init__(self) -> None:
        self._client: Any = None
        self._connected = False
        self._lock = threading.Lock()
        # topic -> 处理函数列表
        self._handlers: dict[str, list[MessageHandler]] = {}
        # 收上来但还没处理的消息: (topic, payload)
        self._pending: deque[tuple[str, dict[str, Any]]] = deque(maxlen=2000)
        # ---- 看门狗相关 ----
        # 最后一次"收到报文"的时刻(time.monotonic 单调时钟, 系统改时间也不受影响)。
        # 注意是"收到报文"而不是"连接成功": 连接成功却收不到报文, 才是要抓的病。
        self._last_rx_at: float = 0.0
        self._stop_evt = threading.Event()
        self._watchdog: threading.Thread | None = None

    # ------------------------------------------------------------------
    # 状态
    # ------------------------------------------------------------------
    @property
    def connected(self) -> bool:
        with self._lock:
            return self._connected

    # ------------------------------------------------------------------
    # 生命周期
    # ------------------------------------------------------------------
    def start(self) -> None:
        """创建客户端并异步连接。函数立刻返回, 不阻塞服务启动。"""
        if settings.mqtt_disabled:
            # 测试环境专用: 压根不连 broker。
            # 原因见文件头第 5 条 —— 用同一个 client_id 去连, 会把正在
            # 运行的正式后端顶下线, 而且它自己还察觉不到。
            logger.info("MQTT 已按配置禁用 (MQTT_DISABLED=1), 跳过连接")
            return

        if not _PAHO_AVAILABLE:
            logger.warning("未安装 paho-mqtt, MQTT 功能已禁用 (pip install paho-mqtt)")
            return

        try:
            self._client = mqtt.Client(
                mqtt.CallbackAPIVersion.VERSION2,
                client_id=settings.mqtt_client_id,
                clean_session=True,
            )
            if settings.mqtt_username:
                self._client.username_pw_set(
                    settings.mqtt_username, settings.mqtt_password
                )

            self._client.on_connect = self._on_connect
            self._client.on_disconnect = self._on_disconnect
            self._client.on_message = self._on_message

            # 断线重连节奏: 1 秒起, 最长 30 秒
            self._client.reconnect_delay_set(min_delay=1, max_delay=30)

            # connect_async 只是"登记要连的地址", 真正的连接在网络线程里做,
            # 所以 broker 没开也不会在这里抛异常
            self._client.connect_async(
                settings.mqtt_host, settings.mqtt_port, settings.mqtt_keepalive
            )
            self._client.loop_start()
            logger.info(
                "MQTT 客户端已启动, 目标 %s:%s (断网会自动重连)",
                settings.mqtt_host,
                settings.mqtt_port,
            )

            # 连接建立后, 再把看门狗挂上(它只依赖"收没收到报文")
            self._start_watchdog()
        except Exception as exc:  # 兜底: 任何意外都不能阻止后端启动
            logger.warning("MQTT 客户端启动失败: %s", exc)
            self._client = None

    def stop(self) -> None:
        # 先让看门狗退出, 再断连接 —— 否则它可能在关到一半时又去重订阅
        self._stop_watchdog()

        if self._client is None:
            return
        try:
            self._client.loop_stop()
            self._client.disconnect()
        except Exception as exc:
            logger.debug("MQTT 关闭时出现小问题(可忽略): %s", exc)
        finally:
            self._client = None
            with self._lock:
                self._connected = False

    # ------------------------------------------------------------------
    # 看门狗: 专治"连接显示正常, 却一条报文都收不到"
    # ------------------------------------------------------------------
    def _start_watchdog(self) -> None:
        """
        起一个守护线程, 定期检查"最近到底有没有收到报文"。

        为什么盯"收到报文"而不是盯 connected:
            被 broker 踢掉时, 如果对方没发 DISCONNECT 报文, paho 的
            _connected 会一直停在 True, 自动重连也就永远不会被触发。
            这时唯一可信的证据, 就是"上一条报文是多久以前收到的"。
        """
        idle = settings.mqtt_watchdog_idle_sec
        if idle <= 0:
            logger.info("MQTT 看门狗已关闭 (mqtt_watchdog_idle_sec=0)")
            return
        if self._watchdog is not None and self._watchdog.is_alive():
            return  # 已经在跑了, 别起第二个

        self._stop_evt.clear()
        with self._lock:
            self._last_rx_at = time.monotonic()

        def _loop() -> None:
            # wait() 的返回值: 超时 → False; 被 set() → True。
            # 用 wait 而不是 sleep, 是为了 stop() 时能立刻退出, 不必干等一整个周期。
            while not self._stop_evt.wait(idle):
                self._check_once(idle)

        self._watchdog = threading.Thread(target=_loop, name="mqtt-watchdog", daemon=True)
        self._watchdog.start()
        logger.info("MQTT 看门狗已启动 (静默超过 %d 秒则自动重新订阅)", idle)

    def _check_once(self, idle: int) -> None:
        """看门狗的一个检查周期。"""
        try:
            if not self.connected:
                # 真断线时 paho 自己会重连, 重连成功后 _on_connect 会重订阅, 不必插手
                return

            with self._lock:
                silence = time.monotonic() - self._last_rx_at
            if silence < idle:
                return

            logger.warning(
                "MQTT 已连接但连续 %.0f 秒没收到任何报文, 判定订阅失活, 重新订阅...",
                silence,
            )
            self._resubscribe()
            # 重置计时, 免得每个周期都刷同一条日志; 真没恢复的话下个周期还会再报
            with self._lock:
                self._last_rx_at = time.monotonic()
        except Exception as exc:  # 守护线程绝不能因为异常就静默死掉
            logger.error("MQTT 看门狗检查异常: %s", exc)

    def _resubscribe(self) -> None:
        """重新订阅上行主题(幂等操作, broker 会用新订阅覆盖旧的)。"""
        if self._client is None:
            return
        try:
            for topic in self._handlers:
                result, _ = self._client.subscribe(topic, qos=0)
                if result == mqtt.MQTT_ERR_SUCCESS:
                    logger.info("重新订阅成功: %s", topic)
                else:
                    logger.warning("重新订阅返回异常码 [%s]: %s", topic, result)
        except Exception as exc:
            logger.error("重新订阅失败: %s", exc)

    def _stop_watchdog(self) -> None:
        self._stop_evt.set()
        wd = self._watchdog
        self._watchdog = None
        if wd is not None and wd.is_alive():
            wd.join(timeout=2)

    # ------------------------------------------------------------------
    # 收
    # ------------------------------------------------------------------
    def _on_connect(
        self, client: Any, userdata: Any, flags: Any, reason_code: Any, properties: Any = None
    ) -> None:
        """连接成功回调(paho 2.x 签名)。"""
        try:
            if getattr(reason_code, "is_failure", False):
                logger.warning("MQTT 连接被拒绝: %s", reason_code)
                return

            with self._lock:
                self._connected = True
                # 重连成功时重置计时: 否则刚连上就可能被看门狗误判为"失活"
                self._last_rx_at = time.monotonic()
            logger.info("MQTT 已连接 (%s:%s)", settings.mqtt_host, settings.mqtt_port)

            # 重连之后必须重新订阅 —— 这点最容易漏,
            # 漏了就会出现"连上了却收不到数据"的怪现象
            for topic in self._handlers:
                client.subscribe(topic, qos=0)
                logger.info("已订阅上行主题: %s", topic)
        except Exception as exc:
            logger.error("MQTT on_connect 处理异常: %s", exc)

    def _on_disconnect(
        self, client: Any, userdata: Any, flags: Any, reason_code: Any, properties: Any = None
    ) -> None:
        with self._lock:
            self._connected = False
        if getattr(reason_code, "is_failure", False):
            logger.warning("MQTT 连接断开(%s), 将持续重试...", reason_code)

    def _on_message(self, client: Any, userdata: Any, message: Any) -> None:
        """收到消息: 解析 JSON 后放队列, 不做耗时操作。"""
        try:
            raw = message.payload.decode("utf-8", "replace")
            try:
                data = json.loads(raw)
            except json.JSONDecodeError:
                logger.warning("MQTT 收到非 JSON 报文 [%s]: %s", message.topic, raw[:120])
                return

            with self._lock:
                self._pending.append((message.topic, data))
                # ★ 看门狗唯一可信的"我还活着"证据: 只要有报文进来就记时间
                self._last_rx_at = time.monotonic()

            for handler in self._handlers.get(message.topic, []):
                try:
                    handler(message.topic, data)
                except Exception as exc:
                    logger.error("MQTT 处理函数异常 [%s]: %s", message.topic, exc)
        except Exception as exc:
            # 绝不能让它冒泡到 paho 的网络线程
            logger.error("MQTT on_message 处理异常: %s", exc)

    def subscribe(self, topic: str, handler: MessageHandler) -> None:
        """注册某主题的处理函数(只在内存里登记)。"""
        self._handlers.setdefault(topic, []).append(handler)
        if self.connected and self._client is not None:
            self._client.subscribe(topic, qos=0)

    def pop_pending(self) -> list[tuple[str, dict[str, Any]]]:
        """取走当前排队的所有消息(调用方负责处理)。"""
        with self._lock:
            items = list(self._pending)
            self._pending.clear()
        return items

    # ------------------------------------------------------------------
    # 发
    # ------------------------------------------------------------------
    def publish_json(self, topic: str, payload: dict[str, Any], qos: int = 0) -> bool:
        """发布一条 JSON 消息, 返回是否真的发出去了。"""
        if self._client is None:
            logger.warning("MQTT 未初始化, 丢弃消息 [%s]", topic)
            return False
        if not self.connected:
            logger.warning("MQTT 未连接, 丢弃消息 [%s]", topic)
            return False

        try:
            body = json.dumps(payload, ensure_ascii=False)
            info = self._client.publish(topic, body, qos=qos)
            if info.rc != 0:
                logger.warning("MQTT 发布失败 [%s] rc=%s", topic, info.rc)
                return False
            logger.debug("MQTT 已发布 [%s] %s", topic, body)
            return True
        except Exception as exc:
            logger.error("MQTT 发布异常 [%s]: %s", topic, exc)
            return False


# 全局单例: 其它模块 from app.mqtt.client import mqtt_client 即可
mqtt_client = MqttClient()
