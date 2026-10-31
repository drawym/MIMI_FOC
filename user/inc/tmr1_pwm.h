/**
  **************************************************************************
  * @file     tmr1_pwm.h
  * @brief    TMR1 三相中心对称互补 PWM 驱动（FOC 用）
  * @note     适配底板：AT32F435RGT7 + FD6288T 三相栅极驱动
  *           接线（已核对原理图网表）：
  *             PA8  TIM1_CH1  -> FD6288T HIN1 (U相高边 Q7)
  *             PB13 TIM1_CH1N -> FD6288T LIN1 (U相低边 Q8)
  *             PA9  TIM1_CH2  -> FD6288T HIN2 (V相高边 Q9)
  *             PB14 TIM1_CH2N -> FD6288T LIN2 (V相低边 Q10)
  *             PA10 TIM1_CH3  -> FD6288T HIN3 (W相高边 Q11)
  *             PB15 TIM1_CH3N -> FD6288T LIN3 (W相低边 Q12)
  *           全部 MUX1 复用推挽输出
  **************************************************************************
  */

#ifndef __TMR1_PWM_H
#define __TMR1_PWM_H

#include "at32f435_437.h"

/* ----------------------------------------------------------------------- */
/* 时钟与频率参数（依据 user/src/at32f435_437_clock.c 实测配置）            */
/*   SCLK=288MHz, AHB=288MHz, APB2=144MHz                                  */
/*   APB2 分频>1 时 TMR1 时钟 = APB2 x 2 = 288MHz                          */
/* ----------------------------------------------------------------------- */
#define TMR1_CLK_HZ          288000000UL   /* TMR1 计数时钟 */
#define TMR1_PWM_FREQ_HZ     10000UL       /* PWM 频率（中心对齐下 f=CLK/(2*(ARR+1))） 10K */
#define TMR1_PWM_ARR         ((TMR1_CLK_HZ / (2UL * TMR1_PWM_FREQ_HZ)) - 1UL) /* 默认 14399 */

/* 死区：DTC 单位 = 1/TMR1CLK = 3.47ns；DTC=100 即约 347ns */
#define TMR1_DEADTIME_DTC    100UL
#define TMR1_NS_TO_DTC(ns)   ((uint16_t)(((uint64_t)(ns) * TMR1_CLK_HZ) / 1000000000ULL))

/* 通道号（1~3）*/
#define TMR1_CH_U             1
#define TMR1_CH_V             2
#define TMR1_CH_W             3

void tmr1_pwm_init(void);                                /* 默认 10kHz + DTC=100(≈347ns) */
void tmr1_pwm_init_ex(uint32_t freq_hz, uint16_t deadtime_dtc);
void tmr1_pwm_set_ccr(uint8_t ch, uint16_t ccr);          /* 直接写比较值（FOC/SVPWM 直用） */
void tmr1_pwm_set_duty(uint8_t ch, uint16_t permille);    /* 占空比 0~1000（千分比） */
void tmr1_pwm_output_enable(confirm_state en);            /* OEN：输出总开关（急停/恢复） */
void tmr1_pwm_set_update_cb(void (*cb)(void));            /* 注册 TMR1 上溢中断回调（每 PWM 周期调用一次，FOC 步进用） */
void tmr1_pwm_set_midpoint_cb(void (*cb)(void));           /* 注册 CH4 比较中断回调（周期中点前 1 点触发，G4 式，每 PWM 周期一次） */

#endif /* __TMR1_PWM_H */
