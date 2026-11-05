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
#define CURR_ADC_VREF_MV     3300UL   //取3300是因为采样电阻是5m，是m级别，而宏定义定义的是5m，单位是mΩ，换算成mV时需要乘以1000，所以取3300
#define CURR_ADC_RES_MAX     4095UL   /* 12 位 */

/* ---- 电流换算参数（对应底板采样电路，移植时按实际改这里） ----
 * 链路：相电流 I → 采样电阻 R14/15/16(5mΩ，低端接 AGND) → 运放差动放大 20 倍
 *       → 输出 = REF_1.65V + I × R × G = 1.65V + I × 0.1V
 * 因此：灵敏度 1A → 100mV；零点偏置 = 1.65V（硬件基准 U7 提供）
 * 电流范围：0~3.3V 对应 -16.5A ~ +16.5A
 *
 * 器件确认（原理图 U11/U13/U15）：INA181A1IDBVT，A1 档增益 20V/V；
 * REF 脚接 U7(TLV9001) 缓冲的 R1/R2=20k/20k 分压 = A3.3V/2。
 * ⚠ A3.3V 同时就是 ADC 的 VREF+（U17.13），所以零点天生落在 VREF/2，
 *   与 3.3V 实际是 3.28 还是 3.30 无关（比例式，抗电源漂移）。
 */
#define CURR_ADC_RSENSE_MOHM 5UL     /* 采样电阻 5mΩ */
#define CURR_ADC_GAIN        20UL    /* 运放放大倍数 */
#define CURR_ADC_BIAS_MV     1650UL  /* 运放零点偏置（REF_1.65V） */

/* 标称零点码值：1.65V 折算成 12 位码值 = 1650×4095/3300 = 2048（四舍五入）。
 * 仅作为"未标定时"的默认值，实际使用 curr_adc_calibrate() 测得的 s_offset_code。 */
#define CURR_ADC_BIAS_CODE   ((int16_t)(((int32_t)CURR_ADC_BIAS_MV * (int32_t)CURR_ADC_RES_MAX \
                                         + (int32_t)CURR_ADC_VREF_MV / 2) / (int32_t)CURR_ADC_VREF_MV))

/* 每码值对应的电流（mA）：分子 = VREF_mV × 1000 / (R × G)。
 * 换算公式：mA = delta_code × CURR_ADC_MA_SCALE / 4095
 * 当前参数下 = 3300×1000/(5×20) = 33000 → 1 码 ≈ 8.06mA
 * （换 A2 档增益 50 时自动变成 13200 → 1 码 ≈ 3.22mA，无需改公式）
 *
 * ⚠ 这里的 ×1000L 绝对不能少：
 *   分子是"毫伏 × 1000 / (mΩ × 增益)"，漏掉 1000 的话 SCALE 会变成 33，
 *   而结果是整数除法 —— delta_code 小于 125 时商全是 0，
 *   也就是【电流小于 1A 时读数全部显示 0】，看起来像 ADC 没工作。
 */
#define CURR_ADC_MA_SCALE   (((int32_t)CURR_ADC_VREF_MV * 1000L) / \
                             ((int32_t)CURR_ADC_RSENSE_MOHM * (int32_t)CURR_ADC_GAIN))

/* ---- 零点标定参数 --------------------------------------------------------- */
#define CURR_ADC_CAL_SAMPLES  256U   /* 标定采样次数：256×100us = 25.6ms，覆盖 256 个 PWM 周期 */
#define CURR_ADC_CAL_TOL_CODE 150U   /* 零点允许偏离标称值的码数；超出判为异常（≈1.2A） */

/* ---- 标定用阻塞采样的等待上限 ----------------------------------------------
 * 只在 curr_adc_calibrate() 里用，运行期采样走硬件触发+中断，不需要这个。
 * 2000 次空转约 30~40us（每次约 5 个时钟 @288MHz），比 100us 周期短。 */
#define CURR_ADC_WAIT_MAX     2000U

/* 三相采样结果（最近一次转换的原始码值，由 ADC 中断写入） */
static uint16_t s_curr_raw[3] = {0U, 0U, 0U};

/* ADC 转换完成回调：由 main.c 注册为 foc_step。
 * 放在这里而不是直接 extern foc_step，是为了让本驱动不依赖上层业务。 */
static void (*s_done_cb)(void) = (void *)0;

/* 注入组自检结果：1=软件触发能正常转换；0=注入组配置或 ADC 供电有问题。
 * 在 curr_adc_init() 里做一次，供上层打印，用于快速区分故障来源。 */
static uint8_t s_self_test_ok = 0U;

