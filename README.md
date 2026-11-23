# 太阳能 12V 双电源自动切换系统

用 **STC8G1K08A** 做 ADC 采样、**ESP-01S + ESPHome** 做判断与联网，实时监测 3S 锂电池电压：低于阈值自动切到 12V 市电适配器，充回目标电压再切回太阳能。路由器全程不断电（超级电容缓冲）。

## 解决什么问题

太阳能 + 12V 电池给路由器供电。冬天日照不足时电池撑不住 24 小时，原本需要人工把路由器电源从电池拔下来插到市电适配器。这个项目把这一下拔插自动化，并保护锂电池不过放。

## 架构

```
                    ┌──────────────────────────────┐
太阳能板 → 控制器 → │  电池 12V (3S Li-ion)        │
                    └──────┬───────────────────────┘
                           │
              ┌────────────┴─────────────┐
              │                          │
        [3A 保险丝]                [150k/27k 分压]
              │                          │
        [继电器 NC]                 STC8 P3.3 (ADC1)
              │
              ├──────────────┬── 路由器 12V
              │              │
        [继电器 NO]    [超级电容组]
              │
        12V 适配器
              │
        [150k/27k 分压]
              │
         STC8 P3.2 (ADC0, 备用)


电池 → 点烟器 12V→5V → AMS1117 3.3V → ESP-01S + STC8
ESP GPIO0 → 继电器 IN（低电平吸合 = 切市电）
ESP GPIO2 → STC8 P5.4 (RST)
```

## 硬件清单

| 部件 | 规格 | 作用 |
|---|---|---|
| STC8G1K08A-8PIN | SOP8，24MHz IRC | 双通道 10 位 ADC |
| ESP-01S | ESP8285，1MB | WiFi + ESPHome |
| 继电器模块 | 单路 SPDT，光耦隔离，3.3V，低电平触发 | 电源切换 |
| 分压电阻 | 150kΩ + 27kΩ，1% 金属膜 | 12V → 1.9V |
| 限流电阻 | 1kΩ | 保护 ADC 引脚 |
| 滤波电容 | 0.1µF 瓷片 | ADC 采样储能 |
| 超级电容组 | 6 串 | 切换瞬间给路由器续命 |
| 点烟器模块 | 12V→5V，9~24V 宽压 | 给系统供电 |
| AMS1117 | 5V→3.3V | 稳压 |
| 保险丝 | 3A | 电池侧保护 |

## 接线

| 从 | 到 | 说明 |
|---|---|---|
| STC8 P3.1 (Pin 6, TX) | ESP GPIO3 (RX) | 9600 8N1 |
| STC8 P3.0 (Pin 5, RX) | ESP GPIO1 (TX) | 接收指令 |
| STC8 P5.4 (Pin 1, RST) | ESP GPIO2（经 10k） | 心跳复位 |
| STC8 P3.3 (Pin 8, ADC1) | 电池分压中点（经 1k） | 主采样通道 |
| STC8 P3.2 (Pin 7, ADC0) | 适配器分压中点（经 1k） | 备用 |
| ESP GPIO0 | 继电器 IN | 低电平吸合 = 切市电 |

分压电路（每路一套）：

```
输入+ ──[150k]──┬──[1k]──┬── STC8 ADC 引脚
                │         │
              [27k]    [0.1µF]
                │         │
               GND       GND
```

## STC8 固件

源码：`stc8_adc_uart/main.c`（v1.3.1，纯 ADC 采样，不参与切换判断）

输出帧（9600 8N1，每 500ms）：

```
ADC0:xxx,ADC1:yyy,VCC:3314,V0:aaa,V1:bbb,SEQ:n,STAT:s
```

| 字段 | 含义 |
|---|---|
| ADC0 / ADC1 | 10 位原始值 0~1023 |
| VCC | STC8 供电 mV（内部 Bandgap 反算） |
| V0 / V1 | 引脚毫伏（分压后） |
| SEQ | 帧序号 0~255 |
| STAT | bit0/bit1 = 对应通道转换超时 |

### 编译

```bash
cd stc8_adc_uart
export PATH="/path/to/sdcc/bin:$PATH"
sdcc --model-small -mmcs51 --out-fmt-ihx -I. main.c -o main.ihx
packihx main.ihx > main.hex
```

### 烧录

这颗芯片（BSL 7.3.13U）**只能用 STC-ISP 烧录**，stcgal 1.10 频率校准握手会失败且不写入任何数据。

1. STC-ISP 选择 `STC8G1K08A-8PIN.cfg` 配置
2. 加载 `main.hex`
3. IRC 选 24.000MHz
4. 取消勾选「复位脚用作 I/O 口」（P5.4 要当 RST 用）
5. 冷启动烧录

免拆机烧录：ESPHome 里点「STC8 进入 ISP 下载模式」（发 `$ISP#`），芯片软复位进 ISP，再点 STC-ISP 下载。

## ESPHome 配置

`8285.yaml`，主要逻辑：

- **采样**：20ms 轮询串口，按行解析
- **滤波**：中值 5 点 + 滑动平均 5 点
- **校准**：`太阳能蓄电池电压 = V1/1000 × 校准系数`（系数可调，默认 6.151）
- **切换**：每 2s 判断一次，低于「切市电阈值」连续 3 次（6s）切市电；高于「切回阈值」连续 5 次（10s）切回太阳能
- **滑块**：阈值和校准系数都能在 HA 里直接调，不用重烧

部署：

```bash
scp 8285.yaml user@server:/path/to/esphome/
docker exec ESPHome esphome compile 8285.yaml
docker exec ESPHome esphome upload 8285.yaml --device <ESP_IP>
```

## 校准

1% 电阻的分压比有 ±2% 误差，必须校准：

1. 万用表测电池实际电压 `V_real`
2. 读 HA 里的 `太阳能蓄电池电压` 为 `V_meas`
3. 新系数 = 当前系数 × `V_real / V_meas`
4. 在 HA 滑块里改

## 注意事项

- **GPIO0 是 ESP8266 启动绑带脚**：上电时必须为高，否则进下载模式。如果继电器模块 IN 有下拉，加 10k 上拉到 3.3V。
- **分压电阻必须 1% 金属膜**，碳膜 5% 在 12V 上是 600mV 误差。
- **超级电容必须有均压电阻**（每节并 100k），否则某一节会过压鼓包。
- **上电限流**：电容从 0V 充电近似短路，串 10Ω/5W 或 NTC。
- **继电器切换有 5~10ms 断开**，靠超级电容缓冲，否则路由器会重启。

## 文件说明

| 文件 | 说明 |
|---|---|
| `stc8_adc_uart/main.c` | STC8 固件源码 |
| `stc8_adc_uart/stc8g.h` | STC8G 寄存器定义 |
| `stc8_adc_uart/STC8G1K08A-8PIN.cfg` | STC-ISP 烧录配置 |
| `8285.yaml` | ESPHome 配置 |
| `wiring_diagram.svg` | 接线图 |
| `PROJECT_OVERVIEW.md` | 从零理解这个项目 |
| `SYSTEM_DESIGN.md` | 系统设计文档 |
| `PROJECT_COMPLETION_REPORT.md` | 结项报告 |
| `flash_stc8.py` / `enter_isp.py` | 烧录辅助脚本（stcgal 对这颗芯片不可用，仅作记录） |

## 许可

MIT
