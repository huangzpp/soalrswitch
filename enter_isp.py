"""
让 STC8G 免断电进入 ISP 模式，然后直接用 STC-ISP 下载。

原理:
  固件主循环每 delay_ms(20)(实测 20.8ms) 才调一次 check_isp_cmd()，
  而 8051 的 UART 只有 SBUF 一级缓冲、没有 FIFO。
  9600bps 下字节间隔 1.04ms，连发 5 字节必然溢出丢字节，
  所以必须逐字节发送、每字节间隔 > 21ms。

用法:
  1. 确认 ESP-01S 已断电或从 P3.0/P3.1 断开（否则 ESP 的 TX 会顶住 P3.0）
  2. python enter_isp.py            # 默认 COM9
     STC8_PORT=COM5 python enter_isp.py
  3. 看到 ENTERING_ISP 后，立刻在 STC-ISP 里点「下载/编程」
     —— 不需要再给板子断电

注意:
  进入 ISP 后如果没下载任何东西，芯片会停在 ISP 监控区，
  此时必须断电重启才能回到用户程序。
"""
import os
import sys
import time

import serial

PORT = os.environ.get("STC8_PORT", "COM9")
GAP = 0.06  # 必须 > 主循环轮询间隔 20.8ms，留足余量

ser = serial.Serial(PORT, 9600, timeout=0.3)
ser.reset_input_buffer()
ser.reset_output_buffer()

print(f"[{PORT}] 逐字节发送 $ISP# (间隔 {GAP*1000:.0f}ms) ...")
for ch in b"$ISP#":
    ser.write(bytes([ch]))
    ser.flush()
    time.sleep(GAP)

time.sleep(0.3)
resp = ser.read(64)
ser.close()

if b"ENTERING_ISP" in resp:
    print("[OK] 芯片已回复 ENTERING_ISP，现在去 STC-ISP 点下载（无需断电）")
    sys.exit(0)

print("[FAIL] 没有收到 ENTERING_ISP")
print("       回应:", resp)
print("       可能原因: ESP 的 TX 顶住了 P3.0 / 固件不是新版本 / 串口接错")
sys.exit(1)