/* 三相零点码值：上电标定后写入；初值为标称 2048，未标定时也能正常工作 */
static uint16_t s_offset_code[3] = {(uint16_t)CURR_ADC_BIAS_CODE,
                                    (uint16_t)CURR_ADC_BIAS_CODE,
                                    (uint16_t)CURR_ADC_BIAS_CODE};

/**
  * @brief  初始化 ADC1 注入组：3 通道序列、TMR1_CH4 硬件触发、12 位、自校准
  * @note   注入组一次触发连续转换 U/V/W 三通道，转换完成后进 ADC1_2_3 中断，
  *         由中断服务函数读数据并调用上层回调（main 里注册为 foc_step）。
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

  /* ---- 5. 注入组序列：U→1、V→2、W→3 ----
   * 用注入组而不是普通组，因为 AT32 普通组的触发源列表里没有 TMR1_CH4
   * （只有 CH1/CH2/CH3），而注入组支持 ADC_PREEMPT_TRIG_TMR1CH4。
   * 这与 STM32 电机库里「TIM1_CH4 → ADC 注入组 → JEOC 中断」是同一套做法，
   * 而且注入组有独立的完成标志(PCCE)和中断，不会和普通组互相干扰。 */
  adc_preempt_channel_length_set(CURR_ADC, 3U);
  adc_preempt_channel_set(CURR_ADC, CURR_ADC_CH_U, 1U, CURR_ADC_SAMPLETIME);
  adc_preempt_channel_set(CURR_ADC, CURR_ADC_CH_V, 2U, CURR_ADC_SAMPLETIME);
  adc_preempt_channel_set(CURR_ADC, CURR_ADC_CH_W, 3U, CURR_ADC_SAMPLETIME);

  /* ---- 6. 触发源：先禁用边沿 ----
   * 校准期间即便有触发进来也不能让它动作，否则校准数据会被打乱，
   * 所以先设 NONE，等自校准完成后再打开。 */
  adc_preempt_conversion_trigger_set(CURR_ADC,
                                     ADC_PREEMPT_TRIG_TMR1CH4,
                                     ADC_PREEMPT_TRIG_EDGE_NONE);

  /* ---- 7. 使能 + 上电自校准 ---- */
  adc_enable(CURR_ADC, TRUE);
  while(adc_flag_get(CURR_ADC, ADC_RDY_FLAG) == RESET);

  adc_calibration_init(CURR_ADC);
  while(adc_calibration_init_status_get(CURR_ADC));
  adc_calibration_start(CURR_ADC);
  while(adc_calibration_status_get(CURR_ADC));

  /* ---- 8. 自检：软件触发一次，确认注入组真的能转换 ----
   * 这一步放在打开硬件触发之前做，此时唯一的触发来源就是软件触发，
   * 不会有 PWM 的 CH4 事件干扰。
   * 作用是把故障分成两类，省得到处猜：
   *   自检失败 → ADC 注入组本身配错了（通道、长度、使能、VDDA）
   *   自检成功但运行中 irq 一直是 0 → ADC 没问题，是 CH4 触发信号没产生 */
  {
    uint32_t wait_cnt = 30000UL;   /* 上限约 500us，只为覆盖最慢的一次转换 */

    adc_flag_clear(CURR_ADC, ADC_PCCE_FLAG);
    adc_preempt_software_trigger_enable(CURR_ADC, TRUE);

    while(adc_flag_get(CURR_ADC, ADC_PCCE_FLAG) == RESET)
    {
      if(wait_cnt == 0U)
      {
        break;
      }
      wait_cnt--;
    }

    s_self_test_ok = (wait_cnt != 0U) ? 1U : 0U;
    adc_flag_clear(CURR_ADC, ADC_PCCE_FLAG);
  }

  /* ---- 9. 打开硬件触发：TMR1_CH4 上升沿 ----
   * CH4 的 CCR4 = ARR-1（顶点前 1 点），触发时刻正好落在零矢量中点。
   * 从这一行之后，采样启动完全由硬件完成，CPU 不再参与"触发"这个动作。 */
  adc_preempt_conversion_trigger_set(CURR_ADC,
                                     ADC_PREEMPT_TRIG_TMR1CH4,
                                     ADC_PREEMPT_TRIG_EDGE_RISING);

  /* ---- 10. 注入组转换完成中断（PCCE）---- */
  adc_flag_clear(CURR_ADC, ADC_PCCE_FLAG);
  adc_interrupt_enable(CURR_ADC, ADC_PCCE_INT, TRUE);
  /* 优先级必须是 1：SEGGER_RTT 的临界区用 BASEPRI=0x20（等于优先级 2）屏蔽中断，
   * 若设成 2 或更低，主循环一打印就会把这个中断挡在门外，FOC 时序立刻乱。 */
  nvic_irq_enable(ADC1_2_3_IRQn, 1, 0);
}

