# -*- coding: utf-8 -*-
"""抓取 ESP32 串口输出若干秒, 用于验证烧录后的运行日志。"""
import sys
import time
import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else 'COM3'
SECONDS = float(sys.argv[2]) if len(sys.argv) > 2 else 8.0
DO_RESET = (len(sys.argv) <= 3) or (sys.argv[3] != '0')   # 第 3 个参数传 0 表示不复位

ser = serial.Serial(PORT, 115200, timeout=0.5)
ser.setDTR(False)   # 避免打开串口时把芯片拉进下载模式
ser.setRTS(False)
time.sleep(0.2)

if DO_RESET:
    # 通过 RTS 控制 EN 引脚, 复现"按一下 RST 键"的效果,
    # 这样能抓到从 bootloader 开始的完整启动日志
    ser.setRTS(True)
    time.sleep(0.15)
    ser.setRTS(False)

ser.reset_input_buffer()
buf = bytearray()
end = time.time() + SECONDS
while time.time() < end:
    chunk = ser.read(4096)
    if chunk:
        buf.extend(chunk)
ser.close()
sys.stdout.write(buf.decode('utf-8', 'replace'))
