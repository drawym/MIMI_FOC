/*
 * File: currloop0.c
 *
 * Code generated for Simulink model 'currloop0'.
 *
 * Model version                  : 9.4
 * Simulink Coder version         : 25.2 (R2025b) 28-Jul-2025
 * C/C++ source code generated on : Sun Nov  1 22:50:05 2026
 *
 * Target selection: ert.tlc
 * Embedded hardware selection: ARM Compatible->ARM Cortex-M
 * Code generation objectives:
 *    1. Execution efficiency
 *    2. RAM efficiency
 * Validation result: Not run
 */

#include "currloop0.h"
#include "rtwtypes.h"
#include <math.h>
#include "mw_cmsis.h"
#include <float.h>
#include "math.h"

/* Named constants for Chart: '<S1>/Chart' */
#define IN_AlignStage                  ((uint8_T)1U)
#define IN_IDLE                        ((uint8_T)2U)
#define IN_OpenStage                   ((uint8_T)3U)
#define IN_RunStage                    ((uint8_T)4U)

/* ===== 启动流程参数（手调值，重新导出模型会覆盖，记得同步改模型）=====
 * 目标：预定位 → 强拖 → 闭环三段平滑衔接，不需要用手扶电机。
 *
 * 三条规则：
 *   1) 强拖速度从 OPEN_RPM_START 线性升到 OPEN_RPM_END，整个 OpenStage 升完，
 *      起步慢才拖得动（磁拉力有限，速度阶跃会让转子直接失步）；
 *   2) RunStage 的角速度必须【等于】OPEN_RPM_END —— 切换瞬间频率连续；
 *   3) 切换时还要把累积相位交接过去（见 case IN_OpenStage），否则磁场突变。
 *
 * 改转速只需动两个 RPM 宏，rad/s 由 ×2π/60 自动换算。
 * 0.10471976F = 2π/60，把 rpm 换成机械角速度 rad/s。 */
#define OPEN_RPM_START        10.0F     /* 强拖起始转速：极慢，先让转子跟住 */
#define OPEN_RPM_END          60.0F     /* 强拖终速，同时也是闭环转速（电频率 7Hz） */
#define OPEN_W_START         (OPEN_RPM_START * 0.10471976F)  /* ≈ 2.09 rad/s */
#define OPEN_W_END           (OPEN_RPM_END   * 0.10471976F)  /* ≈ 12.57 rad/s */
#define OPEN_RAMP_DIV        (1.0F / 30000.0F)               /* OpenStage 共 30000 拍 = 3s */

/* Exported block signals */
real32_T ThetaOpen;                    /* '<S1>/Merge' */

/* Exported data definition */

/* Definition for custom storage class: Struct */
CurrKPKI_type CurrKPKI = {
  /* CurrKi = 2040：与 CurrKp 一起满足零极点对消 Ki/Kp = R/L = 2833，
   * 对应电流环带宽 Kp/L = 0.688/0.0009 ≈ 764 rad/s ≈ 122Hz。
   *
   * ⚠ 为什么不能用原来那组 Kp=0.09 / Ki=25：
   *   那组带宽只有 15.9Hz，而电机 120rpm 的基波频率就有 14Hz ——
   *   电流环在基波频率处增益只剩 75%、相位滞后 41°，
   *   定子电流矢量被"拉扁+拖后"，旋转磁场不再是匀速圆，
   *   转矩脉动直接把转子拽得左右晃。
   *   电流环带宽至少要做到基波频率的 10 倍，才谈得上"跟得上"。 */
  2040.0F,

  /* CurrKp */
  0.688F
};

motor_type motor = {
  /* Pn */
  7.0F
};

/* Block signals and states (default storage) */
DW rtDW;

/* External inputs (root inport signals with default storage) */
ExtU rtU;

/* External outputs (root outports fed by signals with default storage) */
ExtY rtY;

