#include <rtthread.h>
#include "SEGGER_RTT.h"
#include "tmr1_pwm.h"
#include "app_key_rtos.h"   /* 按键：app_key_begin / watcher_init / on / pump */
#include "curr_adc.h"       /* 三相电流中点采样：PC0/1/2 → ADC1_IN10/11/12 */
#include "usart_log.h"      /* 调试串口：UART4（PH2=RX / PH3=TX） */
#include "foc_scope.h"      /* 电流示波：中断直接写 RTT 通道 1（J-Scope） */
#include "currloop0.h"      /* Simulink 生成：电流闭环（Clark/Park/双PI/SVPWM/状态机） */
#include "at32f435_437.h"
#include <string.h>         /* memset：停机时清零模型状态 */


/* 50% 占空比对应的 CCR（零矢量：三相等电位，电机无电流） */
#define CCR_50          ((uint16_t)((TMR1_PWM_ARR + 1U) / 2U))   /* 7200 */


static volatile uint8_t s_motor_run = 0;   /* 0=停止  1=运行（按键线程写、中点中断读） */

/* ---------------------------------------------------------------------------
 * 母线电压（V）：喂给模型的 v_bus 输入
 * SVPWM 内部要做 CCR = (-Vphase / v_bus + 0.5) * 14400，
 * 这个值必须等于【实际母线电压】，否则占空比换算出来的相电压会整体偏大/偏小。
 * 如果你以后加了母线电压采样，把这里换成实测值即可。
 * ------------------------------------------------------------------------- */
#define V_BUS_VOLT          12.0f

/* 电流换算：ADC 驱动返回 mA，模型输入 rtU.ia/ib/ic 要【安培】 */
#define MA_TO_A             0.001f

/* ===== 启动诊断打点（排查卡死用，定位后可以整段删掉） =====
 * 走 RTT 通道 0 而不是串口 —— 万一卡在 USART_LOG 上，打点照样能出来。
 * 上电后连 J-Link，用 RTT Viewer 看 [BOOT]n 停在几号：
 *   [BOOT]n 打出来了、[BOOT]n+1 没出来 → 卡在第 n 号之后那一段里。
 * 只支持 %d/%u/%s/%x，不支持 %f。 */
#define BOOT_MARK(n)   SEGGER_RTT_printf(0, "[BOOT]%d\n", (int)(n))

/* 中点中断进入次数：主循环打印它，用来判断"中断到底有没有在跑"
 *   这个数一直涨 → 中断活着，卡的是主线程
 *   这个数不动   → 中断没跑（或系统已挂） */
static volatile uint32_t s_foc_irq_cnt = 0U;

/* 停机输出：三相 CCR 回 50% 零矢量（实现在下方，这里先声明供 foc_step 调用） */
static void motor_stop_output(void);

/* CCR 安全钳位：把模型输出的 float 占空比限制在 [0, ARR]，防止越界/溢出。
 * 模型输出的满量程是 0~14400（PWM_HalfPeriod_Gain=14400），
 * 但 TMR1_PWM_ARR = 14399，所以 14400 会被钳到 14399，差 1 个计数可忽略。 */
static uint16_t ccr_clamp(float ccr_f)
{
    if (ccr_f < 0.0f)
    {
        return 0U;
    }
    if (ccr_f > (float)TMR1_PWM_ARR)
    {
        return (uint16_t)TMR1_PWM_ARR;
    }
    return (uint16_t)ccr_f;
}

/* FOC 高频步进：挂在 TMR1 周期中点（顶点前 1 点），10kHz 执行。
 *
 * 执行顺序（顺序不能乱）：
 *   1) 中点采样三相电流（此刻零矢量中点，纹波最小）
 *   2) 把 mA 换成 A，填进模型的 5 个输入
 *   3) 跑 currloop0_step()：Clark → 状态机 → Park → 双PI → 反Park → SVPWM
 *   4) 把 rtY.tABC[3]（0~14400）写进三相 CCR
 *
 * 停机时不做 3)/4)，而是把模型状态整体清零 + 三相强制 50% 零矢量，
 * 好处是下次启动时状态机从 IDLE 干净地重新走 预定位→开环→闭环。
 */
