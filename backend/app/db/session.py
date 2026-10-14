# -*- coding: utf-8 -*-
"""
数据库引擎与 Session
---------------------------------------------------------------------
SQLite 的两个坑, 这里都处理了:
  1. check_same_thread=False  —— FastAPI 是多线程的, SQLite 默认禁止连接跨线程使用
  2. 数据库文件放在 backend/data/ 下(绝对路径), 不受 uvicorn 启动目录影响

建表用 create_all 而不是 Alembic 迁移工具 —— 课程设计阶段够用,
将来真要改表结构再引入 Alembic 也不迟。
"""
import logging
from collections.abc import Generator

from sqlalchemy import create_engine
from sqlalchemy.orm import Session, sessionmaker

from app.config import settings
from app.db.base import Base

logger = logging.getLogger(__name__)

# 只有 SQLite 需要这个参数
_connect_args = (
    {"check_same_thread": False}
    if settings.sqlalchemy_url.startswith("sqlite")
    else {}
)

engine = create_engine(
    settings.sqlalchemy_url,
    connect_args=_connect_args,
    echo=False,       # 改成 True 可以看到每条 SQL, 调数据库时很有用
    future=True,
)

SessionLocal = sessionmaker(
    bind=engine,
    autoflush=False,
    autocommit=False,
    expire_on_commit=False,
)


def init_db() -> None:
    """建表(表已存在则跳过)。"""
    # 必须导入 models, 否则 Base 不知道有哪些表要建
    from app.db import models  # noqa: F401

    Base.metadata.create_all(bind=engine)
    logger.info("数据表就绪: %s", ", ".join(sorted(Base.metadata.tables.keys())))


def get_db() -> Generator[Session, None, None]:
    """
    FastAPI 依赖注入用: 每个请求开一个 Session, 请求结束自动关闭。
    用法: def route(db: Session = Depends(get_db))
    """
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()
