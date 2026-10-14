# -*- coding: utf-8 -*-
"""
开发用 MQTT broker(本机没装 Mosquitto 时的替代品)
=====================================================================
用纯 Python 的 amqtt 起一个 broker, 默认监听 0.0.0.0:1883(所有网卡), 允许匿名连接。

★ 为什么默认是 0.0.0.0 而不是 127.0.0.1?
    ESP32 板子是一台**独立的设备**, 它得用"电脑的局域网 IP"(比如
    192.168.202.59)才能连过来。如果 broker 只绑 127.0.0.1(回环地址),
    那个地址只有电脑自己能访问, 板子的连接请求会被直接拒绝 ——
    现象就是"板子 WiFi 明明连上了, 但 MQTT 死活连不上"。

  另外记得让 Windows 防火墙放行 1883 端口的**入站**连接,
  否则数据包会被防火墙悄悄丢掉, 同样表现为连不上。

★ 它只用于本地联调, 不用它做正式部署:
    正式部署请用 Mosquitto 这类成熟 broker —— amqtt 是纯 Python 实现,
    性能和协议覆盖面都比不上 C 写的 Mosquitto。
    换成真 Mosquitto 时后端代码一行都不用改, 只改 .env 里的 MQTT_HOST/PORT。

用法(在 backend/ 目录下):
    .\\.venv\\Scripts\\python.exe scripts\\dev_broker.py
    .\\.venv\\Scripts\\python.exe scripts\\dev_broker.py --port 1884

Ctrl+C 退出。
=====================================================================
"""
import argparse
import asyncio
import logging

try:
    from amqtt.broker import Broker
except ImportError:
    print("缺少 amqtt, 请先执行: pip install -r requirements-dev.txt")
    raise SystemExit(1)


def build_config(host: str, port: int) -> dict:
    return {
        "listeners": {
            "default": {
                "type": "tcp",
                "bind": f"{host}:{port}",
                "max_connections": 100,
            }
        },
        "sys_interval": 10,
        # 联调方便: 允许匿名连接。
        # 正式环境务必关掉并配账号密码(见 Mosquitto 的 password_file)
        "auth": {"allow-anonymous": True},
        # 联调阶段不做主题权限校验, 否则每个新主题都要去改配置
        "topic-check": {"enabled": False},
    }


async def serve(host: str, port: int) -> None:
    broker = Broker(build_config(host, port))
    await broker.start()
    print(f"[开发 broker] 已启动: mqtt://{host}:{port}  (允许匿名)")
    print("[开发 broker] 按 Ctrl+C 退出")
    try:
        await asyncio.Event().wait()
    finally:
        await broker.shutdown()


def main() -> None:
    parser = argparse.ArgumentParser(description="开发用 MQTT broker (纯 Python 实现)")
    # 默认 0.0.0.0 = 监听本机全部网卡, 这样局域网里的 ESP32 才连得进来。
    # 想临时只给本机用(比如在公共 WiFi 上做安全演示), 传 --host 127.0.0.1。
    parser.add_argument("--host", default="0.0.0.0", help="监听地址(默认所有网卡)")
    parser.add_argument("--port", type=int, default=1883, help="监听端口")
    args = parser.parse_args()

    # amqtt 内部日志很吵, 调成 WARNING
    logging.basicConfig(level=logging.WARNING)

    try:
        asyncio.run(serve(args.host, args.port))
    except KeyboardInterrupt:
        print("\n[开发 broker] 已退出")


if __name__ == "__main__":
    main()
