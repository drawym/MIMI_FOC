/**
  ******************************************************************************
  * @file    vf_start.h
  * @brief   V/F 开环启动阶段（在电流闭环之前使用）
  *
  *  一、为什么要有这个模块
  *    I/F 启动（电流闭环 + 频率开环）要求电流环带宽远高于基波频率，
  *    否则电流矢量跟不上给定，磁场被"拉扁、拖后"，转子就左右晃。
  *    V/F 是直接输出正弦电压，不需要任何反馈去追踪，鲁棒性高得多 ——
  *    这正是"以前调 VF 能慢慢转，现在 I/F 转不起来"的根本差别。
  *
  *  二、启动时序（一键启动后自动走完）
  *    0.0 ~ 0.5s   预定位：角度固定在 0°，通一个较小的直流矢量，
  *                 把转子拉到已知位置
  *    0.5 ~ 10.5s  开环升频：电频率从 1Hz 线性升到目标频率，
  *                 电压按 V = V_BOOST + K_VF×f 同步上升
  *    10.5s 之后   由 main 切到电流闭环（相位会交接过去）
  *
  *  三、电压怎么定
  *    相电压峰值 V = V_BOOST + K_VF × f(Hz)
  *      - V_BOOST 补偿低频时的电阻压降与逆变器死区，太低拖不动
  *      - K_VF 近似 V/f 恒定，保持磁通不随频率变化
  *    空载电流 ≈ (V − E)/R，E 是反电动势，7Hz 时只有 0.16V 左右，
  *    所以电压基本由 V_BOOST 决定，调它就能控制启动电流大小。
  ******************************************************************************
  */

#ifndef VF_START_H
#define VF_START_H

#include "at32f435_437.h"

/* ---- 时序参数（单位：拍，10kHz 下一拍 100us） ---------------------------- */
#define VF_ALIGN_STEPS      5000U     /* 预定位 0.5s：先把转子拉到 0° */
#define VF_RAMP_STEPS      100000U    /* 升频 10s：从起始频率线性升到目标频率 */

/* ---- 频率参数（单位：Hz，电频率） ---------------------------------------- */
#define VF_FREQ_START_HZ    1.0f      /* 起步电频率，越小越容易捕获转子 */
#define VF_FREQ_END_HZ      7.0f      /* 目标电频率：7Hz × 60 / 7 对极 = 60rpm 机械 */

/* ---- 电压参数（单位：V，相电压峰值） ------------------------------------- */
#define VF_V_BOOST          0.80f     /* 低频电压提升，决定启动电流大小 */
#define VF_K_VF             0.05f     /* V/f 斜率，V per Hz */

/* ---- SVPWM 输出标定（与 currloop0 的 PWM_HalfPeriod_Gain 必须一致） ------ */
#define VF_VBUS_VOLT        12.0f     /* 母线电压 */
#define VF_PWM_FULL         14400.0f  /* PWM 满量程计数 = ARR + 1 */

/**
  * @brief  开始 V/F 启动（按键启动时调用）
  * @note   会清零角度和计数器，从头开始预定位
  * @retval none
  */
void vf_start_begin(void);

/**
  * @brief  V/F 阶段单步执行（10kHz 中断里调用，一次算一拍的占空比）
  * @note   内部直接调用 tmr1_pwm_set_ccr() 输出三相，
  *         调用前请确保 s_motor_run 为 1。
  * @retval none
  */
void vf_start_step(void);

/**
  * @brief  查询 V/F 阶段是否已经跑完（可以切闭环了）
  * @retval 1=已完成  0=仍在进行
  */
uint8_t vf_start_finished(void);

/**
  * @brief  取当前 V/F 电角度（切闭环时交给闭环积分器，保证相位连续）
  * @retval 电角度，单位 rad，范围 0~2π
  */
float vf_start_get_theta(void);

/**
  * @brief  复位 V/F 阶段（停机时调用）
  * @retval none
  */
void vf_start_reset(void);

#endif /* VF_START_H */
