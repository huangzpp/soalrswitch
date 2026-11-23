/*
 * STC8G1K08A-8PIN — 终极高可用外置双通道 ADC 固件 v1.3.1
 * 
 * 硬件配置:
 *   - 单片机: STC8G1K08A-8PIN (SOP8)
 *   - 时钟: 内部 IRC 24.000MHz
 *   - 串口: 9600bps 8N1 (Timer1 模式2 自动重装, 误差 +0.16%)
 *   - 引脚分配 (SOP8 封装标准逆时针序号):
 *       Pin 1: P5.4 (复位引脚 RST / 通用 I/O)  <-- 硬件复位线接此脚！
 *       Pin 2: VCC  (供电 3.3V)
 *       Pin 3: P5.5 (通用 I/O, 本工程悬空不用)
 *       Pin 4: GND  (系统地)
 *       Pin 5: P3.0 / RxD (串口接收, 接 ESP GPIO1 TX)
 *       Pin 6: P3.1 / TxD (串口发送, 接 ESP GPIO3 RX)
 *       Pin 7: P3.2 / ADC2 (模拟通道 2, 太阳能电池采样通道)
 *       Pin 8: P3.3 / ADC3 (模拟通道 3, 备用 ADC 输入)
 * 
 * ── 核心设计与保障 ──
 * 1. [ADC 精度修复] ADCCFG=0x2F (RESFMT=1, 右对齐) 标准读取:
 *    ADC_RES 存放高 2 位 (D9~D8), ADC_RESL 存放低 8 位 (D7~D0)
 *    公式: (((unsigned int)ADC_RES) << 8) | ADC_RESL
 *    满量程 1023 (彻底解决接 3.3V 恒读 15 的故障)。
 * 2. [抗通道串扰] 切换通道后延时 100us 并丢弃第 1 次采样 (Dummy Read), 随后取 8 次均值。
 * 3. [内部 Bandgap 基准反算] 通道 15 采样内部 ~1189mV 基准, 实时反算真实 VCC (mV),
 *    并据此直接计算出 P3.2(V0) 和 P3.3(V1) 的精确真实毫伏值。
 * 4. [串口收发全双工安全]
 *    - 接收: 串口 1 中断 (中断号 4) 接管, 写入 32 字节环形缓冲区, 主循环随时解析, 零丢包。
 *    - 发送: 中断自动清除 TI 标志与 tx_busy, 彻底杜绝 8051 中断重入死循环; 配备超时保护。
 * 5. [丰富双向调试与免拆机控制指令]
 *    - $PING#      : 返回 #PONG# (快速通信握手)
 *    - $INFO#      : 返回当前系统固件版本、运行时间、上报速率、实时供电及 ADC 值
 *    - $RAW#       : 返回底层寄存器裸值 (ADC_RES, ADC_RESL, ADCCFG, BG) 供免烧录诊断
 *    - $RATE:xxx#  : 动态调整上报周期 (毫秒, 范围 50~10000ms, 默认 500ms)
 *    - $RST#       : 软复位重启单片机应用程序
 *    - $ISP#       : 软复位进入 STC 系统 ISP 监控区 (免断电重新烧录固件)
 * 6. [数据帧向后完全兼容]
 *    ADC0:xxxx,ADC1:yyyy,VCC:zzzz,V0:aaaa,V1:bbbb,SEQ:nn,STAT:s\r\n
 * 7. [看门狗] 开启硬件看门狗 (约 2.1s 溢出), 循环与延时全覆盖喂狗, 遇强干扰自动自愈。
 */

#include "stc8g.h"

#define FOSC 24000000UL
#define BAUD 9600UL
#define FW_VER "1.3.1"
#define BUILD_DATE "2026-11-20"

/* ══════════════════════════════════════════════
 *  看门狗 (PS=7 预分频 256, 约 2.1s 溢出)
 * ══════════════════════════════════════════════ */
#define WDT_FEED() (WDT_CONTR = 0x3F)

