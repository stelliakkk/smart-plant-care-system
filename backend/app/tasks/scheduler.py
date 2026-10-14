# -*- coding: utf-8 -*-
"""
后台定时任务
---------------------------------------------------------------------
用 APScheduler 的 BackgroundScheduler: 它跑在独立线程里,
不会阻塞 FastAPI 的事件循环。

当前挂两个任务:
    1. 定时图像分析 —— 仅当 .env 里 VISION_INTERVAL_SEC > 0 时启用
    2. 设备离线检测 —— 每 15 秒看一次边缘端还在不在

注意: 所有 job 函数都必须自己把异常吃掉。定时任务抛异常虽然不会
      影响 Web 服务, 但会让这个 job 从此不再触发, 问题很难查。
"""
import logging

from apscheduler.schedulers.background import BackgroundScheduler

from app.config import settings

logger = logging.getLogger(__name__)

# 用 BackgroundScheduler 而不是 AsyncIOScheduler:
# 抓图、调用 DeepSeek 都是阻塞式 IO, 放独立线程里反而更简单可靠
scheduler = BackgroundScheduler(timezone="Asia/Shanghai")


def _job_vision() -> None:
    """定时抓拍 + AI 分析。"""
    from app.services import vision_service

    logger.info("定时任务: 开始抓拍并分析图像")
    try:
        vision_service.run_once()
    except Exception:
        logger.exception("定时图像分析失败")


def _job_camera_frame() -> None:
    """每分钟请求一帧，只记录元数据，不保存图片也不调用 AI。"""
    from app.services import camera_service

    try:
        camera_service.capture_metadata()
    except Exception:
        logger.exception("相机元数据抓帧任务失败")


def _job_check_offline() -> None:
    """检查边缘端是否掉线。"""
    from app.services import device_service

    try:
        from app.services import command_service
        command_service.history(1)
        status = device_service.get_status()
        if not status.online:
            logger.warning(
                "设备 %s 当前离线 (最后在线: %s)", status.device_id, status.last_seen
            )
    except Exception:
        logger.exception("离线检测失败")


def start_scheduler() -> None:
    """注册任务并启动调度器。重复调用安全。"""
    if scheduler.running:
        return

    if settings.vision_interval_sec > 0:
        scheduler.add_job(
            _job_vision,
            "interval",
            seconds=settings.vision_interval_sec,
            id="vision",
            replace_existing=True,
            max_instances=1,
            coalesce=True,
        )
        logger.info("定时图像分析已启用: 每 %d 秒一次", settings.vision_interval_sec)
    else:
        logger.info("定时图像分析未启用 (VISION_INTERVAL_SEC=0)")

    from app.services import vision_service
    scheduler.add_job(vision_service.cleanup, "interval", minutes=10,
                      id="vision-cleanup", replace_existing=True, max_instances=1, coalesce=True)
    vision_service.cleanup()

    scheduler.add_job(
        _job_check_offline,
        "interval",
        seconds=15,
        id="offline_check",
        replace_existing=True,
    )

    scheduler.start()
    logger.info("定时任务调度器已启动")


def stop_scheduler() -> None:
    """关闭调度器。"""
    if scheduler.running:
        scheduler.shutdown(wait=False)
        logger.info("定时任务调度器已停止")
