#include <rtthread.h>
#include "SEGGER_RTT.h"
#include "tmr1_pwm.h"

int main(void)
{
    /* ===== TMR1 三相互补 PWM 波形测试（逻辑分析仪验证用） =====
     * 预期输出（10kHz，死区约 347ns，PWM_MODE_A=ST PWM1 波形）：
     *   PA8  CH1  (U高边)  25%  |  PB13 CH1N (U低边)  75% + 死区
     *   PA9  CH2  (V高边)  50%  |  PB14 CH2N (V低边)  50% + 死区
     *   PA10 CH3  (W高边)  90%  |  PB15 CH3N (W低边)  10% + 死区
     * 互补对为反相+死区（切换沿两路都低约 347ns），可验证防直通
     */
    tmr1_pwm_init();                       /* 10kHz + DTC=100(≈347ns)，init 内部已开输出 */

    tmr1_pwm_set_duty(TMR1_CH_U, 250);     /* U 相 25% */
    tmr1_pwm_set_duty(TMR1_CH_V, 500);     /* V 相 50% */
    tmr1_pwm_set_duty(TMR1_CH_W, 900);     /* W 相 90% */

    SEGGER_RTT_printf(0, "PWM test: 10kHz, deadtime~347ns, U=25%% V=50%% W=75%%\n");

    while (1)
    {
        SEGGER_RTT_printf(0, "hello3\n");
        rt_thread_mdelay(1000);  /* 睡眠 1s，让出 CPU */
    }
}
