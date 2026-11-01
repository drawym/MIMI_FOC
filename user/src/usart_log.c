/**
  ******************************************************************************
  * @file    usart_log.c
  * @brief   UART4 串口驱动 + 日志打印（AT32F435RGT7，PH2=RX / PH3=TX）
  *
 *  一、硬件依据（已全部对照本板原理图与网表核实）
 *    1) AT32F435/437 参考手册 Table 6-8（Port H 复用表）：
 *         PH2：MUX8 = UART4_RX   （其余可用功能为 TMR5_CH1 / I2C2_SCL / QSPI1_IO0）
 *         PH3：MUX8 = UART4_TX   （其余可用功能为 TMR5_CH2 / I2C2_SDA / QSPI1_IO1）
 *       这两个引脚与 UART4 的收发方向是【硬绑定】的，PH2 只能收、PH3 只能发。
 *    2) 原理图 U17（AT32F435RGT7，LQFP64）符号：47 脚 = PH2，31 脚 = PH3。
 *    3) 网表连接：
 *         PH3(31) —— R24 100Ω —— KT6368A.7（模块 RX）
 *         PH2(47) —— R20 100Ω —— KT6368A.8（模块 TX）
 *       两条网络上都没有任何对地电容，接线是正确交叉的。
 *    4) KT6368A 出厂默认 UART 为 115200-8-N-1，与本驱动默认值一致。
  *
  *  二、实现要点
  *    1) 发送：轮询 TDBE 阻塞发送，简单可靠、不占用中断；发送完毕等 TDC 置位。
  *    2) 接收：RDBF 中断 + 环形缓冲（单生产者=中断 / 单消费者=线程，无需临界区）。
  *    3) 打印：vsnprintf 格式化到静态缓冲后整段发出；用忙标志做"重入保护"，
  *       忙碌时直接丢弃本次日志，保证在 10kHz 控制中断里调用也不会长时间阻塞。
  *    4) 不依赖 RT-Thread，裸机 / RTOS 环境均可使用。
  *
  *  三、性能参考
  *    115200 bps → 约 11.5 字节/ms。打印 40 字符约占 3.5ms，
  *    因此【禁止】在 10kHz（100us）控制中断里按周期打印，仅用于调试开关。
  ******************************************************************************
  */

#include "usart_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---- 接收环形缓冲：head 由中断写、tail 由用户读 -------------------------- */
static volatile uint8_t  s_rx_buffer[USART_LOG_RX_BUF_SIZE];
static volatile uint16_t s_rx_head = 0U;   /* 写入位置（中断内更新） */
static volatile uint16_t s_rx_tail = 0U;   /* 读出位置（线程内更新） */

/* ---- 打印缓冲与重入标志 -------------------------------------------------- */
static char             s_log_buffer[USART_LOG_TX_BUF_SIZE];
static volatile uint8_t s_log_busy = 0U;   /* 0=空闲  1=正在格式化/发送 */

/* 初始化完成标志：为 0 时所有发送接口直接返回。
 * 作用：万一调用方忘记调 usart_log_init()，UART4 时钟没开、寄存器读回 0，
 * 标志位永远等不到，发送函数会变成死循环把整个线程卡死（曾发生）。
 * 有了这道闸门，忘记初始化只会"没有输出"，不会拖死系统。 */
static volatile uint8_t s_initialized = 0U;

/* 等待标志的超时次数（纯计数，不依赖系统 tick）。
 * 115200 下单字节的 TDBE/TDC 最长约 87us，288MHz 下此值约 1.7ms，
 * 留了 20 倍裕量；即使超时也只是丢一个字节，绝不阻塞调用者。 */
#define USART_LOG_WAIT_TIMEOUT   100000U

/**
  * @brief  等待 UART4 的某个标志置位（带超时保护）
  * @param  flag: 例如 USART_TDBE_FLAG / USART_TDC_FLAG
  * @retval 1=等到了  0=超时（外设异常或未初始化）
  */
static uint8_t usart_wait_flag(uint32_t flag)
{
  uint32_t retry = USART_LOG_WAIT_TIMEOUT;

  while (usart_flag_get(USART_LOG_UART, flag) == RESET)
  {
    if (--retry == 0U)
    {
      return 0U;
    }
  }

  return 1U;
}

/**
  * @brief  初始化 UART4：PH2=RX、PH3=TX、开接收中断
  * @param  baudrate: 波特率，传 0 使用默认 115200
  * @retval none
  */
