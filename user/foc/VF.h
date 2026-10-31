/*
 * File: VF.h
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

#ifndef VF_h_
#define VF_h_
#ifndef VF_COMMON_INCLUDES_
#define VF_COMMON_INCLUDES_
#include "rtwtypes.h"
#endif                                 /* VF_COMMON_INCLUDES_ */

/* Block signals and states (default storage) for system '<Root>' */
typedef struct {
  real32_T UnitDelay_DSTATE;           /* '<S3>/Unit Delay' */
} DW;

/* Constant parameters (default storage) */
typedef struct {
  /* Computed Parameter: Constant_Value
   * Referenced by: '<S4>/Constant'
   */
  real32_T Constant_Value;

  /* Computed Parameter: Gain_Gain_c
   * Referenced by: '<S4>/Gain'
   */
  real32_T Gain_Gain_c;

  /* Computed Parameter: PWM_HalfPeriod_Gain
   * Referenced by: '<S4>/PWM_HalfPeriod'
   */
  real32_T PWM_HalfPeriod_Gain;
} ConstP;

/* External inputs (root inport signals with default storage) */
typedef struct {
  real32_T ud;                         /* '<Root>/ud' */
  real32_T uq;                         /* '<Root>/uq' */
  real32_T Freq;                       /* '<Root>/Freq' */
  real32_T v_bus;                      /* '<Root>/v_bus' */
} ExtU;

/* External outputs (root outports fed by signals with default storage) */
typedef struct {
  real32_T tABC[3];                    /* '<Root>/tABC' */
} ExtY;

/* Block signals and states (default storage) */
extern DW rtDW;

/* External inputs (root inport signals with default storage) */
extern ExtU rtU;

/* External outputs (root outports fed by signals with default storage) */
extern ExtY rtY;

/* Constant parameters (default storage) */
extern const ConstP rtConstP;

/* Model entry point functions */
extern void VF_initialize(void);
extern void VF_step(void);

/*-
 * These blocks were eliminated from the model due to optimizations:
 *
 * Block '<S4>/Scope1' : Unused code path elimination
 * Block '<S4>/Scope2' : Unused code path elimination
 * Block '<S4>/Scope3' : Unused code path elimination
 * Block '<S1>/Scope2' : Unused code path elimination
 * Block '<S1>/Data Type Conversion3' : Eliminate redundant data type conversion
 * Block '<S1>/Data Type Conversion4' : Eliminate redundant data type conversion
 */

/*-
 * The generated code includes comments that allow you to trace directly
 * back to the appropriate location in the model.  The basic format
 * is <system>/block_name, where system is the system number (uniquely
 * assigned by Simulink) and block_name is the name of the block.
 *
 * Note that this particular code originates from a subsystem build,
 * and has its own system numbers different from the parent model.
 * Refer to the system hierarchy for this subsystem below, and use the
 * MATLAB hilite_system command to trace the generated code back
 * to the parent model.  For example,
 *
 * hilite_system('SVPWM_2021b/VF mode')    - opens subsystem SVPWM_2021b/VF mode
 * hilite_system('SVPWM_2021b/VF mode/Kp') - opens and selects block Kp
 *
 * Here is the system hierarchy for this model
 *
 * '<Root>' : 'SVPWM_2021b'
 * '<S1>'   : 'SVPWM_2021b/VF mode'
 * '<S2>'   : 'SVPWM_2021b/VF mode/AntiPark'
 * '<S3>'   : 'SVPWM_2021b/VF mode/FreqGenerator'
 * '<S4>'   : 'SVPWM_2021b/VF mode/SVPWM'
 * '<S5>'   : 'SVPWM_2021b/VF mode/FreqGenerator/RangeLimit'
 * '<S6>'   : 'SVPWM_2021b/VF mode/FreqGenerator/RangeLimit/If Action Subsystem1'
 * '<S7>'   : 'SVPWM_2021b/VF mode/FreqGenerator/RangeLimit/If Action Subsystem2'
 * '<S8>'   : 'SVPWM_2021b/VF mode/FreqGenerator/RangeLimit/If Action Subsystem3'
 * '<S9>'   : 'SVPWM_2021b/VF mode/SVPWM/AntiClark'
 * '<S10>'  : 'SVPWM_2021b/VF mode/SVPWM/ei_t'
 */
#endif                                 /* VF_h_ */

/*
 * File trailer for generated code.
 *
 * [EOF]
 */
