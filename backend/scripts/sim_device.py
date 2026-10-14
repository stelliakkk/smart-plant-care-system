# -*- coding: utf-8 -*-
"""
虚拟边缘端设备(没有硬件时用它顶替 ESP32)
=====================================================================
作用: 冒充 ESP32 往 MQTT 上发传感器数据, 让后端和前端可以先行开发,
      不用干等板子接好。它发的报文格式与真实固件完全一致。

用法(在 backend/ 目录下):
    .\\.venv\\Scripts\\python.exe scripts\\sim_device.py
    .\\.venv\\Scripts\\python.exe scripts\\sim_device.py --interval 2 --device-id edge01

它模拟的行为:
    - 每 interval 秒发一次 plant/{device_id}/telemetry
    - 每 5 次上报附带一次 plant/{device_id}/status 心跳
    - 订阅 plant/{device_id}/cmd, 收到指令后等 duration_ms 再回 cmd_ack
    - 收到 pump 指令后会真的把"土壤湿度"拉上去, 让闭环看起来是通的

按 Ctrl+C 退出。
=====================================================================
"""
import argparse
import json
import math
import random
import signal
import sys
import threading
import time
from datetime import datetime

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("缺少 paho-mqtt, 请先执行: pip install paho-mqtt")
    sys.exit(1)


class FakeSensors:
    """
    让假数据看起来像真的:
        土壤湿度 —— 随蒸发缓慢下降, 收到补水指令后回升
        光照     —— 正弦模拟昼夜变化 + 随机噪声
        温度     —— 围绕 24°C 缓慢波动
        盐分     —— 施肥时上升, 其余时间缓慢下降
    """

    def __init__(self) -> None:
        self.soil_moisture = 45.0
        self.soil_ec = 1.2
        self.temperature = 24.0
        self._tick = 0

    def step(self) -> dict:
        """产生一组新的模拟读数。"""
        self._tick += 1
        # 蒸发: 每次掉一点, 但不低于 5%
        self.soil_moisture = max(5.0, self.soil_moisture - random.uniform(0.4, 1.5))
        # 光照: 正弦模拟昼夜
        lux = 8000 * math.sin(self._tick / 40.0) + random.uniform(-400, 400)
        lux = max(0.0, lux)
        # 温度小幅波动
        self.temperature += random.uniform(-0.3, 0.3)
        self.temperature = max(15.0, min(35.0, self.temperature))

        return {
            "ts": datetime.now().isoformat(timespec="seconds"),
            "soil_moisture": round(self.soil_moisture, 1),
            "soil_ec": round(self.soil_ec, 2),
            "temperature": round(self.temperature, 1),
            "light_lux": round(lux, 0),
        }

    def apply_actuator(self, actuator: str) -> None:
        """执行器动作对"环境"产生的影响, 让闭环演示看起来是通的。"""
        if actuator == "pump":
            self.soil_moisture = min(75.0, self.soil_moisture + random.uniform(6, 12))
        elif actuator == "fertilizer":
            self.soil_ec = min(4.0, self.soil_ec + random.uniform(0.2, 0.5))
        elif actuator == "grow_light":
            print("[模拟] 补光灯开启")