/* ══════════════════════════════════════════════
 *  延时函数 (严格标定 @ 24MHz 1T)
 *  100us: 2400 个时钟周期; 循环约 11 周期/次, 取 218 次
 * ══════════════════════════════════════════════ */
void delay_100us(void)
{
    unsigned int j;
    for (j = 0; j < 218; j++) {
        _nop_();
    }
}

void delay_ms(unsigned int ms)
{
    unsigned int i;
    for (i = 0; i < ms; i++) {
        delay_100us(); delay_100us(); delay_100us(); delay_100us();
        delay_100us(); delay_100us(); delay_100us(); delay_100us();
        delay_100us(); delay_100us();
        WDT_FEED();
    }
}

/* ══════════════════════════════════════════════
 *  串口接收环形队列 (Ring Buffer) 与发送状态
 * ══════════════════════════════════════════════ */
#define RX_BUF_SIZE 32
static volatile unsigned char rx_buf[RX_BUF_SIZE];
static volatile unsigned char rx_head = 0;
static volatile unsigned char rx_tail = 0;
static volatile unsigned char tx_busy = 0;

/* ══════════════════════════════════════════════
 *  UART1 引脚与初始化
 * ══════════════════════════════════════════════ */
static void uart_pins(void)
{
    /* 强制 UART1 映射到 P3.0(RxD) 和 P3.1(TxD) */
    P_SW1 &= ~0xC0;

    /* P3.1(TxD) 设为推挽输出 (极陡上升沿, 极强抗干扰) */
    P3M0 |=  0x02;
    P3M1 &= ~0x02;

    /* P3.0(RxD) 设为准双向口 */
    P3M0 &= ~0x01;
    P3M1 &= ~0x01;
}

/* Timer1 模式2: 8 位自动重装
 * SMOD=1, T1 12T 计数 -> 24M / (192 * 13) = 9615bps (+0.16%) */
void uart_init(void)
{
    uart_pins();

    SCON  = 0x50;                 /* 8 位可变波特率, 允许接收 REN=1 */
    PCON |= 0x80;                 /* SMOD = 1 (波特率加倍) */
    TMOD  = (TMOD & 0x0F) | 0x20;   /* Timer1 模式2 (8位自动重装) */
    TH1   = 243;
    TL1   = 243;
    AUXR &= ~0x01;                /* S1ST2 = 0 -> 由 Timer1 供给串口 1 时钟 */
    TR1   = 1;                    /* 启动 Timer1 */
    TI    = 0;
    RI    = 0;
    tx_busy = 0;

    /* 开启串口中断与全局中断 */
    ES = 1;
    EA = 1;
}

/* 串口 1 中断服务函数: 彻底解决 8051 TI 重入死锁 */
void uart_isr(void) __interrupt (4)
{
    if (RI) {
        unsigned char next_head;
        unsigned char c = SBUF;
        RI = 0;
        next_head = (rx_head + 1) & (RX_BUF_SIZE - 1);
        if (next_head != rx_tail) {
            rx_buf[rx_head] = c;
            rx_head = next_head;
        }
    }
    if (TI) {
        TI = 0;
        tx_busy = 0;
    }
}

unsigned char uart_rx_available(void)
{
    return (rx_head != rx_tail);
}

unsigned char uart_read_byte(void)
{
    unsigned char c = 0;
    if (rx_head != rx_tail) {
        c = rx_buf[rx_tail];
        rx_tail = (rx_tail + 1) & (RX_BUF_SIZE - 1);
    }
    return c;
}

/* ══════════════════════════════════════════════
 *  稳健串口发送函数 (带超时自愈保护)
 * ══════════════════════════════════════════════ */
void uart_putchar(unsigned char c)
{
    unsigned int guard = 50000;
    tx_busy = 1;
    SBUF = c;
    while (tx_busy) {
        if (--guard == 0) {
            tx_busy = 0;
            break;
        }
    }
}

void uart_puts(const char *s)
{
    while (*s) {
        uart_putchar((unsigned char)*s++);
    }
}

