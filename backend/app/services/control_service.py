# -*- coding: utf-8 -*-
"""
闭环控制服务
---------------------------------------------------------------------
★ 本文件只负责"决策", 不负责"兜底"。

责任划分(整个系统最重要的一条纪律):
    后端这里:  看数据 → 要不要动作 → 下发"意图"
    边缘端:    收到意图 → 自己判断能不能执行、实际执行多久

为什么安全策略必须放在边缘端本地?
    一旦断网, 后端就管不到设备了。如果"水泵最长运行 10 秒"这种规则写在
    后端, 那么断网时水泵可能一直抽水, 把植物淹死甚至烧掉水泵。
    所以下面这些必须由边缘端的 plant_ctrl 组件本地实现:
        - 水泵单次最长运行时间
        - 两次运行之间的最小间隔(防止频繁启停)
        - 喷雾每日次数上限(农药残留)
        - 施肥与补水的互锁
        - 看门狗

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
LIGHT_LOW_LUX = 3000.0        # 低于此值考虑补光(lux)
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
    # 光照够了就关灯
    return CommandRequest(
        actuator=ActuatorName.GROW_LIGHT,
        action="off",
        duration_ms=0,
        source="auto",
    )


def decide_fertilizing(payload: TelemetryPayload) -> CommandRequest | None:
    """根据土壤盐分决定是否补肥(高浓度盐水)。"""
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

    ⚠️ 每日喷洒次数上限由边缘端把关 —— 后端可以重复下命令,
       边缘端必须自己拒绝超限的请求(并回报 rejected)。
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