void usart_log_init(uint32_t baudrate)
{
  gpio_init_type gpio_init_struct;

  if (baudrate == 0U)
  {
    baudrate = USART_LOG_DEFAULT_BAUD;
  }

  /* ---- 1. 时钟：GPIOH + UART4（UART4 挂在 APB1，144MHz） ---- */
  crm_periph_clock_enable(CRM_GPIOH_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_UART4_PERIPH_CLOCK, TRUE);

  /* ---- 2. 复用编号：PH2/PH3 均选 MUX8（UART4_RX / UART4_TX） ---- */
  gpio_pin_mux_config(USART_LOG_GPIO, GPIO_PINS_SOURCE2, USART_LOG_MUX);  /* PH2 */
  gpio_pin_mux_config(USART_LOG_GPIO, GPIO_PINS_SOURCE3, USART_LOG_MUX);  /* PH3 */

  gpio_default_para_init(&gpio_init_struct);

  /* ---- 3. TX 引脚：复用推挽输出，强驱动 ---- */
  gpio_init_struct.gpio_pins           = USART_LOG_TX_PIN;
  gpio_init_struct.gpio_mode           = GPIO_MODE_MUX;
  gpio_init_struct.gpio_out_type       = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull           = GPIO_PULL_NONE;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(USART_LOG_GPIO, &gpio_init_struct);

  /* ---- 4. RX 引脚：复用输入 + 上拉（悬空时保持空闲高电平，避免误起帧） ---- */
  gpio_init_struct.gpio_pins           = USART_LOG_RX_PIN;
  gpio_init_struct.gpio_mode           = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pull           = GPIO_PULL_UP;
  gpio_init(USART_LOG_GPIO, &gpio_init_struct);

  /* ---- 5. UART4 参数：8-N-1，库函数内部按 APB1 时钟自动算 BRR ---- */
  usart_init(USART_LOG_UART, baudrate, USART_DATA_8BITS, USART_STOP_1_BIT);

  /* ---- 6. 使能收发器 + 串口模块 ----
   * ⚠ 关键：AT32 固件库的 usart_init() 只配置波特率/数据位/停止位，
   *   【不会】置位 UEN / TDBEEN / RDBFEN，必须在这里单独使能。
   *   漏掉这三行 → 模块不工作 → 数据写进 dt 寄存器但引脚上没有任何波形，
   *   现象就是"程序在跑，串口一个字节都发不出来"。 */
  usart_transmitter_enable(USART_LOG_UART, TRUE);   /* 发送使能：TDBEEN */
  usart_receiver_enable(USART_LOG_UART, TRUE);      /* 接收使能：RDBFEN */
  usart_enable(USART_LOG_UART, TRUE);               /* 模块使能：UEN     */

  /* ---- 7. 开接收中断：优先级 6（低于 PWM/ADC 的 2，避免抢控制环） ---- */
  usart_interrupt_enable(USART_LOG_UART, USART_RDBF_INT, TRUE);
  nvic_irq_enable(UART4_IRQn, 6, 0);

  /* ---- 8. 置就绪标志：此后发送接口才允许访问 UART4 ---- */
  s_initialized = 1U;
}

/**
  * @brief  运行时修改波特率
  * @param  baudrate: 新波特率（0 则忽略）
  * @note   usart_init() 只重算 baudr 与数据格式位，不会清 UEN/TEN/REN，
  *         因此无需重新使能、也不会影响引脚复用配置。
  * @retval none
  */
void usart_log_set_baud(uint32_t baudrate)
{
  if ((s_initialized == 0U) || (baudrate == 0U))
  {
    return;
  }

  /* 等上一字节彻底发完再改分频，避免把正在移出的帧截断 */
  (void)usart_wait_flag(USART_TDC_FLAG);

  usart_init(USART_LOG_UART, baudrate, USART_DATA_8BITS, USART_STOP_1_BIT);
}

/**
  * @brief  发送单个字节（阻塞等待 TDBE）
  * @param  ch: 待发送字节
  * @retval none
  */
void usart_log_putc(uint8_t ch)
{
  /* 未初始化：静默丢弃。绝不访问未开时钟的外设，否则读取标志恒为 0，
   * 下面的等待会变成死循环（这正是之前 main 线程卡死的原因）。 */
  if (s_initialized == 0U)
  {
    return;
  }

  /* 等发送缓冲空；超时则丢弃本字节，保证调用者一定返回 */
  if (usart_wait_flag(USART_TDBE_FLAG) == 0U)
  {
    return;
  }

  usart_data_transmit(USART_LOG_UART, ch);
}

