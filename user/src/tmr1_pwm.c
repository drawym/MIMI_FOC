/**
  **************************************************************************
  * @file     tmr1_pwm.c
  * @brief    TMR1 三相中心对称互补 PWM 驱动（FOC 用）
  * @note
  *   1. 配置要点（与 STM32 笔记的对应关系见注释）：
  *      - 中心对齐计数 TMR_COUNT_TWO_WAY_1（等价 ST 中央对齐模式1，上溢产生更新）
  *      - PWM_MODE_A：中心对齐下有效区间围绕三角波顶点对称
  *        （PWM_A：上坡 CNT>CCR 有效；下坡 CNT>=CCR 有效 → 导通脉冲以中点对称）
  *      - 极性 ACTIVE_HIGH：FD6288T 的 HIN/LIN 为同相输入（高电平导通）
  *      - 空闲电平均设 FALSE：OEN 清零（急停/刹车）时 6 路输出低 → 上下管全关断
  *   2. 死区：TMR1 死区 + FD6288T 内置死区双保险；DTC 单位 3.47ns @288MHz
  *   3. 刹车：底板当前无外部比较器（PB12 悬空），brk_enable 保持 FALSE；
  *      将来接入过流比较器到 PB12 后，把 brk_enable 置 TRUE 并配刹车中断即可
  *   4. 上电安全顺序：GPIO 先输出低（关断）→ 配 TMR → 切 MUX（OEN=0 仍输出低）
  *      → 最后开 OEN，全程 6 路输入保持低电平，MOS 不会误动
  **************************************************************************
  */

#include "tmr1_pwm.h"

/* TMR1 输出引脚 */
#define TMR1_OUT_GPIOA_PINS   (GPIO_PINS_8 | GPIO_PINS_9 | GPIO_PINS_10)  /* CH1/2/3 */
#define TMR1_OUT_GPIOB_PINS   (GPIO_PINS_13 | GPIO_PINS_14 | GPIO_PINS_15) /* CH1N/2N/3N */

static uint16_t s_pwm_arr = TMR1_PWM_ARR;   /* 当前 ARR，供占空比换算 */

static void (*s_update_cb)(void) = (void *)0;   /* TMR1 上溢中断回调（FOC 步进），默认无 */
static void (*s_midpoint_cb)(void) = (void *)0;  /* CH4 比较中断回调（周期中点前1点触发），默认无 */

/**
  * @brief  TMR1 三相中心对称互补 PWM 初始化
  * @param  freq_hz     PWM 频率（中心对齐：f = TMR1CLK/(2*(ARR+1))）
  * @param  deadtime_dtc 死区寄存器值 DTC[7:0]，单位 1/TMR1CLK = 3.47ns
  * @retval none
  */
