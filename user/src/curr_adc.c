/**
  ******************************************************************************
  * @file    curr_adc.c
  * @brief   三相电流(电压)中点采样驱动（ADC1，PC0/1/2）
  *
  * 移植来源：AT32F435_437_Firmware_Library_V2.2.2
  *   examples/adc/tmr_trigger_automatic_preempted
  *
  * 与例程的差异说明：
  *   例程用 TMR1CH4 硬件触发 ADC 抢占组 + DMA 自动搬运；
  *   本工程 TMR1 CH4 已用作"中点比较中断"（G4 式 FOC 同步点），
  *   所以改为【在 CH4 中点中断里软件触发普通组 + 直接读 odt】：
  *     - 采样时刻与例程一致：周期中点（顶点）附近；
  *     - 省掉 DMA 通道和 ADC 中断，代码更短；
  *     - 代价：中断内忙等约 5us（占 10kHz 周期 100us 的 5%，可接受）。
  ******************************************************************************
  */

#include "curr_adc.h"

/* ---- 引脚与通道映射（移植时只需改这里） ---------------------------------- */
#define CURR_ADC             ADC1
#define CURR_ADC_GPIO        GPIOC
#define CURR_ADC_GPIO_PINS   (GPIO_PINS_0 | GPIO_PINS_1 | GPIO_PINS_2)  /* PC0/1/2 */
#define CURR_ADC_CH_U        ADC_CHANNEL_10   /* PC0 */
#define CURR_ADC_CH_V        ADC_CHANNEL_11   /* PC1 */
#define CURR_ADC_CH_W        ADC_CHANNEL_12   /* PC2 */
#define CURR_ADC_SAMPLETIME  ADC_SAMPLETIME_47_5

/* ADC 参考电压（mV），用于码值→毫伏换算 */
#define CURR_ADC_VREF_MV     3300UL
#define CURR_ADC_RES_MAX     4095UL   /* 12 位 */

/* ---- 电流换算参数（对应底板采样电路，移植时按实际改这里） ----
 * 链路：相电流 I → 采样电阻 R14/15/16(5mΩ，低端接 AGND) → 运放差动放大 20 倍
 *       → 输出 = REF_1.65V + I × R × G = 1.65V + I × 0.1V
 * 因此：灵敏度 1A → 100mV；零点偏置 = 1.65V（硬件基准 U7 提供）
 * 电流范围：0~3.3V 对应 -16.5A ~ +16.5A
 */
#define CURR_ADC_RSENSE_MOHM 5UL     /* 采样电阻 5mΩ */
#define CURR_ADC_GAIN        20UL    /* 运放放大倍数 */
#define CURR_ADC_BIAS_MV     1650UL  /* 运放零点偏置（REF_1.65V） */

/* 三相采样结果（最近一次中点采样的原始码值） */
static uint16_t s_curr_raw[3] = {0U, 0U, 0U};

/**
  * @brief  初始化 ADC1 普通组：3 通道序列、软件触发、12 位、校准
  * @retval none
  */
