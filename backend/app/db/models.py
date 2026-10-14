# -*- coding: utf-8 -*-
"""
数据库表模型
---------------------------------------------------------------------
四张表对应系统的四条数据线:
    Plant           植物档案        —— 种的是什么
    SensorReading   传感器读数      —— 环境怎么样
    ControlLog      执行器动作日志  —— 我们做了什么(谁下令的、结果如何)
    VisionRecord    图像分析记录    —— AI 看到了什么

时间字段统一用 datetime.now() 存本地时间。
(规范做法是存 UTC, 但课程设计演示时直接看到本地时间更直观)
"""
from datetime import datetime

from sqlalchemy import Boolean, DateTime, Float, Index, Integer, String, Text
from sqlalchemy.orm import Mapped, mapped_column

from app.db.base import Base


class Plant(Base):
    """植物档案。"""

    __tablename__ = "plant"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    device_id: Mapped[str] = mapped_column(String(32), index=True, default="edge01")
    name: Mapped[str] = mapped_column(String(64), default="未命名植物")
    species: Mapped[str] = mapped_column(String(64), default="")
    planted_at: Mapped[datetime] = mapped_column(DateTime, default=datetime.now)
    note: Mapped[str] = mapped_column(Text, default="")


class SensorReading(Base):
    """
    一组传感器读数(一次上报 = 一行)。
    字段全部允许为空: 某些传感器还没接上时, 其余数据仍应能正常入库。
    """

    __tablename__ = "sensor_reading"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    device_id: Mapped[str] = mapped_column(String(32), index=True, default="edge01")
    ts: Mapped[datetime] = mapped_column(DateTime, default=datetime.now, index=True)

    soil_moisture: Mapped[float | None] = mapped_column(Float, nullable=True)  # 土壤湿度 %
    soil_ec: Mapped[float | None] = mapped_column(Float, nullable=True)        # 土壤盐分 mS/cm
    temperature: Mapped[float | None] = mapped_column(Float, nullable=True)    # 环境温度 °C
    light_lux: Mapped[float | None] = mapped_column(Float, nullable=True)      # 光照强度 lux

    __table_args__ = (Index("ix_sensor_device_ts", "device_id", "ts"),)


class ControlLog(Base):
    """
    执行器动作日志 —— 安全追溯的关键。
    每次开关水泵/补光灯/喷雾/施肥泵都记一行, 前端"操作记录"页面直接读这里。
    """

    __tablename__ = "control_log"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    device_id: Mapped[str] = mapped_column(String(32), index=True, default="edge01")
    ts: Mapped[datetime] = mapped_column(DateTime, default=datetime.now, index=True)

    actuator: Mapped[str] = mapped_column(String(32))          # pump/fertilizer/grow_light/spray
    action: Mapped[str] = mapped_column(String(16))            # on / off
    duration_ms: Mapped[int] = mapped_column(Integer, default=0)
    # 谁下的令: manual(前端手动) / auto(阈值闭环) / ai(AI 诊断触发)
    source: Mapped[str] = mapped_column(String(16), default="manual")
    # pending(已下发待确认) / done / rejected(被边缘端安全策略拒绝)
    result: Mapped[str] = mapped_column(String(16), default="pending")
    reason: Mapped[str] = mapped_column(Text, default="")


class VisionRecord(Base):
    """一次图像分析记录。need_spray 为 AI 闭环控制提供依据。"""

    __tablename__ = "vision_record"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    device_id: Mapped[str] = mapped_column(String(32), index=True, default="edge01")
    ts: Mapped[datetime] = mapped_column(DateTime, default=datetime.now, index=True)

    image_path: Mapped[str] = mapped_column(String(255), default="")
    model: Mapped[str] = mapped_column(String(64), default="")
    diagnosis: Mapped[str] = mapped_column(Text, default="")       # 给人看的结论
    raw_response: Mapped[str] = mapped_column(Text, default="")    # 原始 JSON, 留证
    need_spray: Mapped[bool] = mapped_column(Boolean, default=False)


class CameraFrame(Base):
    """一次 ESP32-CAM 抓拍的元数据；当前阶段绝不保存 JPEG 内容。"""

    __tablename__ = "camera_frame"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    device_id: Mapped[str] = mapped_column(String(32), index=True, default="cam01")
    ts: Mapped[datetime] = mapped_column(DateTime, default=datetime.now, index=True)
    source_url: Mapped[str] = mapped_column(String(255), default="")
    content_type: Mapped[str] = mapped_column(String(64), default="")
    byte_size: Mapped[int] = mapped_column(Integer, default=0)
    captured: Mapped[bool] = mapped_column(Boolean, default=False)
    stored: Mapped[bool] = mapped_column(Boolean, default=False)
    error: Mapped[str] = mapped_column(Text, default="")

    __table_args__ = (Index("ix_camera_frame_device_ts", "device_id", "ts"),)


class ActuatorCommand(Base):
    """Correlated command lifecycle; a new table preserves the existing demo DB."""
    __tablename__ = "actuator_command"
    command_id: Mapped[str] = mapped_column(String(32), primary_key=True)
    device_id: Mapped[str] = mapped_column(String(32), index=True)
    boot_id: Mapped[str] = mapped_column(String(16))
    ts: Mapped[datetime] = mapped_column(DateTime, default=datetime.now, index=True)
    updated_at: Mapped[datetime] = mapped_column(DateTime, default=datetime.now)
    actuator: Mapped[str] = mapped_column(String(32))
    action: Mapped[str] = mapped_column(String(16))
    duration_ms: Mapped[int] = mapped_column(Integer, default=0)
    result: Mapped[str] = mapped_column(String(16), default="pending")
    reason: Mapped[str] = mapped_column(Text, default="")


class AutomationState(Base):
    """Additive schema: durable policy, cooldown and decision journal."""
    __tablename__ = "automation_state"
    device_id: Mapped[str] = mapped_column(String(32), primary_key=True)
    data: Mapped[str] = mapped_column(Text, default="{}")
