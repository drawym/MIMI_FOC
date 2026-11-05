#include <rtthread.h>
#include "SEGGER_RTT.h"
#include "tmr1_pwm.h"
#include "app_key_rtos.h"   /* 按键：app_key_begin / watcher_init / on / pump */
#include "curr_adc.h"       /* 三相电流中点采样：PC0/1/2 → ADC1_IN10/11/12 */
#include "usart_log.h"      /* 调试串口：UART4（PH2=RX / PH3=TX） */
#include "foc_scope.h"      /* 电流示波：中断直接写 RTT 通道 1（J-Scope） */
#include "currloop0.h"      /* Simulink 生成：电流闭环（Clark/Park/双PI/SVPWM/状态机） */
#include "vf_start.h"       /* V/F 开环启动：预定位 + 10s 升频，跑完再切闭环 */
#include "at32f435_437.h"
#include <string.h>         /* memset：停机时清零模型状态 */


/* 50% 占空比对应的 CCR（零矢量：三相等电位，电机无电流） */
#define CCR_50          ((uint16_t)((TMR1_PWM_ARR + 1U) / 2U))   /* 7200 */
/* ---------------------------------------------------------------------------
 * 母线电压（V）：喂给模型的 v_bus 输入
 * ------------------------------------------------------------------------- */
#define V_BUS_VOLT          12.0f

/* 电流换算：curr_adc_get_amp() 返回【毫安】，Simulink 模型的 rtU.ia/ib/ic 要【安培】。
 * ⚠ 这个 ×0.001 绝对不能省：
 *   漏掉它，模型会把 500mA 当成 500A，反馈比给定大 1000 倍，
 *   电流环一进闭环就把输出打到反向饱和 —— 表现是电机没力、抽搐、发烫。 */
#define MA_TO_A             0.001f

static volatile uint8_t s_motor_run = 0;   /* 0=停止  1=运行（按键线程写、中点中断读） */

/* ===== 运行阶段 =====
 * 启动流程刻意分成两段，而不是让 currloop0 自己从静止拖：
 *   VF：电压开环，鲁棒性高，负责把转子从静止拖到目标转速；
 *   闭环：电流环接手，此时反电动势已经建立，电流环容易稳住。
 * 这样避开了 I/F 从静止启动时"电流环带宽不够、磁场畸变、转子乱晃"的问题。 */
#define RUN_PHASE_STOP      0U   /* 停止 */
#define RUN_PHASE_VF        1U   /* V/F 开环强拖（10.5 秒） */
#define RUN_PHASE_CLOSED    2U   /* 电流闭环 */

/* ===== 启动方式选择 =====
 * 0 = I/F 强拖（currloop0 自带的 预定位→斜坡升速→切闭环）
 *     电流全程闭环，限流、转矩可控、切换平滑，工程上更常用。
 *     ⚠ 前提：电流环带宽 ≥ 10 倍启动基波频率。
 *       当前 122Hz vs 7Hz（60rpm），比值 17 倍，满足。
 * 1 = V/F 电压开环（vf_start.c）
 *     不依赖电流环，最鲁棒，但电流随负载漂移、带载易失步。
 *     留作"电流环还没调好"时的保底手段。 */
#define START_MODE_USE_VF    0

static volatile uint8_t s_run_phase = RUN_PHASE_STOP;


/* ===== 启动诊断打点（排查卡死用，定位后可以整段删掉） =====
 * 走 RTT 通道 0 而不是串口 —— 万一卡在 USART_LOG 上，打点照样能出来。
 * 上电后连 J-Link，用 RTT Viewer 看 [BOOT]n 停在几号：
 *   [BOOT]n 打出来了、[BOOT]n+1 没出来 → 卡在第 n 号之后那一段里。
 * 只支持 %d/%u/%s/%x，不支持 %f。 */
#define BOOT_MARK(n)   SEGGER_RTT_printf(0, "[BOOT]%d\n", (int)(n))

/* 中点中断进入次数：主循环打印它，用来判断"中断到底有没有在跑"*/
static volatile uint32_t s_foc_irq_cnt = 0U;

/* 停机输出：三相 CCR 回 50% 零矢量（实现在下方，这里先声明供 foc_step 调用） */
static void motor_stop_output(void);


/* FOC 高频步进：挂在 TMR1 周期中点（顶点前 1 点），10kHz 执行。
 */
