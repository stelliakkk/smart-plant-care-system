# -*- coding: utf-8 -*-
"""SQLAlchemy 声明式基类。"""
from sqlalchemy.orm import DeclarativeBase


class Base(DeclarativeBase):
    """所有 ORM 模型的基类, 继承它即可被 create_all 自动建表。"""

    pass
