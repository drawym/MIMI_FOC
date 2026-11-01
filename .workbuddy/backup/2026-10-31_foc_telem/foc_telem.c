/**
  ******************************************************************************
  * @file    foc_telem.c
  * @brief   FOC 电流遥测实现（单生产者=10kHz 中断 / 单消费者=打印线程）
  *
  *  一、为什么要 seq-lock 而不是关中断
  *    中断每 100us 就会写一次这帧数据，线程读取时随时可能被打断。
  *    做法是发布方在写入前后各把 s_seq_lock 加一：
  *        写之前 -> 变成奇数（表示"正在写"）
  *        写之后 -> 变成偶数（表示"写完了"）
  *    读取方先记下 l1，拷贝完再读 l2，只有 l1 == l2 且 l1 为偶数才认为数据完整，
  *    否则重试。整个过程不需要关中断，也不影响中断响应。
  *
 *  二、输出格式（VOFA+ / FireWater 协议）
 *    每行形如：  123,-456,789\r\n
 *    （U、V、W 三相，单位 mA，零电流时应在 0 附近摆动）
 *    逗号分隔三个通道、换行结束一帧，VOFA+ 选 FireWater + 3 通道即可直接出波形。
 *
 *  三、串口负载估算（一帧 14 字节，字节时间 86.8us @115200 8N1）
 *    周期      速率     点/电周期(8Hz)   带宽占用   发送耗时/周期
 *    10ms     100Hz     12             12%        1.22/10  ms
 *     5ms     200Hz     25             24%        1.22/5   ms   ← 默认
 *     3ms     333Hz     42             40%        1.22/3   ms
 *     2ms     500Hz     62             61%        1.22/2   ms   ← 文本格式的上限附近
 *    再往上（>1kHz）必须换二进制帧，否则 115200 带宽和轮询发送的 CPU 开销都撑不住。
 *
 *  四、为什么用 rt_thread_delay_until 而不是 rt_thread_mdelay
 *    mdelay(n) 是在"发送完成之后"才开始计时的，实际周期 = n + 发送耗时（1.22ms），
 *    10ms 设定会跑成 11.2ms（89Hz），误差 11%，且随速率提高而恶化。
 *    delay_until 按"上一次唤醒时刻 + 周期"的绝对时刻调度，发送耗时被自动吸收，
 *    周期恒定为设定值；若某次严重超时，它会立刻返回并重置基准，不会追赶式突发输出。
  ******************************************************************************
  */

#include "foc_telem.h"
#include "usart_log.h"
#include "SEGGER_RTT.h"

#include <rtthread.h>

/* 默认打印周期：5ms → 200Hz（8Hz 电频率下约 25 点/电周期）
 * 想更细就改小：3→333Hz(42 点/周期)，2→500Hz(62 点/周期，带宽已占 61%)。
 * 若改成 2 之后波形出现"半截行"或周期性停顿，说明蓝牙侧吞吐跟不上了，退回 3 或 5。 */
#define FOC_TELEM_DEFAULT_MS    5U

/* 线程栈：vsnprintf 走 newlib，需要留足余量 */
#define FOC_TELEM_STACK_SIZE    1024U

/* 线程优先级：低于 main(10)、按键(10)，
 * 避免遥测打印抢占控制相关的线程 */
#define FOC_TELEM_PRIORITY      14U

/* ---- 中断与线程共享的一帧数据 -------------------------------------------- */
static volatile foc_telem_frame_t s_frame;
static volatile uint32_t          s_seq_lock = 0U;   /* 偶数=稳定，奇数=写入中 */

/* 发送周期，单位 tick（RT_TICK_PER_SECOND = 1000，所以 1 tick = 1ms） */
static rt_tick_t s_period_ticks = FOC_TELEM_DEFAULT_MS;

/**
  * @brief  发布一帧（10kHz 中断内调用）
  * @param  iu/iv/iw: 三相电流 mA
  * @retval none
  */
void foc_telem_publish(int16_t iu, int16_t iv, int16_t iw)
{
  s_seq_lock++;              /* 变奇数：告诉线程"正在写，别读" */

  s_frame.iu = iu;
  s_frame.iv = iv;
  s_frame.iw = iw;
  s_frame.seq++;

  s_seq_lock++;              /* 变偶数：写完，可以读了 */
}

/**
  * @brief  读取最新一帧
  * @param  out: 输出帧
  * @retval 1=成功  0=连续被中断打断，放弃本次
  */
int foc_telem_read(foc_telem_frame_t *out)
{
  uint32_t l1;
  uint32_t l2;
  uint32_t retry = 8U;

  if (out == 0)
  {
    return 0;
  }

  for (;;)
  {
    l1 = s_seq_lock;

    if ((l1 & 1U) == 0U)     /* 偶数：发布方当前不在写 */
    {
      out->iu  = s_frame.iu;
      out->iv  = s_frame.iv;
      out->iw  = s_frame.iw;
      out->seq = s_frame.seq;

      l2 = s_seq_lock;
      if (l1 == l2)          /* 拷贝期间没有被插入写，数据一致 */
      {
        return 1;
      }
    }

    if (--retry == 0U)
    {
      /* 理论上碰不到：拷贝只有几条指令，远小于 100us 的中断间隔 */
      return 0;
    }
  }
}

/**
  * @brief  遥测打印线程
  * @param  parameter: 未使用
  * @retval none
  */
static void foc_telem_thread_entry(void *parameter)
{
  foc_telem_frame_t frame;
  uint32_t          last_seq = 0U;
  rt_tick_t         next_wake;

  (void)parameter;

  /* 起始横幅：把实际生效的速率打出来，便于确认配置是否生效 */
  USART_LOG("\r\n-- FOC telemetry %u Hz, FireWater 3ch (KEY1 = run) --\r\n",
            (unsigned)(RT_TICK_PER_SECOND / s_period_ticks));

  next_wake = rt_tick_get();

  for (;;)
  {
    /* 取最新一帧；seq 未变说明没有新数据（电机停止时中断不发布），跳过不重复打印 */
    if ((foc_telem_read(&frame) != 0) && (frame.seq != last_seq))
    {
      last_seq = frame.seq;

      USART_LOG("%d,%d,%d\r\n",
                (int)frame.iu, (int)frame.iv, (int)frame.iw);
    }

    /* 绝对时刻调度：等"上次唤醒时刻 + 一个周期"。
     * 发送耗时（约 1.22ms）被自动吸收，周期不随速率漂移；
     * 若某次严重超时，内部会重置基准并立即返回，不会追赶式突发输出。 */
    rt_thread_delay_until(&next_wake, s_period_ticks);
  }
}

/**
  * @brief  启动遥测线程
  * @param  period_ms: 打印周期（ms），0 表示用默认值（5ms / 200Hz）
  * @retval none
  */
void foc_telem_start(uint32_t period_ms)
{
  rt_thread_t tid;

  if (period_ms != 0U)
  {
    /* 毫秒 → tick，向上取整；并保证至少 1 tick，避免 0 周期把线程变成忙等 */
    s_period_ticks = (rt_tick_t)(((period_ms * RT_TICK_PER_SECOND) + 999U) / 1000U);
    if (s_period_ticks == 0U)
    {
      s_period_ticks = 1U;
    }
  }

  tid = rt_thread_create("foc_tm", foc_telem_thread_entry, RT_NULL,
                         FOC_TELEM_STACK_SIZE, FOC_TELEM_PRIORITY, 10);
  if (tid != RT_NULL)
  {
    rt_thread_startup(tid);
  }
}
