/*
 * File: currloop0.h
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

#ifndef currloop0_h_
#define currloop0_h_
#ifndef currloop0_COMMON_INCLUDES_
#define currloop0_COMMON_INCLUDES_
#include "rtwtypes.h"
#include "math.h"
#endif                                 /* currloop0_COMMON_INCLUDES_ */

/* Macros for accessing real-time model data structure */
#ifndef rtmGetErrorStatus
#define rtmGetErrorStatus(rtm)         ((rtm)->errorStatus)
#endif

#ifndef rtmSetErrorStatus
#define rtmSetErrorStatus(rtm, val)    ((rtm)->errorStatus = (val))
#endif

/* Forward declaration for rtModel */
typedef struct tag_RTM RT_MODEL;

/* Block signals and states (default storage) for system '<Root>' */
typedef struct {
  real_T Motor_state;                  /* '<S1>/Chart' */
  real_T ZReset;                       /* '<S1>/Chart' */
  real32_T Merge1;                     /* '<S1>/Merge1' */
  real32_T Merge2;                     /* '<S1>/Merge2' */
  real32_T Integrator_DSTATE;          /* '<S52>/Integrator' */
  real32_T Integrator_DSTATE_b;        /* '<S106>/Integrator' */
  real32_T DiscreteTimeIntegrator1_DSTATE;/* '<S8>/Discrete-Time Integrator1' */
  real32_T DiscreteTimeIntegrator_DSTATE;/* '<S7>/Discrete-Time Integrator' */
  real32_T DiscreteTimeIntegrator1_DSTAT_c;/* '<S7>/Discrete-Time Integrator1' */
  uint16_T temporalCounter_i1;         /* '<S1>/Chart' */
  int8_T DiscreteTimeIntegrator_PrevRese;/* '<S7>/Discrete-Time Integrator' */
  int8_T DiscreteTimeIntegrator1_PrevRes;/* '<S7>/Discrete-Time Integrator1' */
  uint8_T is_active_c3_currloop0;      /* '<S1>/Chart' */
  uint8_T is_c3_currloop0;             /* '<S1>/Chart' */
} DW;

/* Constant parameters (default storage) */
typedef struct {
  /* Computed Parameter: Gain_Gain_i
   * Referenced by: '<S10>/Gain'
   */
  real32_T Gain_Gain_i;

  /* Computed Parameter: PWM_HalfPeriod_Gain
   * Referenced by: '<S10>/PWM_HalfPeriod'
   */
  real32_T PWM_HalfPeriod_Gain;

  /* Pooled Parameter (Expression: 0.5)
   * Referenced by:
   *   '<S6>/Constant2'
   *   '<S7>/Constant5'
   *   '<S8>/Constant3'
   *   '<S10>/Constant'
   */
  real32_T pooled3;
} ConstP;

/* External inputs (root inport signals with default storage) */
typedef struct {
  real32_T ia;                         /* '<Root>/ia' */
  real32_T ib;                         /* '<Root>/ib' */
  real32_T ic;                         /* '<Root>/ic' */
  real32_T v_bus;                      /* '<Root>/v_bus' */
  real32_T Motor_OnOff;                /* '<Root>/Motor_OnOff' */
} ExtU;

/* External outputs (root outports fed by signals with default storage) */
typedef struct {
  real32_T tABC[3];                    /* '<Root>/tABC' */
} ExtY;

/* Type definition for custom storage class: Struct */
typedef struct CurrKPKI_tag {
  real32_T CurrKi;                     /* Referenced by:
                                        * '<S11>/Constant2'
                                        * '<S11>/Constant4'
                                        */
  real32_T CurrKp;                     /* Referenced by:
                                        * '<S11>/Constant1'
                                        * '<S11>/Constant3'
                                        */
} CurrKPKI_type;

typedef struct motor_tag {
  real32_T Pn;                         /* Referenced by:
                                        * '<S7>/Gain'
                                        * '<S8>/Gain'
                                        */
} motor_type;

/* Real-time Model Data Structure */
struct tag_RTM {
  const char_T * volatile errorStatus;
};

/* Block signals and states (default storage) */
extern DW rtDW;

/* External inputs (root inport signals with default storage) */
extern ExtU rtU;

/* External outputs (root outports fed by signals with default storage) */
extern ExtY rtY;

/* Constant parameters (default storage) */
extern const ConstP rtConstP;

/*
 * Exported Global Signals
 *
 * Note: Exported global signals are block signals with an exported global
 * storage class designation.  Code generation will declare the memory for
 * these signals and export their symbols.
 *
 */
extern real32_T ThetaOpen;             /* '<S1>/Merge' */

/* Model entry point functions */
extern void currloop0_initialize(void);
extern void currloop0_step(void);

/* Exported data declaration */