static void foc_step(void)
{
    int16_t current_u;   /* U 相电流 mA（正=流出桥臂） */
    int16_t current_v;   /* V 相电流 mA */
    int16_t current_w;   /* W 相电流 mA */

    s_foc_irq_cnt++;     /* 心跳：主循环读它判断中断是否在跑 */

    /* ---- 0. 停机分支 ----
     * 只在"从运行态切到停止"的这一拍做一次清理，不是每拍都清：
     *   - 清 rtDW：把状态机、PI 积分器、角度积分器全部归零
     *   - 复位 VF 阶段，下次启动重新走预定位
     * 不做的话，下次启动会带着上次的状态直接从 RunStage 开始。 */
    if (s_motor_run == 0U)
    {
        if (s_run_phase != RUN_PHASE_STOP)
        {
            memset(&rtDW, 0, sizeof(rtDW));
            vf_start_reset();
            s_run_phase = RUN_PHASE_STOP;
        }
        return;
    }

    /* ---- 1. 取三相电流 ----
     * 数据已经在 ADC1_2_3 中断的最开头读进 s_curr_raw 了（硬件触发那次转换），
     * 这里只做码值→mA 的换算，既不触发采样、也不忙等。
     * 采样动作本身发生在进入本函数之前，由 TMR1_CH4 硬件完成。 */
    current_u = curr_adc_get_amp(CURR_PHASE_U);
    current_v = curr_adc_get_amp(CURR_PHASE_V);
    current_w = curr_adc_get_amp(CURR_PHASE_W);

    /* RTT 示波：只存数不打印（printf 会把 100us 的控制周期打崩） */
    foc_scope_push(current_u, current_v, current_w);

    /* ---- 2. V/F 开环阶段：电压直接驱动，不经过电流环 ----
     * 这一段是"把转子从静止拖到目标转速"，用电压开环而不是电流闭环，
     * 因为开环不依赖反馈，转子跟得上就转，跟不上的时也不会被错误反馈带偏。 */
    if (s_run_phase == RUN_PHASE_VF)
    {
        
        vf_start_step();   /* 内部算角度、电压、SVPWM，直接写三相 CCR */

        if (vf_start_finished() != 0U)
        {
            /* ---- 切闭环：手动把状态机置到 RunStage，跳过模型自带的
             *      预定位和强拖（那两段是给冷启动用的，这里已经转起来了）---- */
            rtDW.is_active_c3_currloop0 = 1U;   /* 标记 Chart 已激活 */
            rtDW.is_c3_currloop0 = 4U;          /* IN_RunStage = 4 */

            /* 相位交接：VF 的电角度直接交给闭环积分器。
             * 两边单位都是"电角度 rad"，不需要再乘极对数，直接赋值即可。
             * 不做这一步，闭环会从 0 重新起算，切换瞬间磁场突变、转子被猛拽。 */
            rtDW.DiscreteTimeIntegrator1_DSTATE = vf_start_get_theta();

            /* PI 积分器清零：从 0 开始建立，避免带着上一段的残留冲击 */
            rtDW.Integrator_DSTATE   = 0.0f;
            rtDW.Integrator_DSTATE_b = 0.0f;
            rtDW.temporalCounter_i1  = 0U;

            s_run_phase = RUN_PHASE_CLOSED;
        }
        return;
    }

    /* ---- 3. 电流闭环阶段 ----
     * 填模型输入：curr_adc_get_amp() 返回 mA，模型要 A，必须乘 MA_TO_A。 */
    rtU.ia = (float)current_u * MA_TO_A;
    rtU.ib = (float)current_v * MA_TO_A;
    rtU.ic = (float)current_w * MA_TO_A;
    rtU.v_bus       = V_BUS_VOLT;
    rtU.Motor_OnOff = 1.0f;

    currloop0_step();

    /* ---- 4. 输出三相 CCR ---- */
    tmr1_pwm_set_ccr(TMR1_CH_U, rtY.tABC[0]);
    tmr1_pwm_set_ccr(TMR1_CH_V, rtY.tABC[1]);
    tmr1_pwm_set_ccr(TMR1_CH_W, rtY.tABC[2]);
}

