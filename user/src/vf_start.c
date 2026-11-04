/**
  ******************************************************************************
  * @file    vf_start.c
  * @brief   V/F 开环启动阶段实现（预定位 → 线性升频 → 交给电流闭环）
  *
  *  输出链路（与 currloop0 的 SVPWM 保持完全一致，避免转向/相位对不上）：
  *     电压矢量 (Valpha, Vbeta) → Anti-Clark 三相 → 中点注入 → 占空比 → CCR
  *
  *  为什么要把中点注入也照搬过来：
  *      只做正弦调制（SPWM）时，母线 12V 最多只能输出 6V 峰值相电压；
  *      加上中点注入后能到 Vbus/√3 ≈ 6.93V，且三段式的三相波形与闭环时
  *      完全同形，切换瞬间电压不会有形状突变。
  ******************************************************************************
  */

#include "vf_start.h"
#include "tmr1_pwm.h"
#include <math.h>

#define VF_TWO_PI       6.2831853f   /* 2π，用于角度取模 */
#define VF_SQRT3_HALF   0.8660254f   /* √3/2，Clark 反变换系数 */
#define VF_TS           0.0001f      /* 控制周期 100us（10kHz） */

static float    s_theta;    /* 当前电角度（rad，0~2π） */
static uint32_t s_step;     /* 已执行拍数，用于划分预定位/升频两段 */
static uint8_t  s_active;   /* 1=VF 阶段进行中；0=未启动或已复位 */

/**
  * @brief  把占空比（0~1）写进指定通道的 CCR
  * @param  ch   通道号 1~3
  * @param  duty 占空比，0.0~1.0
  * @retval none
  * @note   先钳位再换算，防止浮点越界后强转成 uint16 变成乱值
  */
static void vf_set_duty(uint8_t ch, float duty)
{
  if (duty < 0.0f)
  {
    duty = 0.0f;
  }
  if (duty > 1.0f)
  {
    duty = 1.0f;
  }
  tmr1_pwm_set_ccr(ch, (uint16_t)(duty * VF_PWM_FULL));
}

/**
  * @brief  输出一个电压矢量（αβ 坐标），电压单位 V（峰值）
  * @param  valpha α 轴电压
  * @param  vbeta  β 轴电压
  * @retval none
  * @note   完全复现 currloop0 里 SVPWM 子系统的算法：
  *           Anti-Clark 反算三相 → 中点注入 → duty = 0.5 − u/Vbus
  *         连符号约定都保持一致，这样闭环接上去时电压是连续的。
  */
static void vf_output_vector(float valpha, float vbeta)
{
  float ua;    /* U 相电压（相对中性点） */
  float ub;    /* V 相电压 */
  float uc;    /* W 相电压 */
  float vmin;  /* 三相比较最小值，用于中点注入 */
  float vmax;  /* 三相比较最大值 */
  float voff;  /* 中点偏移量 */

  /* ---- Anti-Clark：αβ → 三相 ---- */
  ua = valpha;
  ub = -0.5f * valpha + VF_SQRT3_HALF * vbeta;
  uc = -0.5f * valpha - VF_SQRT3_HALF * vbeta;

  /* ---- 求三相极值 ---- */
  vmin = (ua < ub) ? ua : ub;
  if (uc < vmin)
  {
    vmin = uc;
  }
  vmax = (ua > ub) ? ua : ub;
  if (uc > vmax)
  {
    vmax = uc;
  }

  /* ---- 中点注入：voff = −(vmax+vmin)/2，把三相整体搬正 ---- */
  voff = -0.5f * (vmax + vmin);

  /* ---- 占空比：duty = 0.5 − (u+voff)/Vbus，与 currloop0 同符号 ---- */
  vf_set_duty(TMR1_CH_U, 0.5f - (ua + voff) / VF_VBUS_VOLT);
  vf_set_duty(TMR1_CH_V, 0.5f - (ub + voff) / VF_VBUS_VOLT);
  vf_set_duty(TMR1_CH_W, 0.5f - (uc + voff) / VF_VBUS_VOLT);
}

void vf_start_begin(void)
{
  s_theta  = 0.0f;
  s_step   = 0U;
  s_active = 1U;
}

void vf_start_reset(void)
{
  s_theta  = 0.0f;
  s_step   = 0U;
  s_active = 0U;
}

uint8_t vf_start_finished(void)
{
  uint32_t total = (uint32_t)VF_ALIGN_STEPS + (uint32_t)VF_RAMP_STEPS;

  return (s_step >= total) ? 1U : 0U;
}

float vf_start_get_theta(void)
{
  return s_theta;
}

void vf_start_step(void)
{
  float    freq_hz;   /* 当前电频率 Hz */
  float    volt;      /* 当前相电压峰值 V */
  float    valpha;    /* α 轴电压 */
  float    vbeta;     /* β 轴电压 */
  uint32_t ramp_step; /* 升频段已走过的拍数 */

  if (s_active == 0U)
  {
    return;
  }

  s_step++;

  if (s_step <= (uint32_t)VF_ALIGN_STEPS)
  {
    /* ---- 阶段一：预定位 ----
     * 角度固定在 0°，只加电压不转。转子被磁场拉到 0° 并稳住，
     * 这样后面开始旋转时，初始角度是确定的，不会一上来就乱甩。 */
    s_theta = 0.0f;
    volt    = VF_V_BOOST;
  }
  else
  {
    /* ---- 阶段二：线性升频 ----
     * 频率从 VF_FREQ_START_HZ 平滑升到 VF_FREQ_END_HZ，
     * 电压同步按 V = V_BOOST + K_VF×f 上升（近似恒磁通）。 */
    ramp_step = s_step - (uint32_t)VF_ALIGN_STEPS;
    if (ramp_step > (uint32_t)VF_RAMP_STEPS)
    {
      ramp_step = (uint32_t)VF_RAMP_STEPS;   /* 到顶后保持终速 */
    }

    freq_hz = VF_FREQ_START_HZ
              + (VF_FREQ_END_HZ - VF_FREQ_START_HZ)
                * ((float)ramp_step / (float)VF_RAMP_STEPS);

    volt = VF_V_BOOST + VF_K_VF * freq_hz;

    /* 角度积分：Δθ = 2π·f·Ts */
    s_theta += VF_TWO_PI * freq_hz * VF_TS;
    if (s_theta >= VF_TWO_PI)
    {
      s_theta -= VF_TWO_PI;
    }
  }

  /* 电压矢量：幅值 volt，角度 s_theta（逆时针旋转） */
  valpha = volt * cosf(s_theta);
  vbeta  = volt * sinf(s_theta);

  vf_output_vector(valpha, vbeta);
}
