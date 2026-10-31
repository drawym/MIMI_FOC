#include <rtthread.h>
#include "SEGGER_RTT.h"
#include "tmr1_pwm.h"
#include "app_key_rtos.h"   /* 按键：app_key_begin / watcher_init / on / pump */
#include "curr_adc.h"       /* 三相电流中点采样：PC0/1/2 → ADC1_IN10/11/12 */
#include "usart_log.h"      /* 调试串口：UART4（PH2=RX / PH3=TX） */
#include "VF.h"
#include "at32f435_437.h"


/* 50% 占空比对应的 CCR（零矢量：三相等电位，电机无电流） */
#define CCR_50          ((uint16_t)((TMR1_PWM_ARR + 1U) / 2U))   /* 7200 */


static volatile uint8_t s_motor_run = 0;   /* 0=停止  1=运行（按键线程写、中点中断读） */

/* CCR 安全钳位：把 VF 输出的 float 占空比限制在 [0, ARR]，防止越界/溢出 */
static uint16_t vf_ccr_safe(float ccr_f)
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

/* FOC 高频步进：挂在 TMR1 周期中点（顶点前 1 点），10kHz 执行
 * 先中点采样三相电流（此刻是零矢量中点、纹波最小），再跑 VF */
static void foc_step(void)
{


    if (s_motor_run == 0U)
    {
        return;
    }

    /* 10kHz 中断里 RTT 打印的节流计数器：每 100 次打一次 → 100Hz 刷新，
     * 避免每个周期都格式化打印拖垮 PWM 时序 */
    static uint16_t s_dbg_cnt = 0U;

    curr_adc_sample();   /* 中点采样三相电流（约 5us 忙等） */

    if ((++s_dbg_cnt % 100U) == 0U)
    {
        /* 电机停止时三相应≈0mA；运行时呈 120° 正弦 */
        SEGGER_RTT_printf(0, "ISR iu,iv,iw=%d,%d,%d mA\n",
                          curr_adc_get_amp(CURR_PHASE_U),
                          curr_adc_get_amp(CURR_PHASE_V),
                          curr_adc_get_amp(CURR_PHASE_W));
    }


    VF_step();   /* θ积分→反Park→SVPWM，结果在 rtY.tABC[3]（0~14400） */

    tmr1_pwm_set_ccr(TMR1_CH_U, vf_ccr_safe(rtY.tABC[0]));
    tmr1_pwm_set_ccr(TMR1_CH_V, vf_ccr_safe(rtY.tABC[1]));
    tmr1_pwm_set_ccr(TMR1_CH_W, vf_ccr_safe(rtY.tABC[2]));
}

/* 停止输出：三相 CCR 回到 50% 零矢量，电机无电流自由停转 */
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
    // /* ---- 0. 调试串口：UART4（PH2=RX / PH3=TX），115200-8-N-1 ---- */
    // usart_log_init(USART_LOG_DEFAULT_BAUD);
    // USART_LOG("boot ok, sclk=%u Hz\r\n", system_core_clock);

    /* ---- 1. PWM 初始化（10kHz；三相 CCR 初始 50%，零矢量，安全） ---- */
    tmr1_pwm_init();



    /* ---- 2. VF 模型初始化 + 输入给定 ---- */
    VF_initialize();
    rtU.ud    = 0.0f;    /* d 轴电压：开环 V/F 恒为 0 */
    rtU.uq    = 1.0f;    /* q 轴电压 */
    rtU.Freq  = 8.0f;    /* 电频率 Hz */
    rtU.v_bus = 12.0f;   /* 母线电压 12V */

    /* ---- 3. 中点 FOC 回调（10kHz 中断里跑 VF） ---- */
    tmr1_pwm_set_midpoint_cb(foc_step);

    /* ---- 3.5 三相电流采样初始化：PC0/1/2 → ADC1_IN10/11/12 ---- */
    curr_adc_init();

    /* ---- 4. 按键：全局初始化（GPIO + 扫描线程），只调一次 ---- */
    app_key_begin();


    rt_thread_t tid;

    
    tid = rt_thread_create("B", thread_b_entry, RT_NULL,
                           1024, 10, 10);
    if (tid != RT_NULL)
    {
        rt_thread_startup(tid);
    }



    /* ---- 6. main 主循环：周期打印三相 CCR（电流已在中点中断里 100Hz 打印） ---- */
    while (1)
    {
        // if (s_motor_run != 0U)
        {
            SEGGER_RTT_printf(0, "tABC=%.1f,%.1f,%.1f\n",
                              rtY.tABC[0], rtY.tABC[1], rtY.tABC[2]);
        }
        rt_thread_mdelay(500);

        USART_LOG("boot ok, sclk=%u Hz\r\n", system_core_clock);
    }
}