/* Real-time model */
static RT_MODEL rtM_;
RT_MODEL *const rtM = &rtM_;
extern real32_T rt_modf_snf(real32_T u0, real32_T u1);
static void SVPWM(real32_T rtu_Valpha, real32_T rtu_Vbeta, real32_T rtu_v_bus,
                  real32_T rty_tABC[3]);
static real_T rtGetNaN(void);
static real32_T rtGetNaNF(void);
extern real_T rtInf;
extern real_T rtMinusInf;
extern real_T rtNaN;
extern real32_T rtInfF;
extern real32_T rtMinusInfF;
extern real32_T rtNaNF;
static boolean_T rtIsInf(real_T value);
static boolean_T rtIsInfF(real32_T value);
static boolean_T rtIsNaN(real_T value);
static boolean_T rtIsNaNF(real32_T value);
real_T rtNaN = -(real_T)NAN;
real_T rtInf = (real_T)INFINITY;
real_T rtMinusInf = -(real_T)INFINITY;
real32_T rtNaNF = -(real32_T)NAN;
real32_T rtInfF = (real32_T)INFINITY;
real32_T rtMinusInfF = -(real32_T)INFINITY;

/* Return rtNaN needed by the generated code. */
static real_T rtGetNaN(void)
{
  return rtNaN;
}

/* Return rtNaNF needed by the generated code. */
static real32_T rtGetNaNF(void)
{
  return rtNaNF;
}

/* Test if value is infinite */
static boolean_T rtIsInf(real_T value)
{
  return (boolean_T)isinf(value);
}

/* Test if single-precision value is infinite */
static boolean_T rtIsInfF(real32_T value)
{
  return (boolean_T)isinf(value);
}

/* Test if value is not a number */
static boolean_T rtIsNaN(real_T value)
{
  return (boolean_T)(isnan(value) != 0);
}

/* Test if single-precision value is not a number */
static boolean_T rtIsNaNF(real32_T value)
{
  return (boolean_T)(isnan(value) != 0);
}

/* Output and update for atomic system: '<S1>/SVPWM' */
static void SVPWM(real32_T rtu_Valpha, real32_T rtu_Vbeta, real32_T rtu_v_bus,
                  real32_T rty_tABC[3])
{
  real32_T tmp[3];
  real32_T tmp_0[3];
  real32_T rtb_Gain_a;
  real32_T rtb_Min;
  real32_T rtb_Sum_o;

  /* Outputs for Atomic SubSystem: '<S10>/AntiClark' */
  /* Gain: '<S12>/Gain' incorporates:
   *  SignalConversion generated from: '<S12>/Valpha'
   */
  rtb_Gain_a = -0.5F * rtu_Valpha;

  /* Gain: '<S12>/Gain1' */
  rtb_Min = 0.866025388F * rtu_Vbeta;

  /* Sum: '<S12>/Sum' */
  rtb_Sum_o = rtb_Gain_a + rtb_Min;

  /* Sum: '<S12>/Sum1' */
  rtb_Min = rtb_Gain_a - rtb_Min;

  /* Gain: '<S13>/Gain' incorporates:
   *  MinMax: '<S13>/Min'
   *  MinMax: '<S13>/Min1'
   *  SignalConversion generated from: '<S12>/Valpha'
   *  Sum: '<S13>/Sum'
   */
  rtb_Gain_a = (fminf(fminf(rtu_Valpha, rtb_Sum_o), rtb_Min) + fmaxf(fmaxf
    (rtu_Valpha, rtb_Sum_o), rtb_Min)) * -0.5F;

  /* Sum: '<S10>/Sum' incorporates:
   *  SignalConversion generated from: '<S12>/Valpha'
   */
  rty_tABC[0] = rtb_Gain_a + rtu_Valpha;

  /* End of Outputs for SubSystem: '<S10>/AntiClark' */
  rty_tABC[1] = rtb_Gain_a + rtb_Sum_o;
  rty_tABC[2] = rtb_Gain_a + rtb_Min;

  /* Gain: '<S10>/Gain' */
  mw_arm_scale_1_f32(&rtConstP.Gain_Gain_i, &rty_tABC[0], &tmp[0], 3U);

  /* Sum: '<S10>/Sum1' incorporates:
   *  Constant: '<S10>/Constant'
   *  Gain: '<S10>/Gain'
   *  Product: '<S10>/Divide'
   */
  tmp[0] /= rtu_v_bus;
  tmp[1] /= rtu_v_bus;
  tmp[2] /= rtu_v_bus;
  mw_arm_bias_2_f32(&tmp[0], &rtConstP.pooled3, &tmp_0[0], 3U);

  /* Gain: '<S10>/PWM_HalfPeriod' incorporates:
   *  Sum: '<S10>/Sum1'
   */
  mw_arm_scale_1_f32(&rtConstP.PWM_HalfPeriod_Gain, &tmp_0[0], &rty_tABC[0], 3U);
}

