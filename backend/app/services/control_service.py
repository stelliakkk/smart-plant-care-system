# -*- coding: utf-8 -*-
"""
早期阈值建议示例（未接入实际执行）
---------------------------------------------------------------------
★ 本文件只负责"决策", 不负责"兜底"。

责任划分(整个系统最重要的一条纪律):
    后端这里:  看数据 → 要不要动作 → 下发"意图"
    边缘端:    收到意图 → 自己判断能不能执行、实际执行多久

为什么安全策略必须放在边缘端本地?
    一旦断网, 后端就管不到设备了。如果"水泵最长运行 10 秒"这种规则写在
    后端, 那么断网时水泵可能一直抽水, 把植物淹死甚至烧掉水泵。
    当前plant_control已实现泵限时、补水/施肥互锁和断网停止。
    最小启停间隔、累计喷洒上限仍待补充；48小时喷洒冷却目前在后端。

实际控制由automation_service与S3 plant_control承担；本文件的建议函数
保留作参考，不会直接触发设备。

本文件的函数都返回 CommandRequest 或 None(None = 不需要动作),
由调用方负责真正下发。
"""
import logging

from app.schemas.device import ActuatorName, CommandRequest
from app.schemas.telemetry import TelemetryPayload

logger = logging.getLogger(__name__)

# ---------------------------------------------------------------------
# 阈值(临时写死, 阶段 4 改为按植物种类从数据库读)
# ---------------------------------------------------------------------
SOIL_MOISTURE_LOW = 25.0      # 低于此值认为土壤偏干(%)
SOIL_MOISTURE_OK = 40.0       # 补水到该值以上就停(%)
LIGHT_LOW_LUX = 100.0        # 低于此值考虑补光(lux)
SOIL_EC_LOW = 0.5             # 土壤盐分偏低(mS/cm)
SOIL_EC_HIGH = 2.5            # 土壤盐分偏高


def decide_irrigation(payload: TelemetryPayload) -> CommandRequest | None:
    """
    根据土壤湿度决定是否补水。

    注意: 这里只给出"打算开 3 秒"这样的意图, 具体够不够湿要靠下一次
          上报数据来验证(闭环)。
    """
    moisture = payload.soil_moisture
    if moisture is None:
        return None
    if moisture < SOIL_MOISTURE_LOW:
        logger.info("土壤湿度 %.1f%% 低于阈值, 建议补水", moisture)
        return CommandRequest(
            actuator=ActuatorName.PUMP,
            action="on",
            duration_ms=3000,
            source="auto",
        )
    return None


def decide_lighting(payload: TelemetryPayload) -> CommandRequest | None:
    """根据光照决定是否补光。TODO(阶段 4): 按植物种类与生长阶段调光强。"""
    lux = payload.light_lux
    if lux is None:
        return None
    if lux < LIGHT_LOW_LUX:
        logger.info("光照 %.0f lux 偏低, 建议补光", lux)
        return CommandRequest(
            actuator=ActuatorName.GROW_LIGHT,
            action="on",
            duration_ms=0,          # 0 表示"一直开着, 等下次判断再关"
            source="auto",
        )
    if lux <= 300.0:
        return None
    # 此旧建议函数未接入实际控制；实际计时判断在S3 plant_control。
    # 高于300 lux才建议关灯
    return CommandRequest(
        actuator=ActuatorName.GROW_LIGHT,
        action="off",
        duration_ms=0,
        source="auto",
    )


def decide_fertilizing(payload: TelemetryPayload) -> CommandRequest | None:
    """旧EC阈值建议示例；无EC标定，当前定时施肥不使用此函数。"""
    ec = payload.soil_ec
    if ec is None:
        return None
    if ec < SOIL_EC_LOW:
        logger.info("土壤盐分 %.2f 偏低, 建议补肥", ec)
        return CommandRequest(
            actuator=ActuatorName.FERTILIZER,
            action="on",
            duration_ms=2000,
            source="auto",
        )
    if ec > SOIL_EC_HIGH:
        logger.warning("土壤盐分 %.2f 偏高, 请勿继续施肥", ec)
    return None


def decide_from_vision(need_spray: bool) -> CommandRequest | None:
    """
    AI 诊断结果为"有虫害"时给出喷药意图。

    此旧建议函数未接入自动喷洒。实际规则要求两次有效定时照片及后端冷却；
    板端累计喷洒限制尚未实现，不能依据本示例认定已有次数保护。
    """
    if not need_spray:
        return None
    logger.info("AI 诊断判定需要喷药, 下发喷雾指令")
    return CommandRequest(
        actuator=ActuatorName.SPRAY,
        action="on",
        duration_ms=1500,
        source="ai",
    )


# TODO(阶段 4):
#   - 把这些决策串成一个 evaluate_all(payload) 函数
#   - 加"正在执行中就不重复下命令"的防抖
#   - 每次决策写入 ControlLog
