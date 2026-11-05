/**
  **************************************************************************
  * @file     tmr1_pwm.c
  * @brief    TMR1 三相中心对称互补 PWM（10kHz，FOC 用）
  *
  *  关键配置：
  *    - 中心对齐计数 TMR_COUNT_TWO_WAY_1：载波呈三角波，相电流纹波对称，
  *      周期中点就是零矢量中心，正好拿来采电流
  *    - PWM_MODE_A + 高有效：FD6288T 的 HIN/LIN 为同相输入
  *    - CCR 预装载打开：新占空比在下次更新事件生效，波形不会出现怪脉冲
  *    - CH4 只产生内部比较事件，硬件直连 ADC 注入组，不占用中断
  **************************************************************************
  */

#include "tmr1_pwm.h"

/* TMR1 输出引脚 */
#define TMR1_OUT_GPIOA_PINS   (GPIO_PINS_8 | GPIO_PINS_9 | GPIO_PINS_10)  /* CH1/2/3 */
#define TMR1_OUT_GPIOB_PINS   (GPIO_PINS_13 | GPIO_PINS_14 | GPIO_PINS_15) /* CH1N/2N/3N */

static uint16_t s_pwm_arr = TMR1_PWM_ARR;   /* 当前 ARR，供占空比换算 */

/**
  * @brief  TMR1 三相中心对称互补 PWM 初始化（10kHz + 死区）
  * @note   上电顺序有讲究：GPIO 先输出低（关断）→ 配 TMR → 切 MUX → 最后开总闸，
  *         全程六路输入保持低电平，MOS 不会误动。
  * @retval none
  */