void curr_adc_init(void)
{
  gpio_init_type        gpio_struct;
  adc_common_config_type adc_common_struct;
  adc_base_config_type  adc_base_struct;

  /* ---- 1. 引脚：PC0/1/2 模拟输入 ---- */
  crm_periph_clock_enable(CRM_GPIOC_PERIPH_CLOCK, TRUE);
  gpio_default_para_init(&gpio_struct);
  gpio_struct.gpio_pins = CURR_ADC_GPIO_PINS;
  gpio_struct.gpio_mode = GPIO_MODE_ANALOG;
  gpio_init(CURR_ADC_GPIO, &gpio_struct);

  /* ---- 2. ADC1 时钟 ---- */
  crm_periph_clock_enable(CRM_ADC1_PERIPH_CLOCK, TRUE);

  /* ---- 3. 公共配置：独立模式；ADC 时钟 = HCLK/8 = 36MHz ----
   * HCLK=288MHz；36MHz 留足裕量（例程 /4=72MHz 是上限），噪声更小 */
  adc_common_default_para_init(&adc_common_struct);
  adc_common_struct.combine_mode = ADC_INDEPENDENT_MODE;
  adc_common_struct.div = ADC_HCLK_DIV_8;
  adc_common_struct.common_dma_mode = ADC_COMMON_DMAMODE_DISABLE;
  adc_common_struct.common_dma_request_repeat_state = FALSE;
  adc_common_struct.sampling_interval = ADC_SAMPLING_INTERVAL_5CYCLES;
  adc_common_struct.tempervintrv_state = FALSE;
  adc_common_struct.vbat_state = FALSE;
  adc_common_config(&adc_common_struct);

  /* ---- 4. 基础配置：序列模式（3 通道一次触发全转），12 位 ---- */
  adc_base_default_para_init(&adc_base_struct);
  adc_base_struct.sequence_mode = TRUE;
  adc_base_struct.repeat_mode = FALSE;
  adc_base_struct.data_align = ADC_RIGHT_ALIGNMENT;
  adc_base_struct.ordinary_channel_length = 3;
  adc_base_config(CURR_ADC, &adc_base_struct);
  adc_resolution_set(CURR_ADC, ADC_RESOLUTION_12B);

  /* ---- 5. 普通组通道序列：U→1、V→2、W→3 ---- */
  adc_ordinary_channel_set(CURR_ADC, CURR_ADC_CH_U, 1, CURR_ADC_SAMPLETIME);
  adc_ordinary_channel_set(CURR_ADC, CURR_ADC_CH_V, 2, CURR_ADC_SAMPLETIME);
  adc_ordinary_channel_set(CURR_ADC, CURR_ADC_CH_W, 3, CURR_ADC_SAMPLETIME);

  /* ---- 6. 触发源：禁用硬件触发（EDGE_NONE），只走软件触发 ----
   * 库的触发源枚举里没有显式"软件"，标准做法是选任意源 + 禁用边沿检测 */
  adc_ordinary_conversion_trigger_set(CURR_ADC,
                                      ADC_ORDINARY_TRIG_TMR1CH1,
                                      ADC_ORDINARY_TRIG_EDGE_NONE);

  /* ---- 6.5 关键：OCCE 每通道置位 ----
   * 默认 eocsfen=0 时，OCCE 只在整条序列(3 通道)全部转换完才置一次位，
   * 而普通组只有一个数据寄存器 odt，序列结束时只留最后一通道数据，
   * 逐通道等标志+读 odt 的写法会死等。置 eocsfen=1 后每个通道转换完
   * 都置 OCCE，读 odt 自动清标志 → 循环才能逐通道收数据。 */
  adc_occe_each_conversion_enable(CURR_ADC, TRUE);

  /* ---- 7. 使能 + 上电校准 ---- */
  adc_enable(CURR_ADC, TRUE);
  while(adc_flag_get(CURR_ADC, ADC_RDY_FLAG) == RESET);

  adc_calibration_init(CURR_ADC);
  while(adc_calibration_init_status_get(CURR_ADC));
  adc_calibration_start(CURR_ADC);
  while(adc_calibration_status_get(CURR_ADC));
}

/**
  * @brief  采样三相一次（在 TMR1 中点中断里调用）
  * @note   软件触发启动 3 通道序列 → 逐通道等 OCCE 标志 → 读 odt。
  *         读 odt 会同时清 OCCE 标志（库已确认），所以每次读到的都是新数据。
  * @retval none
  */
void curr_adc_sample(void)
{
  uint8_t i;

  adc_ordinary_software_trigger_enable(CURR_ADC, TRUE);  /* 启动序列 */

  for(i = 0; i < 3; i++)
  {
    while(adc_flag_get(CURR_ADC, ADC_OCCE_FLAG) == RESET)   /* 等本通道转换完成 */
    {
    }
    s_curr_raw[i] = adc_ordinary_conversion_data_get(CURR_ADC);  /* 读 odt 并清标志 */
  }
}

/**
  * @brief  取某相原始码值
  */
uint16_t curr_adc_get_raw(uint8_t phase)
{
  if(phase > CURR_PHASE_W)
  {
    return 0U;
  }
  return s_curr_raw[phase];
}

/**
  * @brief  取某相电压（毫伏）
  */
uint16_t curr_adc_get_mv(uint8_t phase)
{
  return (uint16_t)(((uint32_t)curr_adc_get_raw(phase) * CURR_ADC_VREF_MV)
                    / CURR_ADC_RES_MAX);
}

/**
  * @brief  取某相电流（毫安，带符号：正=流出母线，负=回流）
  * @note   换算：I(mA) = (V_adc_mV - 1.65V偏置) × 1000 / (5mΩ × 20)
  *                = (V_adc_mV - 1650) × 10
  *         停止且无电流时三相应接近 0mA（±几十 mA 噪声属正常）。
  */
int16_t curr_adc_get_amp(uint8_t phase)
{
  int32_t diff_mv;

  if(phase > CURR_PHASE_W)
  {
    return 0;
  }
  diff_mv = (int32_t)curr_adc_get_mv(phase) - (int32_t)CURR_ADC_BIAS_MV;

  return (int16_t)((diff_mv * 1000L) / ((int32_t)CURR_ADC_RSENSE_MOHM
                                        * (int32_t)CURR_ADC_GAIN));
}
