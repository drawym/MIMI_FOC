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
  * @brief  零点标定：记录电机静止时每相"零电流"对应的 ADC 码值
  * @note   ⚠ 必须在【电机静止、三相 CCR=50% 零矢量】时调用，否则标定值无效。
  *         在 main() 里 curr_adc_init() 之后、按键启动电机之前调用一次。
  *         标定后 curr_adc_get_amp() 自动减去该零点，
  *         可消除 INA181 输入失调 + 分压电阻误差带来的 ±20~50mA 直流偏差。
  *         内部采样 256 次取平均，耗时约 1.3ms。
  * @retval 1=标定成功；0=某相零点偏离标称值过多（多半标定时电机在转），
  *         已自动回退到标称零点 2048，功能不受影响但精度未改善
  */
uint8_t curr_adc_calibrate(void);

/**
  * @brief  取某相当前使用的零点码值（调试/打印用）
  * @param  phase: CURR_PHASE_U / _V / _W
  * @retval 12 位码值；未标定时为标称值 2048；phase 非法返回 0
  */
uint16_t curr_adc_get_offset_code(uint8_t phase);

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
  * @note   换算基于底板采样链路（R_sense=5mΩ，INA181 增益 20，零点 1.65V）：
  *           I(mA) = (raw - offset_code) × 33000 / 4095 ≈ (raw - offset) × 8.06
  *         其中 offset_code 由 curr_adc_calibrate() 实测得到（已含 1.65V 偏置），
  *         未标定时默认为标称值 2048。
  *         零电流时三相应接近 0mA（标定后仅剩 ±8~16mA 噪声）。
  * @retval 电流毫安值（可为负：负=电流从地回流到桥臂）
  */
int16_t curr_adc_get_amp(uint8_t phase);

#endif /* CURR_ADC_H */