void tmr1_pwm_init(void)
{
  gpio_init_type gpio_init_struct;
  tmr_output_config_type tmr_output_struct;
  tmr_brkdt_config_type tmr_brkdt_struct;
  uint32_t period;

  /* ---- 1. 时钟 ---- */
  crm_periph_clock_enable(CRM_TMR1_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);

  /* ---- 2. 输出引脚先当普通推挽输出并拉低（上电安全：关断） ---- */
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins = TMR1_OUT_GPIOA_PINS;
  gpio_init_struct.gpio_mode = GPIO_MODE_OUTPUT;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(GPIOA, &gpio_init_struct);
  gpio_bits_reset(GPIOA, TMR1_OUT_GPIOA_PINS);

  gpio_init_struct.gpio_pins = TMR1_OUT_GPIOB_PINS;
  gpio_init(GPIOB, &gpio_init_struct);
  gpio_bits_reset(GPIOB, TMR1_OUT_GPIOB_PINS);

  /* ---- 3. 时基：中心对齐，f = CLK/(2×(ARR+1)) ---- */
  period = (TMR1_CLK_HZ / (2UL * TMR1_PWM_FREQ_HZ)) - 1UL;
  s_pwm_arr = (uint16_t)period;

  tmr_base_init(TMR1, (uint16_t)period, 0);       /* 预分频 0，计数时钟 288MHz */
  tmr_cnt_dir_set(TMR1, TMR_COUNT_TWO_WAY_1);     /* 中心对齐模式 1（上溢更新） */

  /* ---- 4. 三相互补输出 ---- */
  tmr_output_default_para_init(&tmr_output_struct);
  tmr_output_struct.oc_mode = TMR_OUTPUT_CONTROL_PWM_MODE_A;  /* CNT<CCR 输出高 */
  tmr_output_struct.oc_output_state = TRUE;
  tmr_output_struct.occ_output_state = TRUE;
  tmr_output_struct.oc_polarity = TMR_OUTPUT_ACTIVE_HIGH;     /* FD6288T 高电平导通 */
  tmr_output_struct.occ_polarity = TMR_OUTPUT_ACTIVE_HIGH;
  tmr_output_struct.oc_idle_state = FALSE;                    /* 空闲低电平（关断） */
  tmr_output_struct.occ_idle_state = FALSE;

  tmr_output_channel_config(TMR1, TMR_SELECT_CHANNEL_1, &tmr_output_struct);
  tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_1, (uint16_t)(period / 2));
  tmr_output_channel_config(TMR1, TMR_SELECT_CHANNEL_2, &tmr_output_struct);
  tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_2, (uint16_t)(period / 2));
  tmr_output_channel_config(TMR1, TMR_SELECT_CHANNEL_3, &tmr_output_struct);
  tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_3, (uint16_t)(period / 2));

  /* ---- 5. CCR 预装载（影子寄存器） ----
   * 打开后写 CCR 只是写预装载寄存器，要等下次更新事件（下溢）才生效。
   * 必要性：FOC 在中断里算完立刻写 CCR，此时计数器已从顶点下行一段，
   * 若立即生效，可能在本周期内多翻一次电平，出现不对称的怪脉冲。 */
  tmr_output_channel_buffer_enable(TMR1, TMR_SELECT_CHANNEL_1, TRUE);
  tmr_output_channel_buffer_enable(TMR1, TMR_SELECT_CHANNEL_2, TRUE);
  tmr_output_channel_buffer_enable(TMR1, TMR_SELECT_CHANNEL_3, TRUE);

  /* ---- 6. 死区 + 刹车 ---- */
  tmr_brkdt_default_para_init(&tmr_brkdt_struct);
  tmr_brkdt_struct.deadtime = (uint8_t)TMR1_DEADTIME_DTC;   /* DTC × 3.47ns ≈ 347ns */
  tmr_brkdt_struct.brk_enable = FALSE;                      /* 暂无外部比较器接 BKIN */
  tmr_brkdt_struct.fcsodis_state = TRUE;                    /* OEN=0 时输出空闲低电平 */
  tmr_brkdt_struct.fcsoen_state = TRUE;
  tmr_brkdt_struct.auto_output_enable = FALSE;              /* 故障后须软件复位 */
  tmr_brkdt_struct.brk_polarity = TMR_BRK_INPUT_ACTIVE_HIGH;
  tmr_brkdt_struct.wp_level = TMR_WP_OFF;                   /* 开发期不锁寄存器 */
  tmr_brkdt_config(TMR1, &tmr_brkdt_struct);

  /* ---- 7. GPIO 切 MUX1（此时 OEN=0，引脚仍输出 idle 低电平） ---- */
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins = TMR1_OUT_GPIOA_PINS;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(GPIOA, &gpio_init_struct);
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE8, GPIO_MUX_1);
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE9, GPIO_MUX_1);
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE10, GPIO_MUX_1);

  gpio_init_struct.gpio_pins = TMR1_OUT_GPIOB_PINS;
  gpio_init(GPIOB, &gpio_init_struct);
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE13, GPIO_MUX_1);
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE14, GPIO_MUX_1);
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE15, GPIO_MUX_1);

  /* ---- 8. CH4：顶点比较事件，硬件直连 ADC 注入组 ----
   * PWM_MODE_B 下 OC4REF 在 CNT > CCR4 时有效，CCR4 = ARR−1，
   * 于是上坡走到顶点时产生一次上升沿；ADC 只检测上升沿，故每周期触发一次。
   * ⚠ oc_output_state 必须为 TRUE（对应 OC4E 位）：
   *   OC4E=0 时比较中断照样能进，但 OC4REF 这个内部信号不产生，
   *   ADC 就永远收不到触发。CH4 未做 GPIO 映射，开了也不会有引脚输出。 */
  tmr_output_struct.oc_mode = TMR_OUTPUT_CONTROL_PWM_MODE_B;
  tmr_output_struct.oc_output_state = TRUE;
  tmr_output_struct.occ_output_state = FALSE;
  tmr_output_struct.oc_polarity = TMR_OUTPUT_ACTIVE_HIGH;
  tmr_output_channel_config(TMR1, TMR_SELECT_CHANNEL_4, &tmr_output_struct);
  tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_4, (uint16_t)(s_pwm_arr - 1U));

  /* ---- 9. 开总闸并启动计数 ---- */
  tmr_output_enable(TMR1, TRUE);
  tmr_counter_enable(TMR1, TRUE);
}

/**
  * @brief  设置某相占空比（SVPWM 直接调用）
  * @param  ch  通道号：1=U 2=V 3=W
  * @param  ccr 比较值 0~ARR，duty = CCR/ARR
  * @retval none
  */
void tmr1_pwm_set_ccr(uint8_t ch, uint16_t ccr)
{
  if (ccr > s_pwm_arr)
  {
    ccr = s_pwm_arr;
  }

  switch (ch)
  {
    case 1: tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_1, ccr); break;
    case 2: tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_2, ccr); break;
    case 3: tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_3, ccr); break;
    default: break;
  }
}

/**
  * @brief  主输出使能（急停用）
  * @param  en TRUE=输出 PWM；FALSE=六路全落到空闲低电平，桥臂彻底断开
  * @note   与"三相 50% 零矢量"不同：零矢量仍让桥臂在开关，
  *         电机惯性旋转时会被短路制动；这里直接关 MOE，六管全关，可自由滑行。
  * @retval none
  */
void tmr1_pwm_output_enable(confirm_state en)
{
  tmr_output_enable(TMR1, en);
}