void tmr1_pwm_init_ex(uint32_t freq_hz, uint16_t deadtime_dtc)
{
  gpio_init_type gpio_init_struct;
  tmr_output_config_type tmr_output_struct;
  tmr_brkdt_config_type tmr_brkdt_struct;
  uint32_t period = 0;

  /* ---------- 1. 使能外设时钟 ---------- */
  crm_periph_clock_enable(CRM_TMR1_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);

  /* ---------- 2. 输出引脚先配为推挽输出并拉低（上电安全：关断） ---------- */
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins = TMR1_OUT_GPIOA_PINS;
  gpio_init_struct.gpio_mode = GPIO_MODE_OUTPUT;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(GPIOA, &gpio_init_struct);
  gpio_bits_reset(GPIOA, TMR1_OUT_GPIOA_PINS);   /* 输出低 */

  gpio_init_struct.gpio_pins = TMR1_OUT_GPIOB_PINS;
  gpio_init(GPIOB, &gpio_init_struct);
  gpio_bits_reset(GPIOB, TMR1_OUT_GPIOB_PINS);   /* 输出低 */

  /* ---------- 3. 时基：中心对齐，f = CLK/(2*(ARR+1)) ---------- */
  period = (TMR1_CLK_HZ / (2UL * freq_hz)) - 1UL;
  s_pwm_arr = (uint16_t)period;

  tmr_base_init(TMR1, (uint16_t)period, 0);                    /* 预分频=0，计数时钟 288MHz */
  tmr_cnt_dir_set(TMR1, TMR_COUNT_TWO_WAY_1);                  /* 中心对齐模式1（上溢更新） */

  /* ---------- 4. 通道输出配置（三相互补，PWM_MODE_A + 高有效） ---------- */
  tmr_output_default_para_init(&tmr_output_struct);
  tmr_output_struct.oc_mode = TMR_OUTPUT_CONTROL_PWM_MODE_A;    /* CNT<CCR 输出高（手册定义）：有效区间围绕谷底 = 等价 ST PWM1（同 G4 工程） */
  tmr_output_struct.oc_output_state = TRUE;                    /* 主通道使能 CxE */
  tmr_output_struct.occ_output_state = TRUE;                   /* 互补通道使能 CxNE */
  tmr_output_struct.oc_polarity = TMR_OUTPUT_ACTIVE_HIGH;      /* 有效=高：FD6288T 高电平导通 */
  tmr_output_struct.occ_polarity = TMR_OUTPUT_ACTIVE_HIGH;     /* 互补同相，同样高有效 */
  tmr_output_struct.oc_idle_state = FALSE;                     /* 空闲输出低（关断） */
  tmr_output_struct.occ_idle_state = FALSE;                    /* 空闲输出低（关断） */

  /* 三个通道同一套配置 */
  tmr_output_channel_config(TMR1, TMR_SELECT_CHANNEL_1, &tmr_output_struct);
  tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_1, (uint16_t)(period / 2));  /* 初始 50% */
  tmr_output_channel_config(TMR1, TMR_SELECT_CHANNEL_2, &tmr_output_struct);
  tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_2, (uint16_t)(period / 2));
  tmr_output_channel_config(TMR1, TMR_SELECT_CHANNEL_3, &tmr_output_struct);
  tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_3, (uint16_t)(period / 2));

  /* ---------- 4.5 比较值预装载（影子寄存器） ----------
   * 打开后，写 CCR 只是写进"预装载寄存器"，要等下一次更新事件（下溢）
   * 才搬到真正生效的影子寄存器。
   * 为什么必须开：FOC 是在 ADC 中断里算完立刻写 CCR 的，那时计数器已经
   * 从顶点往下走了一段。若不开预装载，新值会立刻生效——如果新 CCR 比当前
   * CNT 大，本周期会多翻一次电平，PWM 出现不对称的怪脉冲。
   * 开了之后：本周期的波形保持不变，新占空比从下一个周期整周期生效，
   * 代价只是延迟一个 PWM 周期（100us）的相位。 */
  tmr_output_channel_buffer_enable(TMR1, TMR_SELECT_CHANNEL_1, TRUE);
  tmr_output_channel_buffer_enable(TMR1, TMR_SELECT_CHANNEL_2, TRUE);
  tmr_output_channel_buffer_enable(TMR1, TMR_SELECT_CHANNEL_3, TRUE);

  /* ---------- 5. 死区 + 刹车配置 ---------- */
  tmr_brkdt_default_para_init(&tmr_brkdt_struct);
  tmr_brkdt_struct.deadtime = (uint8_t)deadtime_dtc;           /* 死区：DTC * 3.47ns */
  tmr_brkdt_struct.brk_enable = FALSE;                         /* 无外部比较器，暂不使能刹车 */
  tmr_brkdt_struct.fcsodis_state = TRUE;                       /* OEN=0 时输出空闲电平（低），不高阻 */
  tmr_brkdt_struct.fcsoen_state = TRUE;                        /* OEN=1 时冻结状态同低电平 */
  tmr_brkdt_struct.auto_output_enable = FALSE;                 /* 刹车后不自动恢复（同 G4：故障须软件复位 MOE） */
  tmr_brkdt_struct.brk_polarity = TMR_BRK_INPUT_ACTIVE_HIGH;   /* 预留：BKIN 高有效 */
  tmr_brkdt_struct.wp_level = TMR_WP_OFF;                      /* 开发期不锁（同 G4 LockLevel=OFF），量产前再设 LEVEL_3 */
  tmr_brkdt_config(TMR1, &tmr_brkdt_struct);

  /* ---------- 6. GPIO 切 MUX1（此刻 OEN=0，引脚输出 idle=低，仍安全） ---------- */
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins = TMR1_OUT_GPIOA_PINS;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(GPIOA, &gpio_init_struct);
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE8, GPIO_MUX_1);   /* CH1 */
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE9, GPIO_MUX_1);   /* CH2 */
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE10, GPIO_MUX_1);  /* CH3 */

  gpio_init_struct.gpio_pins = TMR1_OUT_GPIOB_PINS;
  gpio_init(GPIOB, &gpio_init_struct);
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE13, GPIO_MUX_1);  /* CH1N */
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE14, GPIO_MUX_1);  /* CH2N */
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE15, GPIO_MUX_1);  /* CH3N */

  /* ---------- 7. 总闸：主输出使能（MOE/OEN）+ 启动计数 ---------- */
  tmr_output_enable(TMR1, TRUE);
  tmr_counter_enable(TMR1, TRUE);

  /* ---------- 8. TMR1 上溢中断（FOC 同步点） ----------
   * 中心对齐模式1 = 下溢更新：每 PWM 周期触发一次（默认 10kHz）。
   * 中断优先级 2（RT-Thread 4 位抢占分组下足够高，快于主循环线程）。
   * 回调由 tmr1_pwm_set_update_cb() 注册，默认空转无开销。
   */
  tmr_interrupt_enable(TMR1, TMR_OVF_INT, TRUE);
  nvic_irq_enable(TMR1_OVF_TMR10_IRQn, 2, 0);

  /* ---------- 9. CH4：中点触发源（硬件直连 ADC 注入组） ----------
   * 相位：CH4 = PWM_MODE_B + CCR4 = ARR-1
   *   PWM_MODE_B 下 OC4REF 在 CNT > CCR4 时有效，于是：
   *     上坡 CNT 从 ARR-1 走到 ARR  → OC4REF 由无效变有效 → 一次上升沿（顶点）
   *     下坡 CNT 从 ARR 走回 ARR-1  → OC4REF 由有效变无效 → 一次下降沿
   *   ADC 只检测上升沿，所以每个 PWM 周期正好触发一次，且正好在零矢量顶点。
   *
   * ⚠ oc_output_state 必须是 TRUE：
   *   它对应寄存器里的 OC4E 位。OC4E=0 时，比较中断（CC4IF）照样能进，
   *   但 OC4REF 内部信号不产生 —— 而 ADC 注入组触发的正是 OC4REF。
   *   之前设成 FALSE，结果就是"中断能进、ADC 不触发、irq 一直是 0"。
   *   CH4 没有做 gpio_pin_mux_config 映射，所以即使 OC4E=1 也不会有引脚输出，
   *   不会干扰三相互补。
   *
   * 去向：OC4REF → ADC 注入组触发输入（ADC_PREEMPT_TRIG_TMR1CH4），全硬件完成。
   */
  tmr_output_struct.oc_mode = TMR_OUTPUT_CONTROL_PWM_MODE_B;
  tmr_output_struct.oc_output_state = TRUE;           /* 必须开：否则 OC4REF 不产生，ADC 收不到触发 */
  tmr_output_struct.occ_output_state = FALSE;         /* 互补通道不用 */
  tmr_output_struct.oc_polarity = TMR_OUTPUT_ACTIVE_HIGH;
  tmr_output_struct.occ_polarity = TMR_OUTPUT_ACTIVE_HIGH;
  tmr_output_channel_config(TMR1, TMR_SELECT_CHANNEL_4, &tmr_output_struct);
  tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_4, (uint16_t)(s_pwm_arr - 1U));  /* 顶点前 1 点 */
}