real32_T rt_modf_snf(real32_T u0, real32_T u1)
{
  real32_T y;
  y = u0;
  if (u1 == 0.0F) {
    if (u0 == 0.0F) {
      y = u1;
    }
  } else if (rtIsNaNF(u0) || rtIsNaNF(u1) || rtIsInfF(u0)) {
    y = (rtNaNF);
  } else if (u0 == 0.0F) {
    y = 0.0F / u1;
  } else if (rtIsInfF(u1)) {
    if ((u1 < 0.0F) != (u0 < 0.0F)) {
      y = u1;
    }
  } else {
    boolean_T yEq;
    y = fmodf(u0, u1);
    yEq = (y == 0.0F);
    if ((!yEq) && (u1 > floorf(u1))) {
      real32_T q;
      q = fabsf(u0 / u1);
      yEq = !(fabsf(q - floorf(q + 0.5F)) > FLT_EPSILON * q);
    }

    if (yEq) {
      y = u1 * 0.0F;
    } else if ((u0 < 0.0F) != (u1 < 0.0F)) {
      y += u1;
    }
  }

  return y;
}

/* Model step function */
void currloop0_step(void)
{
  real32_T rtb_Add1_o;
  real32_T rtb_DiscreteTimeIntegrator;
  real32_T rtb_Gain2;
  real32_T rtb_IProdOut;
  real32_T rtb_Integrator_f;
  real32_T rtb_Saturation;
  real32_T rtb_SinCos1;
  real32_T rtb_Sum;
  real32_T rtb_Switch;
  real32_T rtb_OpenMechW;   /* 强拖阶段当前机械角速度（rad/s），带斜坡，非模型原始变量 */
  int8_T tmp;
  int8_T tmp_0;

  /* Outputs for Atomic SubSystem: '<Root>/currloop' */
  /* Outputs for Atomic SubSystem: '<S1>/Clark' */
  /* Sum: '<S4>/Add1' incorporates:
   *  Gain: '<S4>/Gain'
   *  Gain: '<S4>/Gain1'
   *  Inport: '<Root>/ia'
   *  Inport: '<Root>/ib'
   *  Inport: '<Root>/ic'
   *  Sum: '<S4>/Add'
   */
  rtb_Add1_o = 0.666666687F * rtU.ia - (rtU.ib + rtU.ic) * 0.333333343F;

  /* Gain: '<S4>/Gain2' incorporates:
   *  Inport: '<Root>/ib'
   *  Inport: '<Root>/ic'
   *  Sum: '<S4>/Add2'
   */
  rtb_Gain2 = (rtU.ib - rtU.ic) * 0.577350259F;

  /* End of Outputs for SubSystem: '<S1>/Clark' */

  /* Chart: '<S1>/Chart' incorporates:
   *  Inport: '<Root>/Motor_OnOff'
   */
  if (rtDW.temporalCounter_i1 < 32767) {
    rtDW.temporalCounter_i1++;
  }

  if (rtDW.is_active_c3_currloop0 == 0) {
    rtDW.is_active_c3_currloop0 = 1U;
    rtDW.is_c3_currloop0 = IN_IDLE;
  } else {
    switch (rtDW.is_c3_currloop0) {
     case IN_AlignStage:
      /* 预定位时长：原 1000 拍（0.1s），转子还没稳就进强拖，容易一上来就抖。
       * 改 3000 拍（0.3s），给转子足够时间对齐到已知角度。 */
      if (rtDW.temporalCounter_i1 >= 3000) {
        rtDW.temporalCounter_i1 = 0U;
        rtDW.is_c3_currloop0 = IN_OpenStage;
        rtDW.ZReset = 0.0;
      } else if (rtU.Motor_OnOff == 0.0F) {
        rtDW.is_c3_currloop0 = IN_IDLE;
      } else {
        rtDW.Motor_state = 2.0;
      }
      break;

     case IN_IDLE:
      if (rtU.Motor_OnOff == 1.0F) {
        rtDW.temporalCounter_i1 = 0U;
        rtDW.is_c3_currloop0 = IN_AlignStage;
        rtDW.ZReset = 0.0;
      } else {
        rtDW.Motor_state = 1.0;
      }
      break;

     case IN_OpenStage:
      if (rtU.Motor_OnOff == 0.0F) {
        rtDW.is_c3_currloop0 = IN_IDLE;
      } else if (rtDW.temporalCounter_i1 >= 30000) {
        rtDW.is_c3_currloop0 = IN_RunStage;
        /* ---- 关键：切换瞬间把累积的电角度交接给闭环积分器 ----
         * 强拖用 DiscreteTimeIntegrator_DSTATE（<S7>），
         * 闭环用 DiscreteTimeIntegrator1_DSTATE（<S8>），是两个独立变量。
         * 不做交接的话，闭环从 S8 的旧值（通常是 0）重新起算，
         * ThetaOpen 会从强拖末尾的角度【瞬间跳回 0】——
         * 磁场方向突变，转子被猛拽一下，这就是"切闭环要用手扶"的另一个原因。
         * 两者都是电角度（rad，已乘过 Pn），直接赋值即可保证相位连续。 */
        rtDW.DiscreteTimeIntegrator1_DSTATE = rtDW.DiscreteTimeIntegrator_DSTATE;
      } else {
        rtDW.ZReset = 1.0;
        rtDW.Motor_state = 3.0;
      }
      break;

     default:
      /* case IN_RunStage: */
      if (rtU.Motor_OnOff == 0.0F) {
        rtDW.is_c3_currloop0 = IN_IDLE;
      } else {
        rtDW.Motor_state = 4.0;
      }
      break;
    }
  }

  /* End of Chart: '<S1>/Chart' */

  /* SwitchCase: '<S1>/Switch Case' */
  switch ((int32_T)rtDW.Motor_state) {
   case 1:
    /* Outputs for IfAction SubSystem: '<S1>/If Action Subsystem' incorporates:
     *  ActionPort: '<S5>/Action Port'
     */
    /* Merge: '<S1>/Merge' incorporates:
     *  Constant: '<S5>/Constant'
     *  SignalConversion generated from: '<S5>/theta_fd'
     */
    ThetaOpen = 0.0F;

    /* Merge: '<S1>/Merge1' incorporates:
     *  Constant: '<S5>/Constant1'
     *  SignalConversion generated from: '<S5>/iq_ref'
     */
    rtDW.Merge1 = 0.0F;

    /* Merge: '<S1>/Merge2' incorporates:
     *  Constant: '<S5>/Constant2'
     *  SignalConversion generated from: '<S5>/id_ref'
     */
    rtDW.Merge2 = 0.0F;

    /* End of Outputs for SubSystem: '<S1>/If Action Subsystem' */
    break;

   case 2:
    /* Outputs for IfAction SubSystem: '<S1>/If Action Subsystem1' incorporates:
     *  ActionPort: '<S6>/Action Port'
     */
    /* Merge: '<S1>/Merge' incorporates:
     *  Constant: '<S6>/Constant'
     *  SignalConversion generated from: '<S6>/theta_fd'
     */
    ThetaOpen = 0.0F;

    /* Merge: '<S1>/Merge1' incorporates:
     *  Constant: '<S6>/Constant1'
     *  SignalConversion generated from: '<S6>/iq_ref'
     */
    rtDW.Merge1 = 0.0F;

    /* Merge: '<S1>/Merge2' incorporates:
     *  Constant: '<S6>/Constant2'
     *  SignalConversion generated from: '<S6>/id_ref'
     */
    rtDW.Merge2 = 0.5F;

    /* End of Outputs for SubSystem: '<S1>/If Action Subsystem1' */
    break;

   case 3:
    /* Outputs for IfAction SubSystem: '<S1>/If Action Subsystem2' incorporates:
     *  ActionPort: '<S7>/Action Port'
     */
    /* DiscreteIntegrator: '<S7>/Discrete-Time Integrator' */
    if ((rtDW.ZReset > 0.0) && (rtDW.DiscreteTimeIntegrator_PrevRese <= 0)) {
      rtDW.DiscreteTimeIntegrator_DSTATE = 0.0F;
    }

    rtb_DiscreteTimeIntegrator = rtDW.DiscreteTimeIntegrator_DSTATE;

    /* End of DiscreteIntegrator: '<S7>/Discrete-Time Integrator' */

    /* DiscreteIntegrator: '<S7>/Discrete-Time Integrator1' */
    if ((rtDW.ZReset > 0.0) && (rtDW.DiscreteTimeIntegrator1_PrevRes <= 0)) {
      rtDW.DiscreteTimeIntegrator1_DSTAT_c = 0.0F;
    }

    /* Merge: '<S1>/Merge' incorporates:
     *  Constant: '<S7>/Constant1'
     *  DiscreteIntegrator: '<S7>/Discrete-Time Integrator1'
     *  Math: '<S7>/Mod'
     *  SignalConversion generated from: '<S7>/theta_fd'
     */
    ThetaOpen = rt_modf_snf(rtDW.DiscreteTimeIntegrator1_DSTAT_c, 6.28318501F);

    /* Merge: '<S1>/Merge1' incorporates:
     *  Constant: '<S7>/Constant3'
     *  SignalConversion generated from: '<S7>/iq_ref'
     */
    rtDW.Merge1 = 0.0F;

    /* Merge: '<S1>/Merge2' incorporates:
     *  Constant: '<S7>/Constant5'
     *  SignalConversion generated from: '<S7>/id_ref'
     */
    rtDW.Merge2 = 0.5F;

    /* Update for DiscreteIntegrator: '<S7>/Discrete-Time Integrator' incorporates:
     *  DiscreteIntegrator: '<S7>/Discrete-Time Integrator1'
     *  Gain: '<S7>/Gain'
     *  Product: '<S7>/Product'
     */
    /* ---- 强拖角速度：带斜坡 ----
     * temporalCounter_i1 在本阶段从 0 数到 30000，正好当时间轴用：
     *   第 0 拍    → OPEN_W_START（20rpm，很慢，先让转子跟住）
     *   第 30000 拍 → OPEN_W_END（120rpm，与闭环同速）
     * 中间线性插值，3 秒平滑升速。
     * 为什么必须斜坡：I/F 控制的磁拉力有限，速度阶跃会让转子直接失步，
     * 表现出来就是"原地抖、转不起来、要用手扶"。 */
    rtb_OpenMechW = OPEN_W_START + (OPEN_W_END - OPEN_W_START)
                    * ((real32_T)rtDW.temporalCounter_i1 * OPEN_RAMP_DIV);
    rtDW.DiscreteTimeIntegrator_DSTATE += motor.Pn * rtb_OpenMechW * 0.0001F;
    if (rtDW.ZReset > 0.0) {
      rtDW.DiscreteTimeIntegrator_PrevRese = 1;
      rtDW.DiscreteTimeIntegrator1_PrevRes = 1;
    } else if (rtDW.ZReset < 0.0) {
      rtDW.DiscreteTimeIntegrator_PrevRese = -1;
      rtDW.DiscreteTimeIntegrator1_PrevRes = -1;
    } else if (rtDW.ZReset == 0.0) {
      rtDW.DiscreteTimeIntegrator_PrevRese = 0;
      rtDW.DiscreteTimeIntegrator1_PrevRes = 0;
    } else {
      rtDW.DiscreteTimeIntegrator_PrevRese = 2;
      rtDW.DiscreteTimeIntegrator1_PrevRes = 2;
    }

    /* End of Update for DiscreteIntegrator: '<S7>/Discrete-Time Integrator' */

    /* Update for DiscreteIntegrator: '<S7>/Discrete-Time Integrator1' */
    rtDW.DiscreteTimeIntegrator1_DSTAT_c += 0.0001F * rtb_DiscreteTimeIntegrator;

    /* End of Outputs for SubSystem: '<S1>/If Action Subsystem2' */
    break;

   case 4:
    /* Outputs for IfAction SubSystem: '<S1>/If Action Subsystem3' incorporates:
     *  ActionPort: '<S8>/Action Port'
     */
    /* Merge: '<S1>/Merge' incorporates:
     *  Constant: '<S8>/Constant1'
     *  DiscreteIntegrator: '<S8>/Discrete-Time Integrator1'
     *  Math: '<S8>/Mod'
     *  SignalConversion generated from: '<S8>/theta_fd'
     */
    ThetaOpen = rt_modf_snf(rtDW.DiscreteTimeIntegrator1_DSTATE, 6.28318501F);

    /* Merge: '<S1>/Merge1' incorporates:
     *  Constant: '<S8>/Constant3'
     *  SignalConversion generated from: '<S8>/iq_ref'
     */
    rtDW.Merge1 = 0.5F;

    /* Merge: '<S1>/Merge2' incorporates:
     *  Constant: '<S8>/Constant5'
     *  SignalConversion generated from: '<S8>/id_ref'
     */
    rtDW.Merge2 = 0.0F;

    /* Update for DiscreteIntegrator: '<S8>/Discrete-Time Integrator1' incorporates:
     *  Gain: '<S8>/Gain'
     */
    /* RunStage（闭环）角速度 = OPEN_W_END，与强拖终速完全相同。
     * 这样切换瞬间频率连续，不会把转子甩掉。
     * 需要改运行转速：只改文件顶部的 OPEN_RPM_END 一处即可，
     * 强拖斜坡终点会自动跟着变，两边永远同步。 */
    rtDW.DiscreteTimeIntegrator1_DSTATE += motor.Pn * OPEN_W_END * 0.0001F;

    /* End of Outputs for SubSystem: '<S1>/If Action Subsystem3' */
    break;
  }

  /* End of SwitchCase: '<S1>/Switch Case' */

  /* Trigonometry: '<S1>/SinCos' */
  rtb_DiscreteTimeIntegrator = sinf(ThetaOpen);

  /* Trigonometry: '<S1>/SinCos1' */
  rtb_SinCos1 = cosf(ThetaOpen);

  /* Outputs for Atomic SubSystem: '<S1>/idq_Controller' */
  /* Outputs for Atomic SubSystem: '<S1>/Park' */
  /* Sum: '<S11>/Sum1' incorporates:
   *  Product: '<S9>/Product'
   *  Product: '<S9>/Product1'
   *  Sum: '<S9>/Add'
   */
  rtb_Sum = rtDW.Merge2 - (rtb_Add1_o * rtb_SinCos1 + rtb_Gain2 *
    rtb_DiscreteTimeIntegrator);

  /* End of Outputs for SubSystem: '<S1>/Park' */

  /* Sum: '<S61>/Sum' incorporates:
   *  Constant: '<S11>/Constant3'
   *  DiscreteIntegrator: '<S52>/Integrator'
   *  Product: '<S57>/PProd Out'
   */
  rtb_IProdOut = rtb_Sum * CurrKPKI.CurrKp + rtDW.Integrator_DSTATE;

  /* DeadZone: '<S44>/DeadZone' */
  if (rtb_IProdOut > 6.23538303F) {
    rtb_Integrator_f = rtb_IProdOut - 6.23538303F;
  } else if (rtb_IProdOut >= -6.23538303F) {
    rtb_Integrator_f = 0.0F;
  } else {
    rtb_Integrator_f = rtb_IProdOut - -6.23538303F;
  }

  /* End of DeadZone: '<S44>/DeadZone' */

  /* Product: '<S49>/IProd Out' incorporates:
   *  Constant: '<S11>/Constant4'
   */
  rtb_Sum *= CurrKPKI.CurrKi;

  /* Switch: '<S42>/Switch1' incorporates:
   *  Constant: '<S42>/Clamping_zero'
   *  Constant: '<S42>/Constant'
   *  Constant: '<S42>/Constant2'
   *  RelationalOperator: '<S42>/fix for DT propagation issue'
   */
  if (rtb_Integrator_f > 0.0F) {
    tmp = 1;
  } else {
    tmp = -1;
  }

  /* Switch: '<S42>/Switch2' incorporates:
   *  Constant: '<S42>/Clamping_zero'
   *  Constant: '<S42>/Constant3'
   *  Constant: '<S42>/Constant4'
   *  RelationalOperator: '<S42>/fix for DT propagation issue1'
   */
  if (rtb_Sum > 0.0F) {
    tmp_0 = 1;
  } else {
    tmp_0 = -1;
  }

  /* Switch: '<S42>/Switch' incorporates:
   *  Constant: '<S42>/Clamping_zero'
   *  Constant: '<S42>/Constant1'
   *  Logic: '<S42>/AND3'
   *  RelationalOperator: '<S42>/Equal1'
   *  RelationalOperator: '<S42>/Relational Operator'
   *  Switch: '<S42>/Switch1'
   *  Switch: '<S42>/Switch2'
   */
  if ((rtb_Integrator_f != 0.0F) && (tmp == tmp_0)) {
    rtb_Switch = 0.0F;
  } else {
    rtb_Switch = rtb_Sum;
  }

  /* End of Switch: '<S42>/Switch' */

  /* Saturate: '<S59>/Saturation' */
  if (rtb_IProdOut > 6.23538303F) {
    rtb_Saturation = 6.23538303F;
  } else if (rtb_IProdOut < -6.23538303F) {
    rtb_Saturation = -6.23538303F;
  } else {
    rtb_Saturation = rtb_IProdOut;
  }

  /* End of Saturate: '<S59>/Saturation' */

  /* Outputs for Atomic SubSystem: '<S1>/Park' */
  /* Sum: '<S11>/Sum7' incorporates:
   *  Product: '<S9>/Product2'
   *  Product: '<S9>/Product3'
   *  Sum: '<S9>/Add1'
   */
  rtb_Integrator_f = rtDW.Merge1 - (rtb_Gain2 * rtb_SinCos1 - rtb_Add1_o *
    rtb_DiscreteTimeIntegrator);

  /* End of Outputs for SubSystem: '<S1>/Park' */

  /* Product: '<S103>/IProd Out' incorporates:
   *  Constant: '<S11>/Constant2'
   */
  rtb_IProdOut = rtb_Integrator_f * CurrKPKI.CurrKi;

  /* Sum: '<S115>/Sum' incorporates:
   *  Constant: '<S11>/Constant1'
   *  DiscreteIntegrator: '<S106>/Integrator'
   *  Product: '<S111>/PProd Out'
   */
  rtb_Sum = rtb_Integrator_f * CurrKPKI.CurrKp + rtDW.Integrator_DSTATE_b;

  /* DeadZone: '<S98>/DeadZone' incorporates:
   *  Saturate: '<S113>/Saturation'
   */
  if (rtb_Sum > 6.23538303F) {
    rtb_Add1_o = rtb_Sum - 6.23538303F;
    rtb_Gain2 = 6.23538303F;
  } else {
    if (rtb_Sum >= -6.23538303F) {
      rtb_Add1_o = 0.0F;
    } else {
      rtb_Add1_o = rtb_Sum - -6.23538303F;
    }

    if (rtb_Sum < -6.23538303F) {
      rtb_Gain2 = -6.23538303F;
    } else {
      rtb_Gain2 = rtb_Sum;
    }
  }

  /* End of DeadZone: '<S98>/DeadZone' */

  /* Update for DiscreteIntegrator: '<S52>/Integrator' */
  rtDW.Integrator_DSTATE += 0.0001F * rtb_Switch;

  /* Switch: '<S96>/Switch1' incorporates:
   *  Constant: '<S96>/Clamping_zero'
   *  Constant: '<S96>/Constant'
   *  Constant: '<S96>/Constant2'
   *  RelationalOperator: '<S96>/fix for DT propagation issue'
   */
  if (rtb_Add1_o > 0.0F) {
    tmp = 1;
  } else {
    tmp = -1;
  }

  /* Switch: '<S96>/Switch2' incorporates:
   *  Constant: '<S96>/Clamping_zero'
   *  Constant: '<S96>/Constant3'
   *  Constant: '<S96>/Constant4'
   *  RelationalOperator: '<S96>/fix for DT propagation issue1'
   */
  if (rtb_IProdOut > 0.0F) {
    tmp_0 = 1;
  } else {
    tmp_0 = -1;
  }

  /* Switch: '<S96>/Switch' incorporates:
   *  Constant: '<S96>/Clamping_zero'
   *  Constant: '<S96>/Constant1'
   *  Logic: '<S96>/AND3'
   *  RelationalOperator: '<S96>/Equal1'
   *  RelationalOperator: '<S96>/Relational Operator'
   *  Switch: '<S96>/Switch1'
   *  Switch: '<S96>/Switch2'
   */
  if ((rtb_Add1_o != 0.0F) && (tmp == tmp_0)) {
    rtb_IProdOut = 0.0F;
  }

  /* Update for DiscreteIntegrator: '<S106>/Integrator' incorporates:
   *  Switch: '<S96>/Switch'
   */
  rtDW.Integrator_DSTATE_b += 0.0001F * rtb_IProdOut;

  /* End of Outputs for SubSystem: '<S1>/idq_Controller' */

  /* Outputs for Atomic SubSystem: '<S1>/SVPWM' */
  /* Outputs for Atomic SubSystem: '<S1>/AntiPark' */
  /* Sum: '<S2>/Add' incorporates:
   *  Inport: '<Root>/v_bus'
   *  Outport: '<Root>/tABC'
   *  Product: '<S2>/Product'
   *  Product: '<S2>/Product1'
   *  Product: '<S2>/Product2'
   *  Product: '<S2>/Product3'
   *  Sum: '<S2>/Add1'
   */
  SVPWM(rtb_Saturation * rtb_SinCos1 - rtb_Gain2 * rtb_DiscreteTimeIntegrator,
        rtb_Saturation * rtb_DiscreteTimeIntegrator + rtb_Gain2 * rtb_SinCos1,
        rtU.v_bus, rtY.tABC);

  /* End of Outputs for SubSystem: '<S1>/AntiPark' */
  /* End of Outputs for SubSystem: '<S1>/SVPWM' */
  /* End of Outputs for SubSystem: '<Root>/currloop' */
}

/* Model initialize function */
void currloop0_initialize(void)
{
  /* SystemInitialize for Atomic SubSystem: '<Root>/currloop' */
  /* SystemInitialize for IfAction SubSystem: '<S1>/If Action Subsystem2' */
  /* InitializeConditions for DiscreteIntegrator: '<S7>/Discrete-Time Integrator' */
  rtDW.DiscreteTimeIntegrator_PrevRese = 2;

  /* InitializeConditions for DiscreteIntegrator: '<S7>/Discrete-Time Integrator1' */
  rtDW.DiscreteTimeIntegrator1_PrevRes = 2;

  /* End of SystemInitialize for SubSystem: '<S1>/If Action Subsystem2' */
  /* End of SystemInitialize for SubSystem: '<Root>/currloop' */
}

/*
 * File trailer for generated code.
 *
 * [EOF]
 */
