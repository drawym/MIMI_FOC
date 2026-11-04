/**
  ******************************************************************************
  * @file    foc_scope.c
  * @brief   FOC 电流示波通道实现（10kHz 中断 → RTT 通道 1 → J-Scope）
  *
  *  数据格式（每帧 6 字节，小端）：
  *      offset 0~1 : int16  U 相电流 mA
  *      offset 2~3 : int16  V 相电流 mA
  *      offset 4~5 : int16  W 相电流 mA
  *    帧与帧之间无分隔符、无包头，靠"每帧固定 6 字节"切分。
  *    J-Scope 里配置 3 个通道、类型 int16（Signed 16 bit）即可对上。
  *    → 若把类型改成 float32，每帧变 12 字节，这里也要同步改，否则波形错位。
  ******************************************************************************
  */

#include "foc_scope.h"
#include "SEGGER_RTT.h"

/* ---- 符号模式影子变量：J-Scope 的 Elf/符号模式直接按地址读 ---------------- */
volatile int16_t g_foc_scope_iu = 0;
volatile int16_t g_foc_scope_iv = 0;
volatile int16_t g_foc_scope_iw = 0;

#if (FOC_SCOPE_USE_RTT != 0)
/* RTT 通道 1 的专用缓冲区。
 * 必须显式提供：RTT 库只给通道 0 准备了默认缓冲区，
 * 通道 1 若不指定 pBuffer，SEGGER_RTT_ConfigUpBuffer 会直接失败。 */
static uint8_t s_scope_rtt_buffer[FOC_SCOPE_BUF_SIZE];
#endif

/**
  * @brief  初始化示波通道
  * @retval none
  */
void foc_scope_init(void)
{
  g_foc_scope_iu = 0;
  g_foc_scope_iv = 0;
  g_foc_scope_iw = 0;

#if (FOC_SCOPE_USE_RTT != 0)
  /* 把通道 1 注册成 "Scope"，模式 NO_BLOCK_SKIP：
   * 缓冲区满时直接丢弃本次写入并返回，绝不阻塞 —— 这是能在中断里用的前提。
   * 若改成 BLOCK_IF_FIFO_FULL，调试器一旦停止读取，中断会永久卡死。 */
  SEGGER_RTT_ConfigUpBuffer(FOC_SCOPE_RTT_CH,
                            "Scope",
                            s_scope_rtt_buffer,
                            FOC_SCOPE_BUF_SIZE,
                            SEGGER_RTT_MODE_NO_BLOCK_SKIP);
#endif
}

/**
  * @brief  推送一帧三相电流（10kHz 中点中断内调用）
  * @param  iu/iv/iw: 三相电流 mA
  * @retval none
  */
void foc_scope_push(int16_t iu, int16_t iv, int16_t iw)
{
  /* 先写影子变量：给 J-Scope 符号模式用，3 条存储指令，约 5ns。
   * 三个变量不是原子更新，符号模式可能看到跨周期的组合（相位差 100us），
   * 对波形观测无影响；需要严格同周期请改用下面的 RTT 通道模式。 */
  g_foc_scope_iu = iu;
  g_foc_scope_iv = iv;
  g_foc_scope_iw = iw;

#if (FOC_SCOPE_USE_RTT != 0)
  {
    /* 8 字节整帧：2 字节同步头 + 3 个 int16（小端）。
     * 手工拼字节比 struct 更稳：不用操心编译器对齐/填充，
     * 也不会因为结构体在别的编译器上变大而导致帧长错位。 */
    uint8_t frame[FOC_SCOPE_FRAME_BYTES];

    frame[0] = FOC_SCOPE_SYNC0;
    frame[1] = FOC_SCOPE_SYNC1;
    frame[2] = (uint8_t)((uint16_t)iu & 0xFFU);   /* U 相低字节 */
    frame[3] = (uint8_t)((uint16_t)iu >> 8);      /* U 相高字节 */
    frame[4] = (uint8_t)((uint16_t)iv & 0xFFU);   /* V 相低字节 */
    frame[5] = (uint8_t)((uint16_t)iv >> 8);      /* V 相高字节 */
    frame[6] = (uint8_t)((uint16_t)iw & 0xFFU);   /* W 相低字节 */
    frame[7] = (uint8_t)((uint16_t)iw >> 8);      /* W 相高字节 */

    /* NoLock 版本：省掉 RTT 内部的临界区进出，约省 0.5us。
     * 安全性：中断只写通道 1，线程的 printf 只写通道 0，两者操作不同的
     * 缓冲区控制块，不会互相破坏。 */
    (void)SEGGER_RTT_WriteNoLock(FOC_SCOPE_RTT_CH, frame, FOC_SCOPE_FRAME_BYTES);
  }
#endif
}