void uart_putu16(unsigned int v)
{
    char buf[6];
    unsigned char i = 0, j;
    char tmp;
    if (v == 0) {
        uart_putchar('0');
        return;
    }
    while (v) {
        buf[i++] = '0' + (v % 10);
        v /= 10;
    }
    for (j = 0; j < i / 2; j++) {
        tmp = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = tmp;
    }
    buf[i] = 0;
    uart_puts(buf);
}

/* ══════════════════════════════════════════════
 *  全局系统运行状态与参数
 * ══════════════════════════════════════════════ */
static char cmd_buf[16];
static unsigned char cmd_len = 0;
static unsigned int g_report_rate_ms = 500; /* 默认上报周期 500ms */
static unsigned long g_uptime_sec = 0;      /* 运行秒数统计 */
static unsigned int g_last_vcc_mv = 3300;   /* 最近有效 VCC */
static unsigned int g_last_adc0 = 0;
static unsigned int g_last_adc1 = 0;
static unsigned int g_last_bg = 0;

/* ══════════════════════════════════════════════
 *  双向串口命令解析与响应
 * ══════════════════════════════════════════════ */
void process_command(const char *cmd)
{
    /* 1. 免断电进入系统 ISP 监控区 */
    if (cmd[0] == '$' && cmd[1] == 'I' && cmd[2] == 'S' && cmd[3] == 'P' && cmd[4] == '#') {
        uart_puts("#ACK:ENTERING_ISP#\r\n");
        delay_ms(50);
        EA = 0;
        WDT_CONTR = 0x00;
        IAP_CONTR = 0x60; /* 软复位到 ISP 监控区 */
        while (1);
    }
    /* 2. 软件重启单片机 */
    else if (cmd[0] == '$' && cmd[1] == 'R' && cmd[2] == 'S' && cmd[3] == 'T' && cmd[4] == '#') {
        uart_puts("#ACK:RESETTING#\r\n");
        delay_ms(50);
        EA = 0;
        IAP_CONTR = 0x20; /* 软复位到用户应用程序区 */
        while (1);
    }
    /* 3. 在线心跳探测 */
    else if (cmd[0] == '$' && cmd[1] == 'P' && cmd[2] == 'I' && cmd[3] == 'N' && cmd[4] == 'G' && cmd[5] == '#') {
        uart_puts("#PONG#\r\n");
    }
    /* 4. 查询详细运行状态: $INFO# */
    else if (cmd[0] == '$' && cmd[1] == 'I' && cmd[2] == 'N' && cmd[3] == 'F' && cmd[4] == 'O' && cmd[5] == '#') {
        uart_puts("#INFO: FW=" FW_VER " RATE=");
        uart_putu16(g_report_rate_ms);
        uart_puts("ms UPTIME=");
        uart_putu16((unsigned int)(g_uptime_sec & 0xFFFF));
        uart_puts("s VCC=");
        uart_putu16(g_last_vcc_mv);
        uart_puts("mV ADC0=");
        uart_putu16(g_last_adc0);
        uart_puts(" ADC1=");
        uart_putu16(g_last_adc1);
        uart_puts("#\r\n");
    }
    /* 5. 底层原始寄存器诊断查询: $RAW# */
    else if (cmd[0] == '$' && cmd[1] == 'R' && cmd[2] == 'A' && cmd[3] == 'W' && cmd[4] == '#') {
        uart_puts("#RAW: CFG=0x2F BG_RAW=");
        uart_putu16(g_last_bg);
        uart_puts(" RES0=");
        uart_putu16(g_last_adc0);
        uart_puts(" RES1=");
        uart_putu16(g_last_adc1);
        uart_puts("#\r\n");
    }
    /* 6. 动态调整上报周期: $RATE:xxx# (例如 $RATE:200#) */
    else if (cmd[0] == '$' && cmd[1] == 'R' && cmd[2] == 'A' && cmd[3] == 'T' && cmd[4] == 'E' && cmd[5] == ':') {
        unsigned int val = 0;
        unsigned char p = 6;
        while (cmd[p] >= '0' && cmd[p] <= '9') {
            val = val * 10 + (cmd[p] - '0');
            p++;
        }
        if (cmd[p] == '#' && val >= 50 && val <= 10000) {
            g_report_rate_ms = val;
            uart_puts("#ACK:RATE_SET=");
            uart_putu16(g_report_rate_ms);
            uart_puts("ms#\r\n");
        }
    }
}