/**
  * @brief  默认参数初始化（10kHz，死区约 347ns）
  * @retval none
  */
void tmr1_pwm_init(void)
{
  tmr1_pwm_init_ex(TMR1_PWM_FREQ_HZ, TMR1_DEADTIME_DTC);
}

/**
  * @brief  设置通道比较值（SVPWM 输出直接用它）
  * @param  ch  通道号：1=U 相(高边 CH1)，2=V 相(CH2)，3=W 相(CH3)
  * @param  ccr 比较值 0~ARR
  * @note   中心对齐 + PWM_MODE_A（等价 ST PWM1）：有效区间围绕谷底，
  *         duty(高电平) = CCR/ARR，CCR 越大占空比越大
  * @retval none
  */
void tmr1_pwm_set_ccr(uint8_t ch, uint16_t ccr)
{
  if(ccr > s_pwm_arr)
  {
    ccr = s_pwm_arr;
  }

  switch(ch)
  {
    case 1: tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_1, ccr); break;
    case 2: tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_2, ccr); break;
    case 3: tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_3, ccr); break;
    default: break;
  }
}

/**
  * @brief  按占空比设置通道（千分比，0~1000 对应 0%~100%）
  * @param  ch      通道号 1~3
  * @param  permille 占空比 0~1000
  * @retval none
  */