static void foc_step(void)
{
    int16_t current_u;   /* U 相电流 mA（正=流出桥臂） */
    int16_t current_v;   /* V 相电流 mA */
    int16_t current_w;   /* W 相电流 mA */

    s_foc_irq_cnt++;     /* 心跳：主循环读它判断中断是否在跑 */

    /* ---- 0. 停机分支：清状态 + 零矢量，直接返回 ----
     * 注意：这里是【每个 100us 都清一次】，而不是只在下降沿清。
     * 因为 memset 只有 56 字节，代价可忽略，但换来"永远不会带着脏状态启动"。
     * 它同时把 is_active_c3_currloop0 清 0，下次 step 时状态机会自动回到 IDLE。 */
    if (s_motor_run == 0U)
    {
        memset(&rtDW, 0, sizeof(rtDW));   /* 清状态机 + 所有积分器 */
        motor_stop_output();              /* 三相 CCR = 50% 零矢量 */
        return;
    }

    /* ---- 1. 中点采样三相电流（约 5us 忙等） ---- */
    curr_adc_sample();

    current_u = curr_adc_get_amp(CURR_PHASE_U);
    current_v = curr_adc_get_amp(CURR_PHASE_V);
    current_w = curr_adc_get_amp(CURR_PHASE_W);

    /* RTT 示波：只存数不打印（printf 会把 100us 的控制周期打崩） */
    foc_scope_push(current_u, current_v, current_w);

    /* ---- 2. 填模型输入（接口就是 rtU 这一组结构体） ---- */
    rtU.ia = (float)current_u * MA_TO_A;   /* 安培 */
    rtU.ib = (float)current_v * MA_TO_A;   /* 安培 */
    rtU.ic = (float)current_w * MA_TO_A;   /* 安培 */
    rtU.v_bus = V_BUS_VOLT;                /* 母线电压 V */
    /* ⚠ 生成代码里判断的是 rtU.Motor_OnOff == 1.0F（浮点严格相等），
     *   必须给 1.0f，给 0.999f 或 2 都会让状态机永远进不了启动分支。 */
    rtU.Motor_OnOff = 1.0f;

    /* ---- 3. 跑电流环 ---- */
    currloop0_step();

    /* ---- 4. 输出三相 CCR ---- */
    tmr1_pwm_set_ccr(TMR1_CH_U, ccr_clamp(rtY.tABC[0]));
    tmr1_pwm_set_ccr(TMR1_CH_V, ccr_clamp(rtY.tABC[1]));
    tmr1_pwm_set_ccr(TMR1_CH_W, ccr_clamp(rtY.tABC[2]));
}

/* 停止输出：三相 CCR 回到 50% 零矢量。
 *
 * ⚠ 这不是"断开输出"，也不是"自由滑行"：
 *   三相同为 50% 时，U/V/W 三端在任何时刻都处在同一电位（同为 Vbus 或同为 GND），
 *   线电压恒为 0 → 静止时无电流、无转矩，是标准的待机零矢量；
 *   但等效于【三相绕组被短接】，转子若仍在惯性旋转，反电动势会驱动制动电流。
 *   若要真正自由滑行（高阻滑行），必须关主输出：tmr_output_enable(TMR1, FALSE)。
 *
 * 停机时桥臂仍在 10kHz 开关，因此会有开关损耗与 10kHz 可听噪声，属正常现象。 */
static void motor_stop_output(void)
{
    tmr1_pwm_set_ccr(TMR1_CH_U, CCR_50);
    tmr1_pwm_set_ccr(TMR1_CH_V, CCR_50);
    tmr1_pwm_set_ccr(TMR1_CH_W, CCR_50);
}




/* 两个监听器（分别给线程 A、B；静态分配，生命周期跟随整个程序） */
static app_key_watcher_t s_watcher_a;
static app_key_watcher_t s_watcher_b;


/* ---------------- 线程 B 的回调 ---------------- */

/* 线程 B 收到 KEY1 单击：启动电机 */
static void b_on_key1_click(app_key_id_t key, flex_button_event_t event)
{
    (void)key;
    (void)event;
    s_motor_run = 1U;
    SEGGER_RTT_printf(0, "s_motor_run\n");
}

/* 线程 B 收到 KEY2 单击：暂停电机（三相回零矢量，θ 保留） */
static void b_on_key2_click(app_key_id_t key, flex_button_event_t event)
{
    (void)key;
    (void)event;
    s_motor_run = 0U;
    motor_stop_output();
    SEGGER_RTT_printf(0, "motor_stop_output\n");
}