/**
  * @brief  注册 ADC 转换完成回调（在 ADC1_2_3 中断上下文执行）
  * @param  cb 回调函数指针（禁止阻塞/打印；传 NULL 取消）
  * @retval none
  */
void curr_adc_set_done_callback(void (*cb)(void))
{
  s_done_cb = cb;
}

/**
  * @brief  取注入组自检结果（curr_adc_init 里做的那次软件触发）
  * @retval 1=注入组能正常转换；0=注入组配置或 ADC 供电有问题
  * @note   运行中如果 irq 一直不动，先看这个值：
  *         0 → 问题在 ADC 本身（通道/长度/VDDA）；
  *         1 → ADC 没问题，是 CH4 的触发信号没送到。
  */
uint8_t curr_adc_get_self_test(void)
{
  return s_self_test_ok;
}

/**
  * @brief  ADC1/2/3 中断服务函数：注入组转换完成 → 读三相数据 → 跑 FOC
  * @note   进入本中断时，三个通道的转换结果已经躺在 JDT1~JDT3 里了，
  *         这里只做"搬数据 + 通知上层"，没有任何等待，执行时间恒定。
  *         这就是硬件触发相比软件轮询最大的好处：CPU 永远不会卡在等标志上。
  * @retval none
  */
void ADC1_2_3_IRQHandler(void)
{
  if(adc_flag_get(CURR_ADC, ADC_PCCE_FLAG) != RESET)
  {
    adc_flag_clear(CURR_ADC, ADC_PCCE_FLAG);   /* 先清标志，避免重复进入 */

    /* 注入组数据寄存器与序列位置一一对应：序列1→JDT1→U相，以此类推 */
    s_curr_raw[CURR_PHASE_U] = adc_preempt_conversion_data_get(CURR_ADC,
                                                              ADC_PREEMPT_CHANNEL_1);
    s_curr_raw[CURR_PHASE_V] = adc_preempt_conversion_data_get(CURR_ADC,
                                                              ADC_PREEMPT_CHANNEL_2);
    s_curr_raw[CURR_PHASE_W] = adc_preempt_conversion_data_get(CURR_ADC,
                                                              ADC_PREEMPT_CHANNEL_3);

    if(s_done_cb != (void *)0)
    {
      s_done_cb();
    }
  }
}

/**
  * @brief  软件触发一次三相采样并等待完成（仅零点标定使用，故设为 static）
  * @note   运行期不要调用：正常采样由 TMR1_CH4 硬件触发 + ADC 中断完成，
  *         这里再插一次软件触发会和硬件触发抢同一个注入组。
  *         等待带次数上限，ADC 不响应时约 30us 后放弃，沿用上次数据。
  */
static void curr_adc_sample(void)
{
  uint32_t wait_cnt;   /* 剩余等待次数，递减到 0 即放弃 */

  adc_flag_clear(CURR_ADC, ADC_PCCE_FLAG);
  adc_preempt_software_trigger_enable(CURR_ADC, TRUE);

  wait_cnt = CURR_ADC_WAIT_MAX;
  while(adc_flag_get(CURR_ADC, ADC_PCCE_FLAG) == RESET)
  {
    if(wait_cnt == 0U)
    {
      break;          /* 超时：保留上一次的码值，绝不在这里死等 */
    }
    wait_cnt--;
  }
  adc_flag_clear(CURR_ADC, ADC_PCCE_FLAG);

  s_curr_raw[CURR_PHASE_U] = adc_preempt_conversion_data_get(CURR_ADC,
                                                            ADC_PREEMPT_CHANNEL_1);
  s_curr_raw[CURR_PHASE_V] = adc_preempt_conversion_data_get(CURR_ADC,
                                                            ADC_PREEMPT_CHANNEL_2);
  s_curr_raw[CURR_PHASE_W] = adc_preempt_conversion_data_get(CURR_ADC,
                                                            ADC_PREEMPT_CHANNEL_3);
}

/**
  * @brief  零点标定：记录电机静止、零电流时每相对应的 ADC 码值
  * @note   前置条件（缺一不可）：电机静止、三相 CCR=50% 零矢量。
  *         此时采样电阻上无相电流，读到的码值就是"零点"，
  *         它包含 1.65V 标称偏置 + INA181 输入失调 + 分压电阻误差 + ADC 失调。
  *         标定后 curr_adc_get_amp() 直接减这个实测零点，
  *         可把 ±20~50mA 的直流偏差压到 ±1 码（约 8mA）。
  * @retval 1=标定成功；0=某相偏差过大（多半是标定时电机在转），已回退标称零点
  */
