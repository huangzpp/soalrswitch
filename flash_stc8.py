"""
STC8G1K08A-8PIN 烧录脚本

关键点:
  -t 24000000  显式把 IRC 校准到 24.000MHz。**本芯片不适用**：
               芯片存储频率为 23.995MHz，强 trim 到 24.000MHz 会报
               "frequency trimming unsuccessful" 并中止烧录（未写入任何数据）。
               不传 -t 时 stcgal 会沿用芯片内存的 23.995MHz，误差 0.02%，
               对 9600bps 完全可以忽略。
  -b 115200    ISP 阶段通信速率（不是用户程序的 9600）。
  烧录需要冷启动: 运行脚本后给核心板断电再上电。
"""
import os
import sys
import subprocess

STCGAL = r"C:\Users\HZP\AppData\Local\Programs\Python\Python312\Scripts\stcgal.exe"
PORT   = os.environ.get("STC8_PORT", "COM9")
HEX    = os.path.join(os.path.dirname(os.path.abspath(__file__)), "stc8_adc_uart", "main.hex")
TRIM   = None      # 不要设成 "24000000"，见文件头说明
WAIT_S = 60

if not os.path.exists(HEX):
    sys.exit(f"找不到固件: {HEX}")

print("=" * 58)
print("  烧录 STC8G1K08A-8PIN")
print(f"  端口: {PORT}")
print(f"  固件: {HEX}  ({os.path.getsize(HEX)} 字节)")
print(f"  校准: {TRIM if TRIM else '沿用芯片存储值 (23.995MHz)'}")
print("=" * 58)
print()
print(">>> 现在给核心板【断电 -> 重新上电】，等待握手 <<<")
print()

cmd = [STCGAL, "-P", "stc8g", "-p", PORT, "-b", "115200"]
if TRIM:
    cmd += ["-t", TRIM]
cmd.append(HEX)
proc = subprocess.Popen(cmd)
try:
    rc = proc.wait(timeout=WAIT_S)
    if rc == 0:
        print("\n[OK] 烧录成功")
    else:
        print(f"\n[FAIL] 烧录失败，退出码 {rc}")
        sys.exit(rc)
except subprocess.TimeoutExpired:
    proc.terminate()
    print(f"\n[TIMEOUT] {WAIT_S} 秒内没检测到冷启动上电")
    sys.exit(1)