/* 停止输出：三相 CCR 回到 50% 零矢量。
 * ⚠ 这不是"断开输出"，也不是"自由滑行"：
 *   三相同为 50% 时，U/V/W 三端在任何时刻都处在同一电位（同为 Vbus 或同为 GND），
 *   线电压恒为 0 → 静止时无电流、无转矩，是标准的待机零矢量；
 *   但等效于【三相绕组被短接】，转子若仍在惯性旋转，反电动势会驱动制动电流。
 *   若要真正自由滑行（高阻滑行），必须关主输出：tmr_output_enable(TMR1, FALSE)。
 * 停机时桥臂仍在 10kHz 开关，因此会有开关损耗与 10kHz 可听噪声，属正常现象。 */
static void motor_stop_output(void)
{
    tmr1_pwm_set_ccr(TMR1_CH_U, CCR_50);
    tmr1_pwm_set_ccr(TMR1_CH_V, CCR_50);
    tmr1_pwm_set_ccr(TMR1_CH_W, CCR_50);
}







/* ---------------- 线程 B 的回调 ---------------- */
static app_key_watcher_t event_a;
/* 线程 B 收到 KEY1 单击：启动电机 */
static void b_on_key1_click(app_key_id_t key, flex_button_event_t event)
{
    (void)key;
    (void)event;

#if (START_MODE_USE_VF != 0)
    vf_start_begin();            /* 复位 VF 角度和计时，从预定位开始 */
    s_run_phase = RUN_PHASE_VF;  /* 先进 VF 开环，跑完自动切闭环 */
#else
    /* I/F 模式：什么都不用做，交给 currloop0 的状态机
     * 它自己会走 IDLE → 预定位(0.3s) → 斜坡升速(3s) → 闭环 */
    s_run_phase = RUN_PHASE_CLOSED;
#endif

    rtU.v_bus = V_BUS_VOLT;      /* 母线电压 V */
    rtU.Motor_OnOff = 1.0f;      /* 模型判 Motor_OnOff == 1.0F，必须给精确值 */

    s_motor_run = 1U;            /* 最后才放开运行标志，避免中断先跑一步 */

    SEGGER_RTT_printf(0, "start: VF phase\n");
}

/* 线程 B 收到 KEY2 单击：暂停电机（三相回零矢量） */
static void b_on_key2_click(app_key_id_t key, flex_button_event_t event)
{
    (void)key;
    (void)event;

    s_motor_run = 0U;            /* 先停运行标志，中断里就不会再写 CCR 了 */
    s_run_phase = RUN_PHASE_STOP;

    memset(&rtDW, 0, sizeof(rtDW));   /* 清状态机 + 所有积分器 */
    vf_start_reset();                 /* 复位 VF 阶段 */

    motor_stop_output();
    SEGGER_RTT_printf(0, "motor_stop_output\n");
}

/* 线程 B：注册"KEY1 启动、KEY2 暂停"，然后非阻塞泵（直接用全局监听器 event_a） */
static void thread_key_entry(void *parameter)
{
    app_key_watcher_init(&event_a);
    app_key_on(&event_a, APP_KEY_1, FLEX_BTN_PRESS_CLICK, b_on_key1_click);
    app_key_on(&event_a, APP_KEY_2, FLEX_BTN_PRESS_CLICK, b_on_key2_click);

    while (1)
    {
        app_key_pump(&event_a, RT_WAITING_NO);
        rt_thread_mdelay(10);
    }
}









/* ############################################################################
 * #  三、main：初始化 + 创建线程 A/B + 主循环打印                             #
 * ##########################################################################*/

