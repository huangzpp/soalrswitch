/*
 * stc8g.h — STC8G 系列 SDCC 寄存器定义
 * 适用: STC8G1K08A, STC8G1K08, STC8G2K64S4 等 STC8G 系列
 */

#ifndef __STC8G_H
#define __STC8G_H

#include <compiler.h>  /* SDCC mcs51 编译器内置 */

/* ══════════════════════════════════════════════
 *  标准 8051 SFR (地址兼容)
 * ══════════════════════════════════════════════ */
SFR(P0,    0x80);
SFR(SP,    0x81);
SFR(DPL,   0x82);
SFR(DPH,   0x83);
SFR(PCON,  0x87);
SFR(TCON,  0x88);
SFR(TMOD,  0x89);
SFR(TL0,   0x8A);
SFR(TL1,   0x8B);
SFR(TH0,   0x8C);
SFR(TH1,   0x8D);
SFR(AUXR,  0x8E);
SFR(INTCLKO, 0x8F);
SFR(P1,    0x90);
SFR(P1M1,  0x91);
SFR(P1M0,  0x92);
SFR(P0M1,  0x93);
SFR(P0M0,  0x94);
SFR(P2M1,  0x95);
SFR(P2M0,  0x96);
SFR(SCON,  0x98);
SFR(SBUF,  0x99);
SFR(S2CON, 0x9A);
SFR(S2BUF, 0x9B);
SFR(P1ASF, 0x9D);
SFR(P2,    0xA0);
SFR(BUS_SPEED, 0xA1);
SFR(P_SW1, 0xA2);
SFR(IE,    0xA8);
SFR(SADDR, 0xA9);
SFR(WKTCL, 0xAA);
SFR(WKTCH, 0xAB);
SFR(S3CON, 0xAC);
SFR(S3BUF, 0xAD);
SFR(IE2,   0xAF);
SFR(P3,    0xB0);
SFR(P3M1,  0xB1);
SFR(P3M0,  0xB2);
SFR(P4M1,  0xB3);
SFR(P4M0,  0xB4);
SFR(P5,    0xB5); /* STC8G 8pin 无 P5 但保留 */
SFR(P5M1,  0xB6);
SFR(P5M0,  0xB7);
SFR(IP,    0xB8);
SFR(SADEN, 0xB9);
SFR(P_SW2, 0xBA);
SFR(ADC_CONTR, 0xBC);
SFR(ADC_RES,   0xBD);
SFR(ADC_RESL,  0xBE);
SFR(P4,    0xC0);
SFR(WDT_CONTR, 0xC1);
SFR(IAP_DATA,  0xC2);
SFR(IAP_ADDRH, 0xC3);
SFR(IAP_ADDRL, 0xC4);
SFR(IAP_CMD,   0xC5);
SFR(IAP_TRIG,  0xC6);
SFR(IAP_CONTR, 0xC7);
/* P5 at 0xC8 reserved for STC8G compatibility - defined above at 0xB5 */
SFR(SPSTAT, 0xCD);
SFR(SPCTL,  0xCE);
SFR(SPDAT,  0xCF);
SFR(PSW,   0xD0);
SFR(T4T3M, 0xD1);
SFR(T4H,   0xD2);
SFR(T4L,   0xD3);
SFR(T3H,   0xD4);
SFR(T3L,   0xD5);
SFR(T2H,   0xD6);
SFR(T2L,   0xD7);
SFR(ACC,   0xE0);
SFR(P7,    0xE8);
SFR(ADCCFG, 0xDE); /* ADC 配置寄存器 */
SFR(B,     0xF0);
SFR(PWMCFG, 0xF1);
SFR(PCA_PWM0, 0xF2);
SFR(PWMIF,  0xF6);
SFR(PWMFDCR, 0xF7);
SFR(P6,    0xF8);

/* ══════════════════════════════════════════════
 *  IE 位定义
 * ══════════════════════════════════════════════ */
SBIT(EA,   0xA8, 7);
SBIT(ELVD, 0xA8, 6);
SBIT(EADC, 0xA8, 5);
SBIT(ES,   0xA8, 4);
SBIT(ET1,  0xA8, 3);
SBIT(EX1,  0xA8, 2);
SBIT(ET0,  0xA8, 1);
SBIT(EX0,  0xA8, 0);

/* ══════════════════════════════════════════════
 *  TCON 位定义
 * ══════════════════════════════════════════════ */
SBIT(TF1, 0x88, 7);
SBIT(TR1, 0x88, 6);
SBIT(TF0, 0x88, 5);
SBIT(TR0, 0x88, 4);
SBIT(IE1, 0x88, 3);
SBIT(IT1, 0x88, 2);
SBIT(IE0, 0x88, 1);
SBIT(IT0, 0x88, 0);

/* ══════════════════════════════════════════════
 *  SCON 位定义
 * ══════════════════════════════════════════════ */
SBIT(SM0, 0x98, 7);
SBIT(SM1, 0x98, 6);
SBIT(SM2, 0x98, 5);
SBIT(REN, 0x98, 4);
SBIT(TB8, 0x98, 3);
SBIT(RB8, 0x98, 2);
SBIT(TI,  0x98, 1);
SBIT(RI,  0x98, 0);

/* ══════════════════════════════════════════════
 *  P3 位定义
 * ══════════════════════════════════════════════ */
SBIT(RXD, 0xB0, 0);  /* P3.0 RXD */
SBIT(TXD, 0xB0, 1);  /* P3.1 TXD */
SBIT(INT0, 0xB0, 2); /* P3.2 INT0 / ADC2 */
SBIT(INT1, 0xB0, 3); /* P3.3 INT1 / ADC3 */
SBIT(T0,   0xB0, 4);
SBIT(T1,   0xB0, 5);

/* ══════════════════════════════════════════════
 *  PSW 位定义
 * ══════════════════════════════════════════════ */
SBIT(CY,  0xD0, 7);
SBIT(AC,  0xD0, 6);
SBIT(F0,  0xD0, 5);
SBIT(RS1, 0xD0, 4);
SBIT(RS0, 0xD0, 3);
SBIT(OV,  0xD0, 2);
SBIT(F1,  0xD0, 1);
SBIT(P,   0xD0, 0);

/* ══════════════════════════════════════════════
 *  内置 nop
 * ══════════════════════════════════════════════ */
#define _nop_() __asm nop __endasm

#endif /* __STC8G_H */
