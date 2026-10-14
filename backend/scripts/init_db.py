# -*- coding: utf-8 -*-
"""
初始化数据库
---------------------------------------------------------------------
建表 + 插入一株示例植物(已有则跳过)。

用法(在 backend/ 目录下):
    .\\.venv\\Scripts\\python.exe scripts\\init_db.py
"""
import sys
from pathlib import Path

# 允许直接运行本脚本: 把 backend/ 加到模块搜索路径
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from app.db.models import Plant  # noqa: E402
from app.db.session import SessionLocal, init_db  # noqa: E402


def main() -> None:
    init_db()

    db = SessionLocal()
    try:
        exists = db.query(Plant).first()
        if exists:
            print(f"已存在植物档案: id={exists.id} name={exists.name}")
            return

        plant = Plant(name="示例植物", species="待填写", note="由 init_db.py 创建")
        db.add(plant)
        db.commit()
        db.refresh(plant)
        print(f"已创建植物档案: id={plant.id} name={plant.name}")
    finally:
        db.close()


if __name__ == "__main__":
    main()
