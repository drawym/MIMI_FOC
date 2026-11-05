#ifndef CURR_ADC_H
#define CURR_ADC_H

/* ============================================================================
 *  curr_adc.h —— 三相电流采样（ADC1 注入组，TMR1_CH4 硬件触发）
 *
 *  引脚：PC0=U相、PC1=V相、PC2=W相 → ADC1_IN10/IN11/IN12
 *
 *  采样时序（标准 FOC 做法，全程硬件联动，CPU 不参与触发）：
 *      TMR1 计数器升到 CCR4（=ARR-1，顶点前1点）
 *          → CH4 比较事件（硬件直连，不经中断）
 *          → ADC 注入组自动启动，连续转换 U/V/W 三通道
 *          → 转换完成置 PCCE 标志
 *          → 进 ADC1_2_3_IRQHandler
 *          → 读三相数据 + 调用已注册的回调（foc_step）
 *
 *  为什么必须在中点采样：
 *      零矢量期间三相等电位、线电压为 0，电流纹波是以中点对称的三角波，
 *      只有在中点采，瞬时值才等于平均值；偏出去就会采到开关沿的共模尖峰。
 *
 *  用法：
 *      curr_adc_init();                        // main 里调一次
 *      curr_adc_calibrate();                   // 电机静止时标定零点
 *      curr_adc_set_done_callback(foc_step);   // 注册 FOC 步进函数
 *      // 之后无需任何轮询，中断会自动驱动 foc_step
 * ==========================================================================*/

#include "at32f435_437.h"

/* 三相序号（供 get_raw/get_mv 使用） */
#define CURR_PHASE_U   0
#define CURR_PHASE_V   1
#define CURR_PHASE_W   2

/**
  * @brief  初始化 ADC1 注入组：PC0/1/2 → IN10/11/12，3 通道序列
  * @note   触发源 = TMR1_CH4 上升沿（周期中点）；ADC 时钟 = HCLK/8 = 36MHz；
  *         含上电自校准。末尾会打开 PCCE 中断并配置 NVIC。main 里调一次。
  * @retval none
  */
void curr_adc_init(void);

/**
  * @brief  注册"三相数据就绪"回调（在 ADC 中断上下文执行）
  * @param  cb 回调函数指针；传 NULL 取消注册
  * @note   回调里禁止阻塞/打印，典型实现就是 FOC 的 foc_step()。
  *         ADC 中断已经把三相数据读进内部数组，回调里直接用
  *         curr_adc_get_amp() 取值即可，不需要再触发采样。
  * @retval none
  */
void curr_adc_set_done_callback(void (*cb)(void));

/**
  * @brief  取注入组自检结果（init 里那次软件触发是否成功）
  * @retval 1=注入组能正常转换；0=注入组配置或 ADC 供电有问题
  * @note   运行中 irq 不动时先看它：0 查 ADC 本身，1 查 CH4 触发信号。
  */
uint8_t curr_adc_get_self_test(void);

/**
  * @brief  零点标定：记录电机静止时每相"零电流"对应的 ADC 码值
  * @note   必须在【电机静止、三相 50% 零矢量】时调用，否则标定值无效。
  *         内部采样 256 次取平均（约 1.3ms），标定后 curr_adc_get_amp()
  *         自动减去该零点，消除 INA181 失调与分压电阻带来的 ±20~50mA 偏差。
  * @retval 1=成功；0=某相偏离标称值过多（多半标定时电机在转），已回退到 2048
  */
uint8_t curr_adc_calibrate(void);

/**
  * @brief  取某相零点码值（调试打印用）
  * @retval 12 位码值；未标定时为 2048；phase 非法返回 0
  */
uint16_t curr_adc_get_offset_code(uint8_t phase);

/**
  * @brief  取某相电流（mA，带符号：正=流出桥臂，负=回流）
  * @note   换算：mA = (raw − offset) × 33000 / 4095 ≈ 1 码 8.06mA。
  *         零电流时三相应接近 0mA（标定后仅剩 ±8~16mA 噪声）。
  * @retval 电流毫安值；phase 非法返回 0
  */
int16_t curr_adc_get_amp(uint8_t phase);

#endif /* CURR_ADC_H */