/**
  * @brief  发送一段原始数据，返回前保证最后一个字节已移出移位寄存器
  * @param  data: 数据首地址（NULL 直接返回）
  * @param  len : 字节数
  * @retval none
  */
void usart_log_write(const uint8_t *data, uint16_t len)
{
  uint16_t index;

  if ((s_initialized == 0U) || (data == NULL))
  {
    return;
  }

  for (index = 0U; index < len; index++)
  {
    usart_log_putc(data[index]);
  }

  /* 等最后一字节完全发送出去（TDC），带超时：即使外设异常也不会僵死 */
  (void)usart_wait_flag(USART_TDC_FLAG);
}

/**
  * @brief  格式化打印（printf 风格）
  * @param  fmt: 格式串
  * @note   使用 GCC 原子交换获取忙标志：竞争失败直接丢弃，绝不阻塞等待。
  *         这样即使在中断里误用也不会死锁，只会丢一行日志。
  * @retval none
  */
void usart_log_printf(const char *fmt, ...)
{
  va_list args;
  int     length;

  if (fmt == NULL)
  {
    return;
  }

  /* 重入保护：0 → 1 成功表示拿到"发送权"，否则说明上一次还没发完 */
  if (__atomic_exchange_n(&s_log_busy, 1U, __ATOMIC_ACQUIRE) != 0U)
  {
    return;
  }

  va_start(args, fmt);
  length = vsnprintf(s_log_buffer, sizeof(s_log_buffer), fmt, args);
  va_end(args);

  if (length > 0)
  {
    /* vsnprintf 返回"本应写入的长度"，可能大于缓冲，需按实际可用长度截断 */
    if (length > (int)(sizeof(s_log_buffer) - 1U))
    {
      length = (int)(sizeof(s_log_buffer) - 1U);
    }
    usart_log_write((const uint8_t *)s_log_buffer, (uint16_t)length);
  }

  __atomic_store_n(&s_log_busy, 0U, __ATOMIC_RELEASE);
}

/**
  * @brief  从接收环形缓冲取 1 字节（非阻塞）
  * @param  ch: 输出缓冲
  * @retval 1=取到数据  0=无数据
  */
int usart_log_getc(uint8_t *ch)
{
  if ((ch == NULL) || (s_rx_tail == s_rx_head))
  {
    return 0;
  }

  *ch       = s_rx_buffer[s_rx_tail];
  s_rx_tail = (uint16_t)((s_rx_tail + 1U) & (USART_LOG_RX_BUF_SIZE - 1U));
  return 1;
}

/**
  * @brief  查询接收缓冲中待读字节数
  * @retval 可读字节数（0 ~ USART_LOG_RX_BUF_SIZE-1）
  */
uint16_t usart_log_rx_available(void)
{
  return (uint16_t)((s_rx_head - s_rx_tail) & (USART_LOG_RX_BUF_SIZE - 1U));
}

/**
  * @brief  UART4 中断服务：收字节入环形缓冲
  * @note   先取数据再清错误标志：清除溢出标志的库函数内部会读 dt 寄存器，
  *         顺序反了会把刚到的一个字节一起清掉。
  * @retval none
  */
void UART4_IRQHandler(void)
{
  uint8_t  rx_byte;
  uint16_t next_head;

  if (usart_flag_get(USART_LOG_UART, USART_RDBF_FLAG) == SET)
  {
    rx_byte   = (uint8_t)usart_data_receive(USART_LOG_UART);
    next_head = (uint16_t)((s_rx_head + 1U) & (USART_LOG_RX_BUF_SIZE - 1U));

    /* 缓冲满则丢弃新字节，保留已有数据（宁可丢新，不覆盖未读的旧数据） */
    if (next_head != s_rx_tail)
    {
      s_rx_buffer[s_rx_head] = rx_byte;
      s_rx_head              = next_head;
    }
  }

  /* 溢出/帧错误：清标志，防止反复进中断 */
  if (usart_flag_get(USART_LOG_UART, USART_ROERR_FLAG) == SET)
  {
    usart_flag_clear(USART_LOG_UART, USART_ROERR_FLAG);
  }
}