uint8_t curr_adc_calibrate(void)
{
  uint32_t sum_code[3]  = {0U, 0U, 0U};   /* 三相码值累加和（先累加后除法，避免逐次截断） */
  uint16_t avg_code[3];                   /* 三相平均码值（即标定得到的零点） */
  uint16_t sample_index;                  /* 采样序号 */
  uint8_t  phase;                         /* 相序号：0=U 1=V 2=W */
  int32_t  deviation_code;                /* 实测零点与标称零点的偏差（码） */
  uint8_t  is_ok = 1U;                    /* 1=标定有效；0=某相异常，将回退标称值 */

  /* ---- 标定期间必须独占 ADC ----
   * PWM 每 100us 会触发一次注入组；若不禁用，软件触发和硬件触发会抢同一个
   * 序列，读出的三相是两次转换混在一起的数据，标定结果直接作废。 */
  adc_interrupt_enable(CURR_ADC, ADC_PCCE_INT, FALSE);
  adc_preempt_conversion_trigger_set(CURR_ADC,
                                     ADC_PREEMPT_TRIG_TMR1CH4,
                                     ADC_PREEMPT_TRIG_EDGE_NONE);

  for (sample_index = 0U; sample_index < CURR_ADC_CAL_SAMPLES; sample_index++)
  {
    curr_adc_sample();                    /* 软件触发一次，三相各采一次 */

    for (phase = 0U; phase < 3U; phase++)
    {
      sum_code[phase] += s_curr_raw[phase];
    }
  }

  /* 加半个分母再除，等于四舍五入，避免一律向下取整 */
  for (phase = 0U; phase < 3U; phase++)
  {
    avg_code[phase] = (uint16_t)((sum_code[phase] + CURR_ADC_CAL_SAMPLES / 2U)
                                 / CURR_ADC_CAL_SAMPLES);
  }

  /* 先校验再落盘：任一路异常则整体不采纳，避免写入无效零点造成读数偏移 */
  for (phase = 0U; phase < 3U; phase++)
  {
    deviation_code = (int32_t)avg_code[phase] - (int32_t)CURR_ADC_BIAS_CODE;

    if ((deviation_code > (int32_t)CURR_ADC_CAL_TOL_CODE) ||
        (deviation_code < -(int32_t)CURR_ADC_CAL_TOL_CODE))
    {
      is_ok = 0U;
      break;
    }
  }

  if (is_ok != 0U)
  {
    s_offset_code[0] = avg_code[0];
    s_offset_code[1] = avg_code[1];
    s_offset_code[2] = avg_code[2];
  }
  else
  {
    s_offset_code[0] = (uint16_t)CURR_ADC_BIAS_CODE;
    s_offset_code[1] = (uint16_t)CURR_ADC_BIAS_CODE;
    s_offset_code[2] = (uint16_t)CURR_ADC_BIAS_CODE;
  }

  /* ---- 归还 ADC：恢复硬件触发 + 完成中断 ---- */
  adc_flag_clear(CURR_ADC, ADC_PCCE_FLAG);
  adc_preempt_conversion_trigger_set(CURR_ADC,
                                     ADC_PREEMPT_TRIG_TMR1CH4,
                                     ADC_PREEMPT_TRIG_EDGE_RISING);
  adc_interrupt_enable(CURR_ADC, ADC_PCCE_INT, TRUE);

  return is_ok;
}

/**
  * @brief  取某相零点码值（调试打印用）
  */
uint16_t curr_adc_get_offset_code(uint8_t phase)
{
  if (phase > CURR_PHASE_W)
  {
    return 0U;
  }
  return s_offset_code[phase];
}

/**
  * @brief  取某相电流（mA，带符号：正=流出桥臂，负=回流）
  * @note   换算：mA = (raw − offset) × 33000 / 4095，1 码 ≈ 8.06mA。
  *         分子最大 4095×33000 ≈ 1.35e8，int32 装得下。
  * @retval 电流毫安值；phase 非法返回 0
  */
int16_t curr_adc_get_amp(uint8_t phase)
{
  int32_t delta_code;   /* 相对零点的码值差 */

  if (phase > CURR_PHASE_W)
  {
    return 0;           /* 越界保护：一旦传错相号，读数组外会算出离谱电流 */
  }

  delta_code = (int32_t)s_curr_raw[phase] - (int32_t)s_offset_code[phase];

  return (int16_t)((delta_code * CURR_ADC_MA_SCALE) / (int32_t)CURR_ADC_RES_MAX);
}