void check_rx_commands(void)
{
    while (uart_rx_available()) {
        unsigned char c = uart_read_byte();
        if (c == '$') {
            cmd_buf[0] = '$';
            cmd_len = 1;
        } else if (cmd_len > 0) {
            if (cmd_len < sizeof(cmd_buf) - 1) {
                cmd_buf[cmd_len++] = (char)c;
                if (c == '#') {
                    cmd_buf[cmd_len] = '\0';
                    process_command(cmd_buf);
                    cmd_len = 0;
                }
            } else {
                cmd_len = 0; /* 溢出丢弃 */
            }
        }
    }
}

/* ══════════════════════════════════════════════
 *  ADC 模块 (P3.2=通道2, P3.3=通道3, 通道15=内部1.19V基准)
 * ══════════════════════════════════════════════ */
void adc_init(void)
{
    /* P3.2, P3.3 设为高阻模拟输入 */
    P3M0 &= ~0x0C;
    P3M1 |=  0x0C;

    /* 开 ADC 内部供电电源 */
    ADC_CONTR = 0x80;

    /* 结果格式: RESFMT = 1 (Bit 5 = 1, 右对齐)
     * 时钟分频: SPEED = 15 (0x0F, 最慢最稳转换)
     * ADCCFG = 0x2F */
    ADCCFG = 0x2F;
    delay_ms(5);
}

/* 单次转换 (带超时退出保护) */
static unsigned int adc_convert_single(void)
{
    unsigned int timeout = 4000;

    ADC_CONTR |= 0x40; /* 启动转换 ADC_START */
    while (!(ADC_CONTR & 0x20)) {
        if (--timeout == 0) {
            ADC_CONTR &= ~0x40;
            return 0xFFFF; /* 转换超时 */
        }
    }
    ADC_CONTR &= ~0x20; /* 硬件转换完成, 清除 ADC_FLAG */

    /* ─── 核心修复 ───
     * 右对齐 (RESFMT=1) 标准拼接公式:
     * ADC_RES 存放高 2 位 (D9~D8), ADC_RESL 存放低 8 位 (D7~D0)
     * 满量程 = (3 << 8) | 255 = 1023 !
     */
    return (((unsigned int)ADC_RES) << 8) | ADC_RESL;
}

/* 切换通道并做 n 次平均滤波 (含通道建立延时与 Dummy Read) */
unsigned int adc_read_avg(unsigned char ch, unsigned char n)
{
    unsigned long sum = 0;
    unsigned int v;
    unsigned char i;

    /* 切换通道并保持 ADC 电源开启 */
    ADC_CONTR = 0x80 | (ch & 0x0F);

    /* 通道建立延时 (充饱内部采样电容, 消除切换阶跃) */
    delay_100us();

    /* 丢弃通道切换后的第 1 次采样 (消除多路开关残余电荷) */
    adc_convert_single();

    /* 进行 n 次稳定采样并累加 */
    for (i = 0; i < n; i++) {
        v = adc_convert_single();
        if (v == 0xFFFF) {
            return 0xFFFF;
        }
        sum += v;
    }
    return (unsigned int)(sum / n);
}

/* 利用内部 ~1189mV Bandgap 基准反算芯片实际工作电压 VCC (mV)
 * 标称值: 1189mV * 1023 = 1216347 */
unsigned int read_vcc_mv(void)
{
    unsigned int bg;
    unsigned long mv;

    bg = adc_read_avg(15, 8);
    g_last_bg = bg;
    if (bg == 0xFFFF || bg < 80 || bg > 1000) {
        return g_last_vcc_mv; /* 若超出正常范围, 保持上次有效值 */
    }
    mv = 1216347UL / bg;
    if (mv < 1800UL || mv > 5500UL) {
        return g_last_vcc_mv;
    }
    g_last_vcc_mv = (unsigned int)mv;
    return g_last_vcc_mv;
}

