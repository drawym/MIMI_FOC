#ifndef CURR_ADC_H
#define CURR_ADC_H

/* ============================================================================
 *  curr_adc.h —— 三相电流(电压)中点采样（ADC1）
 *
 *  引脚：PC0=U相、PC1=V相、PC2=W相 → ADC1_IN10/IN11/IN12
 *  时机：TMR1 CH4 中点中断（顶点前1点，每 PWM 周期一次）里调用
 *        curr_adc_sample()，此时三相电流处于 SVPWM 零矢量中点，
 *        纹波最小，是标准 FOC 电流采样点。
 *
 *  用法：
 *    main() 里调用一次 curr_adc_init();
 *    中点中断回调（foc_step）顶部调用 curr_adc_sample();
 *    任意线程用 curr_adc_get_raw()/curr_adc_get_mv() 读取。
 *
 *  注：本驱动只做"采集+存储"，不做电流闭环；
 *      采样电阻电压 → 电流 的换算（需要运放增益/偏置）留给上层。
 * ==========================================================================*/

#include "at32f435_437.h"

/* 三相序号（供 get_raw/get_mv 使用） */
#define CURR_PHASE_U   0
#define CURR_PHASE_V   1
#define CURR_PHASE_W   2

/**
  * @brief  初始化 ADC1：PC0/1/2 → IN10/11/12，普通组 3 通道序列
  * @note   软件触发（禁用硬件触发）；ADC 时钟 = HCLK/8 = 36MHz；
  *         含上电校准。main 里调一次。
  * @retval none
  */
void curr_adc_init(void);

/**
  * @brief  采样三相一次（必须在 TMR1 中点中断里调用）
  * @note   软件触发 3 通道序列，忙等转换完成（约 5us @36MHz），
  *         结果存入内部数组。读 odt 自动清 OCCE 标志。
  * @retval none
  */
void curr_adc_sample(void);

/**
  * @brief  取某相原始码值（12 位，0~4095）
  * @param  phase: CURR_PHASE_U / _V / _W
  * @retval 原始 ADC 码值；phase 非法返回 0
  */
uint16_t curr_adc_get_raw(uint8_t phase);

/**
  * @brief  取某相电压（毫伏，参考 VDD=3.3V 换算）
  * @param  phase: CURR_PHASE_U / _V / _W
  * @retval 毫伏值
  */
uint16_t curr_adc_get_mv(uint8_t phase);

/**
  * @brief  取某相电流（毫安，带符号）
  * @param  phase: CURR_PHASE_U / _V / _W
  * @note   换算基于底板采样链路（R_sense=5mΩ，运放增益20，偏置 REF_1.65V）：
  *           I(mA) = (V_adc_mV - 1650) × 10
  *         零电流时三相应接近 0mA（±几十 mA 属运放/ADC 噪声）。
  * @retval 电流毫安值（可为负）
  */
int16_t curr_adc_get_amp(uint8_t phase);

#endif /* CURR_ADC_H */
