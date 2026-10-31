/*
 * File: VF.c
 *
 * Code generated for Simulink model 'VF'.
 *
 * Model version                  : 1.2
 * Simulink Coder version         : 25.2 (R2025b) 28-Jul-2025
 * C/C++ source code generated on : Sat Oct 24 21:14:34 2026
 *
 * Target selection: ert.tlc
 * Embedded hardware selection: ARM Compatible->ARM Cortex-M
 * Code generation objectives:
 *    1. Execution efficiency
 *    2. RAM efficiency
 * Validation result: Not run
 */

#include "VF.h"
#include "rtwtypes.h"
#include <math.h>
#include "mw_cmsis.h"

/* Block signals and states (default storage) */
DW rtDW;

/* External inputs (root inport signals with default storage) */
ExtU rtU;

/* External outputs (root outports fed by signals with default storage) */
ExtY rtY;
static void SVPWM(real32_T rtu_Valpha, real32_T rtu_Vbeta, real32_T rtu_v_bus,
                  real32_T rty_tABC[3]);
static void VFmode(void);

/* Output and update for atomic system: '<S1>/SVPWM' */
static void SVPWM(real32_T rtu_Valpha, real32_T rtu_Vbeta, real32_T rtu_v_bus,
                  real32_T rty_tABC[3])
{
  real32_T tmp[3];
  real32_T tmp_0[3];
  real32_T rtb_Min;
  real32_T rtb_Min1;
  real32_T rtb_Sum_p;

  /* Outputs for Atomic SubSystem: '<S4>/AntiClark' */
  /* Gain: '<S9>/Gain' incorporates:
   *  SignalConversion generated from: '<S9>/Valpha'
   */
  rtb_Min1 = -0.5F * rtu_Valpha;

  /* Gain: '<S9>/Gain1' */
  rtb_Min = 0.866025388F * rtu_Vbeta;

  /* Sum: '<S9>/Sum' */
  rtb_Sum_p = rtb_Min1 + rtb_Min;

  /* Sum: '<S9>/Sum1' */
  rtb_Min1 -= rtb_Min;

  /* Outputs for Atomic SubSystem: '<S4>/ei_t' */
  /* Gain: '<S10>/Gain' incorporates:
   *  MinMax: '<S10>/Min'
   *  MinMax: '<S10>/Min1'
   *  SignalConversion generated from: '<S9>/Valpha'
   *  Sum: '<S10>/Sum'
   */
  rtb_Min = (fminf(fminf(rtu_Valpha, rtb_Sum_p), rtb_Min1) + fmaxf(fmaxf
              (rtu_Valpha, rtb_Sum_p), rtb_Min1)) * -0.5F;

  /* End of Outputs for SubSystem: '<S4>/ei_t' */

  /* Sum: '<S4>/Sum' incorporates:
   *  SignalConversion generated from: '<S9>/Valpha'
   */
  rty_tABC[0] = rtb_Min + rtu_Valpha;

  /* End of Outputs for SubSystem: '<S4>/AntiClark' */
  rty_tABC[1] = rtb_Min + rtb_Sum_p;
  rty_tABC[2] = rtb_Min + rtb_Min1;

  /* Gain: '<S4>/Gain' */
  mw_arm_scale_1_f32(&rtConstP.Gain_Gain_c, &rty_tABC[0], &tmp[0], 3U);

  /* Sum: '<S4>/Sum1' incorporates:
   *  Constant: '<S4>/Constant'
   *  Gain: '<S4>/Gain'
   *  Product: '<S4>/Divide'
   */
  tmp[0] /= rtu_v_bus;
  tmp[1] /= rtu_v_bus;
  tmp[2] /= rtu_v_bus;
  mw_arm_bias_2_f32(&tmp[0], &rtConstP.Constant_Value, &tmp_0[0], 3U);

  /* Gain: '<S4>/PWM_HalfPeriod' incorporates:
   *  Sum: '<S4>/Sum1'
   */
  mw_arm_scale_1_f32(&rtConstP.PWM_HalfPeriod_Gain, &tmp_0[0], &rty_tABC[0], 3U);
}

