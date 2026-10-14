# -*- coding: utf-8 -*-
"""
统一日志配置
---------------------------------------------------------------------
所有模块都用标准 logging 库, 通过 getLogger(__name__) 拿 logger,
在 main.py 启动时调用一次 setup_logging() 统一配置输出格式。
"""
import logging
import sys

LOG_FORMAT = "%(asctime)s [%(levelname)-7s] %(name)-24s | %(message)s"
DATE_FORMAT = "%Y-%m-%d %H:%M:%S"

# 这些第三方库日志太吵, 单独降噪
_NOISY_LOGGERS = {
    "paho": logging.WARNING,
    "apscheduler": logging.WARNING,
    "asyncio": logging.WARNING,
}


def setup_logging(level: str = "INFO") -> None:
    """配置根 logger。可重复调用(比如 uvicorn --reload 重新导入模块时)。"""
    root = logging.getLogger()

    # 先清掉旧 handler, 否则会重复打印
    for h in list(root.handlers):
        root.removeHandler(h)

    handler = logging.StreamHandler(sys.stdout)
    handler.setFormatter(logging.Formatter(LOG_FORMAT, DATE_FORMAT))
    root.addHandler(handler)
    root.setLevel(getattr(logging, level.upper(), logging.INFO))

    for name, lvl in _NOISY_LOGGERS.items():
        logging.getLogger(name).setLevel(lvl)


def get_logger(name: str) -> logging.Logger:
    """模块内获取 logger 的快捷方式。"""
    return logging.getLogger(name)
