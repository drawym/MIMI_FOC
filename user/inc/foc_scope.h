/**
  ******************************************************************************
  * @file    foc_scope.h
  * @brief   FOC 电流"示波器"通道：把 10kHz 采样值以二进制形式送给 J-Scope
  *
  *  一、为什么用二进制而不是 printf
  *    SEGGER_RTT_printf("%d,%d,%d\n", ...) 的耗时几乎全在整数转 ASCII 的格式化上，
  *    实测 50~200us。控制周期只有 100us，放进去必然把 PWM 更新推迟，
  *    电流波形立刻崩掉（这不是 RTT 慢，是格式化慢）。
  *    本模块只做 6 字节 memcpy，约 1~3us，占周期的 1~3%，可以放心放中断。
  *
  *  二、两种取数方式，二选一
  *    ① RTT 通道模式（FOC_SCOPE_USE_RTT = 1，默认）
  *       中断把 3 个 int16 写进 RTT 通道 1，J-Scope 选 RTT 数据源读走。
  *       速率高（可到几十 kHz），是看 10kHz 原始波形的正规做法。
  *       注意：通道 0 是终端（SEGGER_RTT_printf 在用），二进制数据必须用独立通道，
  *             否则 ASCII 和二进制混在一条流里，两边都解析不了。
  *
  *    ② 符号模式（FOC_SCOPE_USE_RTT = 0）
  *       只更新三个全局变量 g_foc_scope_iu/iv/iw，J-Scope 用 Elf/符号模式
  *       直接按地址轮询内存。目标是零成本（约 5ns），但采样率受 SWD 带宽限制，
  *       一般只能到几 kHz，且时间戳由 J-Scope 自己产生，不是严格的等间隔。
  *
  *  三、接线与前提
  *    - 必须有 J-Link（或兼容 RTT 的调试器），普通串口、蓝牙都做不了这件事；
  *    - RTT 模式需要 J-Scope（SEGGER 官方免费工具，RTT Viewer 只能看文本）；
  *    - 缓冲区模式必须是 NO_BLOCK_SKIP（本工程已是），
  *      BLOCK 模式在调试器停住时会让中断永久死等 → 整机卡死。
  ******************************************************************************
  */

#ifndef FOC_SCOPE_H
#define FOC_SCOPE_H

#include "at32f435_437.h"

/* 1 = 走 RTT 通道 1 送二进制（推荐，能到几十 kHz）
 * 0 = 只更新全局变量，交给 J-Scope 符号模式轮询 */
#ifndef FOC_SCOPE_USE_RTT
#define FOC_SCOPE_USE_RTT       1
#endif

/* 示波数据使用的 RTT 上行通道号。
 * 通道 0 已被终端（SEGGER_RTT_printf / RTT Viewer）占用，不要改回 0。 */
#ifndef FOC_SCOPE_RTT_CH
#define FOC_SCOPE_RTT_CH        1U
#endif

/* 示波通道缓冲区大小。
 * 10kHz × 6 字节 = 60kB/s，4096 字节约 68ms 的余量，
 * 足够吸收 J-Scope 的读取间隙；加大会多占 RAM（当前 4KB）。 */
#ifndef FOC_SCOPE_BUF_SIZE
#define FOC_SCOPE_BUF_SIZE      4096U
#endif

/* ---- 符号模式用的影子变量（供 J-Scope Elf 模式按地址读取） ---------------- */
extern volatile int16_t g_foc_scope_iu;   /* U 相电流 mA */
extern volatile int16_t g_foc_scope_iv;   /* V 相电流 mA */
extern volatile int16_t g_foc_scope_iw;   /* W 相电流 mA */

/**
  * @brief  初始化示波通道（main 里调用一次，必须在第一次 push 之前）
  * @retval none
  * @note   FOC_SCOPE_USE_RTT=0 时本函数只清影子变量，不配置 RTT。
  */
void foc_scope_init(void);

/**
  * @brief  推送一帧电流（10kHz 中点中断内调用）
  * @param  iu/iv/iw: 三相电流，单位 mA
  * @retval none
  * @note   执行时间约 1~3us（RTT 模式）或约 5ns（仅符号模式）。
  *         无除法、无格式化、不关中断，可安全放在 PWM 中断里。
  */
void foc_scope_push(int16_t iu, int16_t iv, int16_t iw);

#endif /* FOC_SCOPE_H */