/* Output and update for atomic system: '<Root>/VF mode' */
static void VFmode(void)
{
  real32_T rtb_Merge;
  real32_T rtb_Sum;

  /* Outputs for Atomic SubSystem: '<S1>/FreqGenerator' */
  /* Sum: '<S3>/Sum' incorporates:
   *  Gain: '<S3>/Gain'
   *  Inport: '<Root>/Freq'
   *  UnitDelay: '<S3>/Unit Delay'
   */
  rtb_Sum = 0.000628318521F * rtU.Freq + rtDW.UnitDelay_DSTATE;

  /* If: '<S5>/If' incorporates:
   *  Constant: '<S5>/Constant'
   *  Constant: '<S5>/Constant1'
   *  RelationalOperator: '<S5>/Relational Operator'
   *  RelationalOperator: '<S5>/Relational Operator1'
   */
  if (rtb_Sum > 6.28318548F) {
    /* Outputs for IfAction SubSystem: '<S5>/If Action Subsystem1' incorporates:
     *  ActionPort: '<S6>/Action Port'
     */
    /* Sum: '<S6>/Sum' incorporates:
     *  Constant: '<S6>/Constant'
     */
    rtb_Merge = rtb_Sum - 6.28318548F;

    /* End of Outputs for SubSystem: '<S5>/If Action Subsystem1' */
  } else if (rtb_Sum < 0.0F) {
    /* Outputs for IfAction SubSystem: '<S5>/If Action Subsystem3' incorporates:
     *  ActionPort: '<S8>/Action Port'
     */
    /* Sum: '<S8>/Sum' incorporates:
     *  Constant: '<S8>/Constant'
     */
    rtb_Merge = rtb_Sum + 6.28318548F;

    /* End of Outputs for SubSystem: '<S5>/If Action Subsystem3' */
  } else {
    /* Outputs for IfAction SubSystem: '<S5>/If Action Subsystem2' incorporates:
     *  ActionPort: '<S7>/Action Port'
     */
    /* SignalConversion generated from: '<S7>/In1' */
    rtb_Merge = rtb_Sum;

    /* End of Outputs for SubSystem: '<S5>/If Action Subsystem2' */
  }

  /* End of If: '<S5>/If' */

  /* Update for UnitDelay: '<S3>/Unit Delay' */
  rtDW.UnitDelay_DSTATE = rtb_Merge;

  /* End of Outputs for SubSystem: '<S1>/FreqGenerator' */

  /* Trigonometry: '<S1>/SinCos' */
  rtb_Sum = arm_sin_f32(rtb_Merge);

  /* Trigonometry: '<S1>/SinCos1' */
  rtb_Merge = arm_cos_f32(rtb_Merge);

  /* Outputs for Atomic SubSystem: '<S1>/SVPWM' */
  /* Outputs for Atomic SubSystem: '<S1>/AntiPark' */
  /* Sum: '<S2>/Add' incorporates:
   *  Inport: '<Root>/ud'
   *  Inport: '<Root>/uq'
   *  Inport: '<Root>/v_bus'
   *  Outport: '<Root>/tABC'
   *  Product: '<S2>/Product'
   *  Product: '<S2>/Product1'
   *  Product: '<S2>/Product2'
   *  Product: '<S2>/Product3'
   *  Sum: '<S2>/Add1'
   */
  SVPWM(rtU.ud * rtb_Merge - rtU.uq * rtb_Sum, rtU.ud * rtb_Sum + rtU.uq *
        rtb_Merge, rtU.v_bus, rtY.tABC);

  /* End of Outputs for SubSystem: '<S1>/AntiPark' */
  /* End of Outputs for SubSystem: '<S1>/SVPWM' */
}

/* Model step function */
void VF_step(void)
{
  /* Outputs for Atomic SubSystem: '<Root>/VF mode' */
  VFmode();

  /* End of Outputs for SubSystem: '<Root>/VF mode' */
}

/* Model initialize function */
void VF_initialize(void)
{
  /* (no initialization code required) */
}

/*
 * File trailer for generated code.
 *
 * [EOF]
 */