/* Declaration for custom storage class: Struct */
extern CurrKPKI_type CurrKPKI;
extern motor_type motor;

/* Real-time Model object */
extern RT_MODEL *const rtM;

/*-
 * These blocks were eliminated from the model due to optimizations:
 *
 * Block '<S7>/Scope1' : Unused code path elimination
 * Block '<S8>/Scope1' : Unused code path elimination
 * Block '<S1>/Scope' : Unused code path elimination
 * Block '<S1>/Scope1' : Unused code path elimination
 * Block '<S1>/Scope2' : Unused code path elimination
 * Block '<S1>/Scope3' : Unused code path elimination
 * Block '<S1>/Scope4' : Unused code path elimination
 * Block '<S1>/Scope5' : Unused code path elimination
 * Block '<S11>/Scope' : Unused code path elimination
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
 * hilite_system('IF/currloop')    - opens subsystem IF/currloop
 * hilite_system('IF/currloop/Kp') - opens and selects block Kp
 *
 * Here is the system hierarchy for this model
 *
 * '<Root>' : 'IF'
 * '<S1>'   : 'IF/currloop'
 * '<S2>'   : 'IF/currloop/AntiPark'
 * '<S3>'   : 'IF/currloop/Chart'
 * '<S4>'   : 'IF/currloop/Clark'
 * '<S5>'   : 'IF/currloop/If Action Subsystem'
 * '<S6>'   : 'IF/currloop/If Action Subsystem1'
 * '<S7>'   : 'IF/currloop/If Action Subsystem2'
 * '<S8>'   : 'IF/currloop/If Action Subsystem3'
 * '<S9>'   : 'IF/currloop/Park'
 * '<S10>'  : 'IF/currloop/SVPWM'
 * '<S11>'  : 'IF/currloop/idq_Controller'
 * '<S12>'  : 'IF/currloop/SVPWM/AntiClark'
 * '<S13>'  : 'IF/currloop/SVPWM/ei_t'
 * '<S14>'  : 'IF/currloop/idq_Controller/PID Controller1'
 * '<S15>'  : 'IF/currloop/idq_Controller/PID Controller2'
 * '<S16>'  : 'IF/currloop/idq_Controller/PID Controller1/Anti-windup'
 * '<S17>'  : 'IF/currloop/idq_Controller/PID Controller1/D Gain'
 * '<S18>'  : 'IF/currloop/idq_Controller/PID Controller1/External Derivative'
 * '<S19>'  : 'IF/currloop/idq_Controller/PID Controller1/Filter'
 * '<S20>'  : 'IF/currloop/idq_Controller/PID Controller1/Filter ICs'
 * '<S21>'  : 'IF/currloop/idq_Controller/PID Controller1/I Gain'
 * '<S22>'  : 'IF/currloop/idq_Controller/PID Controller1/Ideal P Gain'
 * '<S23>'  : 'IF/currloop/idq_Controller/PID Controller1/Ideal P Gain Fdbk'
 * '<S24>'  : 'IF/currloop/idq_Controller/PID Controller1/Integrator'
 * '<S25>'  : 'IF/currloop/idq_Controller/PID Controller1/Integrator ICs'
 * '<S26>'  : 'IF/currloop/idq_Controller/PID Controller1/N Copy'
 * '<S27>'  : 'IF/currloop/idq_Controller/PID Controller1/N Gain'
 * '<S28>'  : 'IF/currloop/idq_Controller/PID Controller1/P Copy'
 * '<S29>'  : 'IF/currloop/idq_Controller/PID Controller1/Parallel P Gain'
 * '<S30>'  : 'IF/currloop/idq_Controller/PID Controller1/Reset Signal'
 * '<S31>'  : 'IF/currloop/idq_Controller/PID Controller1/Saturation'
 * '<S32>'  : 'IF/currloop/idq_Controller/PID Controller1/Saturation Fdbk'
 * '<S33>'  : 'IF/currloop/idq_Controller/PID Controller1/Sum'
 * '<S34>'  : 'IF/currloop/idq_Controller/PID Controller1/Sum Fdbk'
 * '<S35>'  : 'IF/currloop/idq_Controller/PID Controller1/Tracking Mode'
 * '<S36>'  : 'IF/currloop/idq_Controller/PID Controller1/Tracking Mode Sum'
 * '<S37>'  : 'IF/currloop/idq_Controller/PID Controller1/Tsamp - Integral'
 * '<S38>'  : 'IF/currloop/idq_Controller/PID Controller1/Tsamp - Ngain'
 * '<S39>'  : 'IF/currloop/idq_Controller/PID Controller1/postSat Signal'
 * '<S40>'  : 'IF/currloop/idq_Controller/PID Controller1/preInt Signal'
 * '<S41>'  : 'IF/currloop/idq_Controller/PID Controller1/preSat Signal'
 * '<S42>'  : 'IF/currloop/idq_Controller/PID Controller1/Anti-windup/Disc. Clamping Parallel'
 * '<S43>'  : 'IF/currloop/idq_Controller/PID Controller1/Anti-windup/Disc. Clamping Parallel/Dead Zone'
 * '<S44>'  : 'IF/currloop/idq_Controller/PID Controller1/Anti-windup/Disc. Clamping Parallel/Dead Zone/Enabled'
 * '<S45>'  : 'IF/currloop/idq_Controller/PID Controller1/D Gain/Disabled'
 * '<S46>'  : 'IF/currloop/idq_Controller/PID Controller1/External Derivative/Disabled'
 * '<S47>'  : 'IF/currloop/idq_Controller/PID Controller1/Filter/Disabled'
 * '<S48>'  : 'IF/currloop/idq_Controller/PID Controller1/Filter ICs/Disabled'
 * '<S49>'  : 'IF/currloop/idq_Controller/PID Controller1/I Gain/External Parameters'
 * '<S50>'  : 'IF/currloop/idq_Controller/PID Controller1/Ideal P Gain/Passthrough'
 * '<S51>'  : 'IF/currloop/idq_Controller/PID Controller1/Ideal P Gain Fdbk/Disabled'
 * '<S52>'  : 'IF/currloop/idq_Controller/PID Controller1/Integrator/Discrete'
 * '<S53>'  : 'IF/currloop/idq_Controller/PID Controller1/Integrator ICs/Internal IC'
 * '<S54>'  : 'IF/currloop/idq_Controller/PID Controller1/N Copy/Disabled wSignal Specification'
 * '<S55>'  : 'IF/currloop/idq_Controller/PID Controller1/N Gain/Disabled'
 * '<S56>'  : 'IF/currloop/idq_Controller/PID Controller1/P Copy/Disabled'
 * '<S57>'  : 'IF/currloop/idq_Controller/PID Controller1/Parallel P Gain/External Parameters'
 * '<S58>'  : 'IF/currloop/idq_Controller/PID Controller1/Reset Signal/Disabled'
 * '<S59>'  : 'IF/currloop/idq_Controller/PID Controller1/Saturation/Enabled'
 * '<S60>'  : 'IF/currloop/idq_Controller/PID Controller1/Saturation Fdbk/Disabled'
 * '<S61>'  : 'IF/currloop/idq_Controller/PID Controller1/Sum/Sum_PI'
 * '<S62>'  : 'IF/currloop/idq_Controller/PID Controller1/Sum Fdbk/Disabled'
 * '<S63>'  : 'IF/currloop/idq_Controller/PID Controller1/Tracking Mode/Disabled'
 * '<S64>'  : 'IF/currloop/idq_Controller/PID Controller1/Tracking Mode Sum/Passthrough'
 * '<S65>'  : 'IF/currloop/idq_Controller/PID Controller1/Tsamp - Integral/TsSignalSpecification'
 * '<S66>'  : 'IF/currloop/idq_Controller/PID Controller1/Tsamp - Ngain/Passthrough'
 * '<S67>'  : 'IF/currloop/idq_Controller/PID Controller1/postSat Signal/Forward_Path'
 * '<S68>'  : 'IF/currloop/idq_Controller/PID Controller1/preInt Signal/Internal PreInt'
 * '<S69>'  : 'IF/currloop/idq_Controller/PID Controller1/preSat Signal/Forward_Path'
 * '<S70>'  : 'IF/currloop/idq_Controller/PID Controller2/Anti-windup'
 * '<S71>'  : 'IF/currloop/idq_Controller/PID Controller2/D Gain'
 * '<S72>'  : 'IF/currloop/idq_Controller/PID Controller2/External Derivative'
 * '<S73>'  : 'IF/currloop/idq_Controller/PID Controller2/Filter'
 * '<S74>'  : 'IF/currloop/idq_Controller/PID Controller2/Filter ICs'
 * '<S75>'  : 'IF/currloop/idq_Controller/PID Controller2/I Gain'
 * '<S76>'  : 'IF/currloop/idq_Controller/PID Controller2/Ideal P Gain'
 * '<S77>'  : 'IF/currloop/idq_Controller/PID Controller2/Ideal P Gain Fdbk'
 * '<S78>'  : 'IF/currloop/idq_Controller/PID Controller2/Integrator'
 * '<S79>'  : 'IF/currloop/idq_Controller/PID Controller2/Integrator ICs'
 * '<S80>'  : 'IF/currloop/idq_Controller/PID Controller2/N Copy'
 * '<S81>'  : 'IF/currloop/idq_Controller/PID Controller2/N Gain'
 * '<S82>'  : 'IF/currloop/idq_Controller/PID Controller2/P Copy'
 * '<S83>'  : 'IF/currloop/idq_Controller/PID Controller2/Parallel P Gain'
 * '<S84>'  : 'IF/currloop/idq_Controller/PID Controller2/Reset Signal'
 * '<S85>'  : 'IF/currloop/idq_Controller/PID Controller2/Saturation'
 * '<S86>'  : 'IF/currloop/idq_Controller/PID Controller2/Saturation Fdbk'
 * '<S87>'  : 'IF/currloop/idq_Controller/PID Controller2/Sum'
 * '<S88>'  : 'IF/currloop/idq_Controller/PID Controller2/Sum Fdbk'
 * '<S89>'  : 'IF/currloop/idq_Controller/PID Controller2/Tracking Mode'
 * '<S90>'  : 'IF/currloop/idq_Controller/PID Controller2/Tracking Mode Sum'
 * '<S91>'  : 'IF/currloop/idq_Controller/PID Controller2/Tsamp - Integral'
 * '<S92>'  : 'IF/currloop/idq_Controller/PID Controller2/Tsamp - Ngain'
 * '<S93>'  : 'IF/currloop/idq_Controller/PID Controller2/postSat Signal'
 * '<S94>'  : 'IF/currloop/idq_Controller/PID Controller2/preInt Signal'
 * '<S95>'  : 'IF/currloop/idq_Controller/PID Controller2/preSat Signal'
 * '<S96>'  : 'IF/currloop/idq_Controller/PID Controller2/Anti-windup/Disc. Clamping Parallel'
 * '<S97>'  : 'IF/currloop/idq_Controller/PID Controller2/Anti-windup/Disc. Clamping Parallel/Dead Zone'
 * '<S98>'  : 'IF/currloop/idq_Controller/PID Controller2/Anti-windup/Disc. Clamping Parallel/Dead Zone/Enabled'
 * '<S99>'  : 'IF/currloop/idq_Controller/PID Controller2/D Gain/Disabled'
 * '<S100>' : 'IF/currloop/idq_Controller/PID Controller2/External Derivative/Disabled'
 * '<S101>' : 'IF/currloop/idq_Controller/PID Controller2/Filter/Disabled'
 * '<S102>' : 'IF/currloop/idq_Controller/PID Controller2/Filter ICs/Disabled'
 * '<S103>' : 'IF/currloop/idq_Controller/PID Controller2/I Gain/External Parameters'
 * '<S104>' : 'IF/currloop/idq_Controller/PID Controller2/Ideal P Gain/Passthrough'
 * '<S105>' : 'IF/currloop/idq_Controller/PID Controller2/Ideal P Gain Fdbk/Disabled'
 * '<S106>' : 'IF/currloop/idq_Controller/PID Controller2/Integrator/Discrete'
 * '<S107>' : 'IF/currloop/idq_Controller/PID Controller2/Integrator ICs/Internal IC'
 * '<S108>' : 'IF/currloop/idq_Controller/PID Controller2/N Copy/Disabled wSignal Specification'
 * '<S109>' : 'IF/currloop/idq_Controller/PID Controller2/N Gain/Disabled'
 * '<S110>' : 'IF/currloop/idq_Controller/PID Controller2/P Copy/Disabled'
 * '<S111>' : 'IF/currloop/idq_Controller/PID Controller2/Parallel P Gain/External Parameters'
 * '<S112>' : 'IF/currloop/idq_Controller/PID Controller2/Reset Signal/Disabled'
 * '<S113>' : 'IF/currloop/idq_Controller/PID Controller2/Saturation/Enabled'
 * '<S114>' : 'IF/currloop/idq_Controller/PID Controller2/Saturation Fdbk/Disabled'
 * '<S115>' : 'IF/currloop/idq_Controller/PID Controller2/Sum/Sum_PI'
 * '<S116>' : 'IF/currloop/idq_Controller/PID Controller2/Sum Fdbk/Disabled'
 * '<S117>' : 'IF/currloop/idq_Controller/PID Controller2/Tracking Mode/Disabled'
 * '<S118>' : 'IF/currloop/idq_Controller/PID Controller2/Tracking Mode Sum/Passthrough'
 * '<S119>' : 'IF/currloop/idq_Controller/PID Controller2/Tsamp - Integral/TsSignalSpecification'
 * '<S120>' : 'IF/currloop/idq_Controller/PID Controller2/Tsamp - Ngain/Passthrough'
 * '<S121>' : 'IF/currloop/idq_Controller/PID Controller2/postSat Signal/Forward_Path'
 * '<S122>' : 'IF/currloop/idq_Controller/PID Controller2/preInt Signal/Internal PreInt'
 * '<S123>' : 'IF/currloop/idq_Controller/PID Controller2/preSat Signal/Forward_Path'
 */
#endif                                 /* currloop0_h_ */

/*
 * File trailer for generated code.
 *
 * [EOF]
 */