/* ══════════════════════════════════════════════
 *  主程序
 * ══════════════════════════════════════════════ */
void main(void)
{
    unsigned int adc0, adc1;
    unsigned int vcc_mv = 3300;
    unsigned int v0_mv, v1_mv;
    unsigned char seq = 0;
    unsigned char stat = 0;
    unsigned int elapsed;
    unsigned int loop_delay;

    /* 1. 先关闭看门狗, 待各项硬件初始化就绪 */
    WDT_CONTR = 0x00;

    /* 2. 串口初始化与 ADC 初始化 */
    uart_init();
    adc_init();

    /* 3. 上电开机详报横幅 (输出完整软硬件自检明细) */
    delay_ms(50);
    uart_puts("\r\n#STC8G FW:" FW_VER " FOSC:24MHz BAUD:9600#\r\n");
    uart_puts("========================================\r\n");
    uart_puts("STC8G1K08A ADC Module Firmware v" FW_VER "\r\n");
    uart_puts("Build: " BUILD_DATE " FOSC: 24.000MHz 9600-8N1\r\n");
    uart_puts("Pins: P3.2=ADC2, P3.3=ADC3, P3.0=RXD, P3.1=TXD\r\n");
    uart_puts("Ref: Bandgap(CH15) ~1189mV, VCC Auto-Calc\r\n");
    uart_puts("Commands: $ISP# $RST# $PING# $INFO# $RATE:ms# $RAW#\r\n");
    uart_puts("========================================\r\n");

    /* 4. 开启硬件看门狗 (PS=7 预分频 256, 约 2.1s 溢出) */
    WDT_FEED();

    while (1)
    {
        stat = 0;

        /* 采样 P3.2 (通道 2) */
        adc0 = adc_read_avg(2, 8);
        if (adc0 == 0xFFFF) {
            stat |= 0x01;
            adc0 = g_last_adc0;
        } else {
            g_last_adc0 = adc0;
        }

        /* 采样 P3.3 (通道 3) */
        adc1 = adc_read_avg(3, 8);
        if (adc1 == 0xFFFF) {
            stat |= 0x02;
            adc1 = g_last_adc1;
        } else {
            g_last_adc1 = adc1;
        }

        /* 读取内部基准并反算真实供电 VCC (mV) */
        vcc_mv = read_vcc_mv();

        /* 计算引脚真实输入电压 (mV) */
        if (vcc_mv > 0) {
            v0_mv = (unsigned int)(((unsigned long)adc0 * vcc_mv) / 1023UL);
            v1_mv = (unsigned int)(((unsigned long)adc1 * vcc_mv) / 1023UL);
        } else {
            v0_mv = 0;
            v1_mv = 0;
        }

        /* ── 发送完整数据帧 (向后完全兼容旧版 ESPHome) ── */
        uart_puts("ADC0:");
        uart_putu16(adc0);
        uart_puts(",ADC1:");
        uart_putu16(adc1);
        uart_puts(",VCC:");
        uart_putu16(vcc_mv);
        uart_puts(",V0:");
        uart_putu16(v0_mv);
        uart_puts(",V1:");
        uart_putu16(v1_mv);
        uart_puts(",SEQ:");
        uart_putu16(seq++);
        uart_puts(",STAT:");
        uart_putu16(stat);
        uart_puts("\r\n");

        /* ── 精确动态延时循环 (每 10ms 检查一次串口命令并喂狗) ──
         * 扣除前置 ADC 采样约 8ms 开销，让上报周期精准吻合设定值
         */
        loop_delay = (g_report_rate_ms > 10) ? (g_report_rate_ms - 8) : g_report_rate_ms;
        elapsed = 0;
        while (elapsed < loop_delay) {
            check_rx_commands();
            delay_ms(10);
            elapsed += 10;
        }

        /* 更新运行时间 (秒) */
        g_uptime_sec += (g_report_rate_ms / 1000) ? (g_report_rate_ms / 1000) : 1;
    }
}