int main(void)
{
    /* ---- 0. 调试串口：UART4（PH2=RX / PH3=TX），115200-8-N-1 ---- */
    usart_log_init(USART_LOG_DEFAULT_BAUD);
    BOOT_MARK(1);   /* 卡在这里之后 → usart_log_init 本身有问题 */

    /* ---- 1. PWM 初始化（10kHz；三相 CCR 初始 50%，零矢量，安全） ---- */
    tmr1_pwm_init();
    BOOT_MARK(2);   /* 到这里说明 PWM 起来了，10kHz 中断已在跑 */


    /* ----  三相电流采样初始化：PC0/1/2 → ADC1_IN10/11/12 ---- */
    curr_adc_init();
    /* 自检结果直接打出来：0=注入组本身没转起来，1=ADC 正常（问题在 CH4 触发） */
    SEGGER_RTT_printf(0, "[ADC] selftest=%d\n", (int)curr_adc_get_self_test());
    BOOT_MARK(3);



    /* ---- 2. 电流环模型初始化 + 输入默认值 ----
     * currloop0_initialize() 只做一件事：把开环角度积分器的 PrevReset 置 2
     * （表示"本拍不处于复位状态"）。真正的参数在 currloop0.c 里的
     * CurrKPKI / motor 两个结构体，改 Kp/Ki/极对数要去那里改。
     * 这里的 rtU.* 只是给个初值，实际运行时 foc_step() 每拍都会重新写。 */
    currloop0_initialize();
    rtU.ia          = 0.0f;         /* A 相电流 A */
    rtU.ib          = 0.0f;         /* B 相电流 A */
    rtU.ic          = 0.0f;         /* C 相电流 A */
    rtU.v_bus       = V_BUS_VOLT;   /* 母线电压 12V */
    rtU.Motor_OnOff = 0.0f;         /* 0=停机 1=运行（由 foc_step 按 s_motor_run 写） */
    BOOT_MARK(4);

    /* ---- 3. 注册 FOC 步进（由 ADC 转换完成中断驱动） ----
     * 整条链路全靠硬件串起来：
     *   TMR1 计数到 ARR-1 → CH4 比较事件 → ADC 注入组自动转换 3 通道
     *   → 转换完成置 PCCE → 进 ADC1_2_3_IRQHandler（读数据）
     *   → 调用本回调 foc_step（算 Clark/Park/PI/SVPWM→写 CCR）
     * CPU 既不触发、也不等待，ADC 不工作时中断根本不来，不会死锁。 */
    curr_adc_set_done_callback(foc_step);
    BOOT_MARK(5);   /* 到这里 FOC 回调已挂上，之后每次 ADC 转换完成都会进来 */


    /* ---- 3.51 零点标定（必须在电机静止时做） ----
     * 此刻 PWM 已是三相 50% 零矢量、电机未启动，采样电阻上无相电流，
     * 读到的码值就是"零电流"的真实零点（含 INA181 失调 + 分压电阻误差）。
     * 标定后 curr_adc_get_amp() 直接减这个实测值，替代标称的 1.65V(2048)，
     * 把 ±20~50mA 的直流偏差压到 ±1 码（约 8mA），三相和才真正收敛到 0。
     * ⚠ 标定期间电机不能转，否则会把"正在转时的读数"当成零点写进去。 */
    if (curr_adc_calibrate() != 0U)
    {
        USART_LOG("I calib OK: U=%u V=%u W=%u code (nominal 2048)\r\n",
                  (unsigned)curr_adc_get_offset_code(CURR_PHASE_U),
                  (unsigned)curr_adc_get_offset_code(CURR_PHASE_V),
                  (unsigned)curr_adc_get_offset_code(CURR_PHASE_W));
    }
    else
    {
        USART_LOG("I0 calib FAILED: use nominal 2048, check motor is stopped\r\n");
    }
    BOOT_MARK(6);   /* 到这里说明 ADC 标定的 256 次采样全部完成，ADC 没死等 */

    /* ---- 3.55 电流示波通道：注册 RTT 通道 1（J-Scope 用，10kHz 全采样） ----
     *  RTT -> VOFA */
    foc_scope_init();
    BOOT_MARK(8);

    /* ---- 4. 按键：全局初始化（GPIO + 扫描线程），只调一次 ---- */
    app_key_begin();
    BOOT_MARK(7);


    rt_thread_t tid;
    tid = rt_thread_create("Key", thread_key_entry, RT_NULL,
                           1024, 10, 10);
    if (tid != RT_NULL)
    {
        rt_thread_startup(tid);
    }


    BOOT_MARK(10);


    while (1)
    {
        SEGGER_RTT_printf(0, "irq=%u tABC=%d,%d,%d run=%d I=%d,%d,%d mA\n",
                          (unsigned)s_foc_irq_cnt,
                          (int)rtY.tABC[0], (int)rtY.tABC[1], (int)rtY.tABC[2],
                          (int)s_motor_run,
                          (int)curr_adc_get_amp(CURR_PHASE_U),
                          (int)curr_adc_get_amp(CURR_PHASE_V),
                          (int)curr_adc_get_amp(CURR_PHASE_W));
        rt_thread_mdelay(500);
    }
}
