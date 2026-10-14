# -*- coding: utf-8 -*-
"""
FastAPI 依赖注入
---------------------------------------------------------------------
路由函数签名里写 db: DbSession 就能拿到数据库会话,
比到处重复写 db: Session = Depends(get_db) 干净得多。
"""
from typing import Annotated

from fastapi import Depends
from sqlalchemy.orm import Session

from app.db.session import get_db

DbSession = Annotated[Session, Depends(get_db)]