/* 线程 B：注册"KEY1 启动、KEY2 暂停"，然后非阻塞泵（直接用全局监听器 s_watcher_b） */
static void thread_b_entry(void *parameter)
{
    app_key_watcher_init(&s_watcher_b);
    app_key_on(&s_watcher_b, APP_KEY_1, FLEX_BTN_PRESS_CLICK, b_on_key1_click);
    app_key_on(&s_watcher_b, APP_KEY_2, FLEX_BTN_PRESS_CLICK, b_on_key2_click);

    while (1)
    {
        app_key_pump(&s_watcher_b, RT_WAITING_NO);
        rt_thread_mdelay(10);
    }
}









/* ############################################################################
 * #  三、main：初始化 + 创建线程 A/B + 主循环打印                             #
 * ##########################################################################*/

int main(void)
{
    /* ---- 0. 调试串口：UART4（PH2=RX / PH3=TX），115200-8-N-1 ----
     * ⚠ 顺序不能动：必须"先初始化，后打印"。
     *   若注释掉 usart_log_init() 而保留下面的 USART_LOG，UART4 时钟未开，
     *   发送函数永远等不到 TDBE，main 线程会永久卡死在第一个字节上。 */
    usart_log_init(USART_LOG_DEFAULT_BAUD);
    BOOT_MARK(1);   /* 卡在这里之后 → usart_log_init 本身有问题 */
    USART_LOG("\r\n\r\n=== MIMI-FOC UART4 OK, 115200-8-N-1 ===\r\n");
    USART_LOG("sclk=%u Hz\r\n", (unsigned)system_core_clock);
    BOOT_MARK(2);   /* 到这里 USART_LOG 已经通了；没到这里 → 串口发送死等 TDBE */

    /* ---- 1. PWM 初始化（10kHz；三相 CCR 初始 50%，零矢量，安全） ---- */
    tmr1_pwm_init();
    BOOT_MARK(3);   /* 到这里说明 PWM 起来了，10kHz 中断已在跑 */



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

    /* ---- 3. 中点 FOC 回调（10kHz 中断里跑电流闭环） ---- */
    tmr1_pwm_set_midpoint_cb(foc_step);
    BOOT_MARK(5);   /* 到这里中断回调已挂上，之后每次中断都会累加 s_foc_irq_cnt */

    /* ---- 3.5 三相电流采样初始化：PC0/1/2 → ADC1_IN10/11/12 ---- */
    curr_adc_init();
    BOOT_MARK(6);

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
    BOOT_MARK(7);   /* 到这里说明 ADC 标定的 256 次采样全部完成，ADC 没死等 */

    /* ---- 3.55 电流示波通道：注册 RTT 通道 1（J-Scope 用，10kHz 全采样） ----
     *  RTT -> VOFA */
    foc_scope_init();
    BOOT_MARK(8);

    /* ---- 4. 按键：全局初始化（GPIO + 扫描线程），只调一次 ---- */
    app_key_begin();
    BOOT_MARK(9);


    rt_thread_t tid;

    
    tid = rt_thread_create("B", thread_b_entry, RT_NULL,
                           1024, 10, 10);
    if (tid != RT_NULL)
    {
        rt_thread_startup(tid);
    }

    /* ---- 5. 串口只做单向输出（电流遥测），不再建回显线程 ----
     * 若以后需要"从上位机发指令给 MCU"，再恢复一个读线程：
     *     uint8_t ch;
     *     if (usart_log_getc(&ch) != 0) { ... }
     * 接收中断一直在跑，环形缓冲会自己覆盖旧数据，不读也不占资源。 */



    BOOT_MARK(10);

    /* ---- 6. main 主循环 ----
     * ⚠ SEGGER_RTT_printf 不支持 %f：float 必须转成 int 再打，
     *   用 %.1f 传进去只会打出乱码或空，白白以为"卡死了"。
     *   irq 每 500ms 应该涨 5000 左右（10kHz×0.5s），不涨就说明中断没在跑。 */
    while (1)
    {
        SEGGER_RTT_printf(0, "irq=%u tABC=%d,%d,%d run=%d\n",
                          (unsigned)s_foc_irq_cnt,
                          (int)rtY.tABC[0], (int)rtY.tABC[1], (int)rtY.tABC[2],
                          (int)s_motor_run);
        rt_thread_mdelay(500);
    }
}