void tmr1_pwm_set_duty(uint8_t ch, uint16_t permille)
{
  if(permille > 1000)
  {
    permille = 1000;
  }
  /* PWM_A 模式：CNT<CCR 输出高 → duty = CCR/ARR → CCR = ARR*duty/1000（duty=100% -> CCR=ARR） */
  tmr1_pwm_set_ccr(ch, (uint16_t)(((uint32_t)s_pwm_arr * permille) / 1000UL));
}

/**
  * @brief  主输出使能（OEN 总开关）
  * @param  en TRUE=输出 PWM；FALSE=输出全部落到空闲电平（低，上下管关断）
  * @retval none
  */
void tmr1_pwm_output_enable(confirm_state en)
{
  tmr_output_enable(TMR1, en);
}

/**
  * @brief  注册 TMR1 上溢中断回调（每 PWM 周期调用一次）
  * @param  cb 回调函数指针（中断上下文，禁止阻塞/打印；传 NULL 取消）
  * @retval none
  */
void tmr1_pwm_set_update_cb(void (*cb)(void))
{
  s_update_cb = cb;
}

/**
  * @brief  TMR1 上溢中断服务函数
  * @note   TMR1 上溢与 TMR10 共用向量（TMR10 未启用，无冲突）。
  *         中心对齐模式1下每 PWM 周期下溢触发一次 = FOC 步进同步点。
  *         先清标志再执行回调，防止回调期间重复进入。
  */
void TMR1_OVF_TMR10_IRQHandler(void)
{
  tmr_flag_clear(TMR1, TMR_OVF_FLAG);
  if(s_update_cb != (void *)0)
  {
    s_update_cb();
  }
}

/**
  * @brief  注册 CH4 中点触发回调（每 PWM 周期一次，G4 式同步点）
  * @param  cb 回调函数指针（中断上下文，禁止阻塞/打印；传 NULL 取消）
  * @note   触发时刻 = 周期中点（顶点 CNT=ARR）前 1 个计数点（CNT=ARR-1），
  *         与 G4 工程 CH4(PWM2, CCR=ARR-1) 的 ADC 触发相位一致；
  *         本实现直接走比较中断，未接 ADC。
  * @retval none
  */
void tmr1_pwm_set_midpoint_cb(void (*cb)(void))
{
  s_midpoint_cb = cb;
}

/**
  * @brief  TMR1 通道中断服务函数（CH4 比较 → 周期中点前 1 点触发）
  * @note   TMR1 CH1~4 比较中断共用此向量（CH1/2/3 未使能中断，无冲突）。
  *         中心对齐下上坡匹配(CNT=ARR-1)与下坡匹配仅隔 2 个计数（约 7ns），
  *         NVIC 响应远慢于该间隔 → 两次匹配合并为一次标志置位，
  *         先清标志再执行回调，自然每 PWM 周期只回调一次（相位=上坡匹配点）。
  */
void TMR1_CH_IRQHandler(void)
{
  tmr_flag_clear(TMR1, TMR_C4_FLAG);
  if(s_midpoint_cb != (void *)0)
  {
    s_midpoint_cb();
  }
}
