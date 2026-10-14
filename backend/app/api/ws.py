# -*- coding: utf-8 -*-
"""
WebSocket 实时推送
---------------------------------------------------------------------
前端连上 /ws 之后, 后端可以主动把新数据推过去, 不必让前端不停轮询。

支持多个客户端同时连接(比如 PC 浏览器和手机同时看同一个花盆)。

前端约定(见 docs/api.md):
    收到 {"type": "telemetry", "data": {...}}   新的传感器读数
    收到 {"type": "vision",    "data": {...}}   新的 AI 诊断结果
    收到 {"type": "status",    "data": {...}}   设备在线状态变化
"""
import logging
from typing import Any

from fastapi import APIRouter, WebSocket, WebSocketDisconnect

logger = logging.getLogger(__name__)

router = APIRouter()


class ConnectionManager:
    """管理所有活跃的 WebSocket 连接。"""

    def __init__(self) -> None:
        self._connections: list[WebSocket] = []

    @property
    def count(self) -> int:
        return len(self._connections)

    async def connect(self, websocket: WebSocket) -> None:
        await websocket.accept()
        self._connections.append(websocket)
        logger.info("WebSocket 客户端接入, 当前在线 %d 个", self.count)

    def disconnect(self, websocket: WebSocket) -> None:
        if websocket in self._connections:
            self._connections.remove(websocket)
        logger.info("WebSocket 客户端断开, 剩余 %d 个", self.count)

    async def broadcast(self, message: dict[str, Any]) -> None:
        """给所有客户端广播一条消息, 自动剔除掉线者。"""
        dead: list[WebSocket] = []
        for ws in list(self._connections):
            try:
                await ws.send_json(message)
            except Exception:
                dead.append(ws)
        for ws in dead:
            self.disconnect(ws)


manager = ConnectionManager()


@router.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket) -> None:
    await manager.connect(websocket)
    try:
        # 先打个招呼, 让前端知道连接是通的
        await websocket.send_json({"type": "hello", "data": {"msg": "已连接"}})
        while True:
            # 目前只做回显; 将来前端可以通过这里发"心跳"或"急停"
            text = await websocket.receive_text()
            if text == "ping":
                await websocket.send_json({"type": "pong", "data": {}})
            else:
                await websocket.send_json({"type": "echo", "data": text})
    except WebSocketDisconnect:
        manager.disconnect(websocket)
    except Exception as exc:
        logger.warning("WebSocket 异常: %s", exc)
        manager.disconnect(websocket)
