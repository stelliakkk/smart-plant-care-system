# -*- coding: utf-8 -*-
"""图像分析结果契约(前端"AI 诊断"页面用)。"""
from datetime import datetime

from pydantic import BaseModel, Field


class VisionResult(BaseModel):
    """一次 DeepSeek 图像分析的输出。"""

    id: int | None = None
    ts: datetime | None = None
    image_url: str = Field(default="", description="抓拍图的访问地址")
    diagnosis: str = Field(default="", description="给用户看的诊断结论")
    need_spray: bool = Field(default=False, description="是否需要喷药(闭环依据)")
    model: str = Field(default="", description="实际使用的模型名")