def main() -> None:
    parser = argparse.ArgumentParser(description="虚拟 ESP32 边缘端设备")
    parser.add_argument("--host", default="127.0.0.1", help="MQTT broker 地址")
    parser.add_argument("--port", type=int, default=1883, help="MQTT broker 端口")
    parser.add_argument("--device-id", default="edge01", help="设备编号")
    parser.add_argument("--interval", type=float, default=3.0, help="上报间隔(秒)")
    args = parser.parse_args()

    sensors = FakeSensors()
    counter = {"n": 0}
    started_at = time.time()

    telemetry_topic = f"plant/{args.device_id}/telemetry"
    status_topic = f"plant/{args.device_id}/status"
    cmd_topic = f"plant/{args.device_id}/cmd"
    ack_topic = f"plant/{args.device_id}/cmd_ack"

    # --------------------------- MQTT 回调 ---------------------------
    def on_connect(client, userdata, flags, reason_code, properties=None):
        if getattr(reason_code, "is_failure", False):
            print(f"[MQTT] 连接被拒绝: {reason_code}")
            return
        print(f"[MQTT] 已连接 {args.host}:{args.port}")
        client.subscribe(cmd_topic, qos=0)
        print(f"[MQTT] 已订阅指令主题 {cmd_topic}")

    def on_disconnect(client, userdata, flags, reason_code, properties=None):
        if getattr(reason_code, "is_failure", False):
            print(f"[MQTT] 连接断开({reason_code}), 正在自动重连...")

    def on_message(client, userdata, msg):
        """收到后端下发的执行器指令。"""
        try:
            cmd = json.loads(msg.payload.decode("utf-8", "replace"))
        except json.JSONDecodeError:
            print("[CMD] 收到非 JSON 报文, 已忽略")
            return

        actuator = cmd.get("actuator", "?")
        action = cmd.get("action", "?")
        duration_ms = int(cmd.get("duration_ms", 0) or 0)
        source = cmd.get("source", "manual")
        print(f"[CMD] {actuator} {action} {duration_ms}ms (来源: {source})")

        def finish():
            if action == "on":
                sensors.apply_actuator(actuator)
            ack = {
                "actuator": actuator,
                "result": "done",
                "reason": f"模拟执行 {duration_ms}ms",
                "ts": datetime.now().isoformat(timespec="seconds"),
            }
            client.publish(ack_topic, json.dumps(ack, ensure_ascii=False))
            print(f"[ACK] 已回执: {ack['actuator']} -> {ack['result']}")

        # 用定时器模拟"执行耗时", 避免阻塞 paho 的网络线程
        if duration_ms > 0:
            threading.Timer(duration_ms / 1000.0, finish).start()
        else:
            finish()

    # --------------------------- 启动 ---------------------------
    client = mqtt.Client(
        mqtt.CallbackAPIVersion.VERSION2, client_id=f"sim-{args.device_id}"
    )
    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    client.on_message = on_message
    client.reconnect_delay_set(min_delay=1, max_delay=15)

    try:
        client.connect(args.host, args.port, keepalive=60)
    except Exception as exc:
        print(f"[MQTT] 无法连接 {args.host}:{args.port} -> {exc}")
        print("       请先确认 Mosquitto 已启动:")
        print("       D:\\SmartAgriBrainRuntime\\mosquitto\\mosquitto.exe -c mosquitto.conf")
        sys.exit(2)

    client.loop_start()

    def on_exit(signum, frame):
        print("\n[退出] 正在断开 ...")
        client.loop_stop()
        client.disconnect()
        sys.exit(0)

    signal.signal(signal.SIGINT, on_exit)

    print(f"[模拟] 开始上报: 间隔 {args.interval}s, 设备 {args.device_id}, Ctrl+C 退出")
    try:
        while True:
            data = sensors.step()
            data["device_id"] = args.device_id
            client.publish(telemetry_topic, json.dumps(data, ensure_ascii=False))
            counter["n"] += 1
            print(
                f"[{counter['n']:>4}] 湿度 {data['soil_moisture']:>5}%  "
                f"盐分 {data['soil_ec']:>5}  温度 {data['temperature']:>5}C  "
                f"光照 {data['light_lux']:>7} lux"
            )

            # 每 5 次发一次心跳
            if counter["n"] % 5 == 0:
                status = {
                    "device_id": args.device_id,
                    "online": True,
                    "rssi": random.randint(-70, -40),
                    "uptime_s": int(time.time() - started_at),
                    "fault": "",
                }
                client.publish(status_topic, json.dumps(status, ensure_ascii=False))
                print(f"[心跳] 已上报 {status_topic}")

            time.sleep(args.interval)
    except KeyboardInterrupt:
        on_exit(None, None)


if __name__ == "__main__":
    main()
