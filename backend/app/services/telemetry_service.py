# -*- coding: utf-8 -*-
"""
传感器数据服务
---------------------------------------------------------------------
负责: 上报数据入库、查询历史、阈值判断。

阈值判断的作用是给闭环控制提供输入 —— 但它只产出"建议",
真正的动作由 control_service 决策、由边缘端执行。
"""
import logging
from datetime import datetime

from sqlalchemy import select
from sqlalchemy.orm import Session

from app.config import settings
from app.db.models import SensorReading
from app.db.session import SessionLocal
from app.schemas.telemetry import TelemetryPayload

logger = logging.getLogger(__name__)


def save_reading(payload: TelemetryPayload) -> int:
    """把一条上报数据写入数据库, 返回新记录的主键。"""
    db: Session = SessionLocal()
    try:
        row = SensorReading(
            device_id=payload.device_id,
            data_source=payload.data_source,
            soil_moisture=payload.soil_moisture,
            soil_ec=payload.soil_ec,
            temperature=payload.temperature,
            light_lux=payload.light_lux,
        )
        # 没带时间就用数据库默认值(写入时的时间)
        if payload.ts is not None:
            row.ts = payload.ts

        db.add(row)
        db.commit()
        db.refresh(row)
        logger.info(
            "传感器数据入库 id=%s 湿度=%s%% 盐分=%s 温度=%s°C 光照=%s lux",
            row.id,
            row.soil_moisture,
            row.soil_ec,
            row.temperature,
            row.light_lux,
        )
        return int(row.id)
    except Exception:
        db.rollback()
        logger.exception("传感器数据入库失败")
        raise
    finally:
        db.close()


def get_latest(device_id: str | None = None) -> SensorReading | None:
    """取最新一条读数(前端总览页用)。"""
    device_id = device_id or settings.device_id
    db: Session = SessionLocal()
    try:
        stmt = (
            select(SensorReading)
            .where(SensorReading.device_id == device_id)
            .order_by(SensorReading.ts.desc(), SensorReading.id.desc())
            .limit(1)
        )
        return db.execute(stmt).scalar_one_or_none()
    finally:
        db.close()


def get_history(
    device_id: str | None = None,
    limit: int = 200,
    since: datetime | None = None,
) -> list[SensorReading]:
    """取历史读数, 按时间正序返回(前端画曲线用)。"""
    device_id = device_id or settings.device_id
    db: Session = SessionLocal()
    try:
        stmt = select(SensorReading).where(SensorReading.device_id == device_id)
        if since is not None:
            stmt = stmt.where(SensorReading.ts >= since)
        # 先按时间倒序取最近的 limit 条, 再反转成正序
        stmt = stmt.order_by(SensorReading.ts.desc(), SensorReading.id.desc()).limit(limit)
        rows = list(db.execute(stmt).scalars().all())
        rows.reverse()
        return rows
    finally:
        db.close()


def check_thresholds(reading: SensorReading) -> list[str]:
    """
    对照阈值给出一组"提示文本"。

    TODO(阶段 4): 阈值应该按植物种类配置, 而不是写死。
    现在先返回空列表, 保证骨架能跑通。
    """
    warnings: list[str] = []
    # 示例(待按植物种类调整):
    #   if reading.soil_moisture is not None and reading.soil_moisture < 25:
    #       warnings.append("土壤偏干, 建议补水")
    #   if reading.light_lux is not None and reading.light_lux < 100:
    #       warnings.append("光照不足, 建议补光")
    return warnings
