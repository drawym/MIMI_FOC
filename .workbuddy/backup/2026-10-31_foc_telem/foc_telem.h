/**
  ******************************************************************************
  * @file    foc_telem.h
  * @brief   FOC 电流遥测：中断里只存数，线程里做格式化和串口发送
  *
  *  为什么要拆成两半：
  *    TMR1 中点中断跑在 10kHz（100us 一次）。一次 USART_LOG("I %d,%d,%d mA\r\n")
  *    约 20 字节，115200 下要 1.7ms —— 是控制周期的 17 倍。直接在中断里打印，
  *    PWM 占空比会推迟好几个周期才更新，电流波形立刻崩掉。
  *
  *    所以中断侧只做 3 次 16 位写入（约 100ns），格式化、串口发送全部交给
  *    一个低优先级线程按固定周期取最新值。这样采样的实时性不受影响，
  *    串口慢也只是少打几行，绝不拖累控制环。
  *
 *  输出格式（VOFA+ / FireWater 协议，逗号分通道、换行结束帧）：
 *    123,-456,789\r\n      →  三个通道依次为 U/V/W 相电流（mA）
 *    VOFA+ 里选 FireWater、3 通道，即可直接出波形。
 *
 *  用法：
 *    main() 里 curr_adc_init() 之后调用一次 foc_telem_start(5U);    // 200Hz 输出
 *    中点中断里 curr_adc_sample() 之后调用 foc_telem_publish(...)；
 *    任意线程可用 foc_telem_read() 取最新一帧（无锁、无阻塞）。
  ******************************************************************************
  */

#ifndef FOC_TELEM_H
#define FOC_TELEM_H

#include "at32f435_437.h"

/* 遥测帧：三相电流（毫安，带符号）+ 发布序号 */
typedef struct
{
  int16_t  iu;        /* U 相电流 mA */
  int16_t  iv;        /* V 相电流 mA */
  int16_t  iw;        /* W 相电流 mA */
  uint32_t seq;       /* 每发布一次 +1，用于判断有没有新数据 */
} foc_telem_frame_t;

/**
  * @brief  发布一帧电流数据（供 10kHz 中点中断调用）
  * @param  iu/iv/iw: 三相电流，单位 mA
  * @note   内部用 seq-lock 保证线程读到的一定是完整的一帧，不会读到写了一半的数据。
  *         本函数只有几次存储，无除法、无格式化，执行时间约 0.1us。
  * @retval none
  */
void foc_telem_publish(int16_t iu, int16_t iv, int16_t iw);

/**
  * @brief  读取最新一帧（非阻塞，任意线程可调用）
  * @param  out: 输出帧
  * @retval 1=读取成功  0=读取期间一直被中断打断，本周期放弃
  */
int foc_telem_read(foc_telem_frame_t *out);

/**
  * @brief  启动遥测打印线程
  * @param  period_ms: 打印周期（ms），传 0 用默认 5ms（200Hz）
  * @note   周期越短，波形越细，但串口/蓝牙带宽消耗成正比增加。
  *         8Hz 电频率下的点数：2ms→62 点/周期，3ms→42，5ms→25，10ms→12。
  *         一帧 14 字节，5ms(200Hz) 占 115200 带宽约 24%，3ms 约 40%，2ms 约 61%。
  *         电机未运行时中断不采样，线程不会输出（避免刷屏）。
  * @retval none
  */
void foc_telem_start(uint32_t period_ms);

#endif /* FOC_TELEM_H */
