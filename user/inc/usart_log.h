/**
  ******************************************************************************
  * @file    usart_log.h
  * @brief   UART4 串口驱动 + 日志打印接口（AT32F435RGT7）
  *
 *  硬件连接（AT32F435RGT7 / LQFP64，已对照本板网表 Netlist_底板_2026-10-31.tel 核对）：
 *    ------------------------------------------------------------------
 *    MCU 引脚   芯片脚号   IOMUX 复用        实际接到
 *    ------------------------------------------------------------------
 *    PH2        47         MUX8 = UART4_RX   模块 8 脚（模块 TX，经 R20 100Ω）
 *    PH3        31         MUX8 = UART4_TX   模块 7 脚（模块 RX，经 R24 100Ω）
 *    ------------------------------------------------------------------
 *    蓝牙芯片为 KT6368A：7 脚是它的 RX、8 脚是它的 TX，
 *    与 PH3=TX / PH2=RX 正好交叉，板上连线是正确的，不需要飞线。
  *
  *  注意：PH2/PH3 在 AT32F435 的 IOMUX 中【只能】是 UART4_RX / UART4_TX
  *        （PH2 无 TX 功能，PH3 无 RX 功能），不存在"交换"配置的可能。
  *
  *  默认参数：115200 bps，8 数据位，1 停止位，无校验，无流控。
  *
  *  用法：
  *    main() 里调用一次：usart_log_init(115200U);
  *    打印：USART_LOG("hello\r\n");
  *          USART_LOG("value = %.2f\r\n", 3.14159f);
  *          USART_LOG("a=%d b=%u c=0x%04X\r\n", a, b, c);
  *    接收：uint8_t ch; if (usart_log_getc(&ch)) { ... }
  ******************************************************************************
  */

#ifndef USART_LOG_H
#define USART_LOG_H

#include "at32f435_437.h"

/* ---- 硬件映射（换板子/换串口时只改这里） --------------------------------- */
#define USART_LOG_UART          UART4           /* PH2/PH3 只能复用为 UART4        */
#define USART_LOG_GPIO          GPIOH           /* 端口 H                          */
#define USART_LOG_RX_PIN        GPIO_PINS_2     /* PH2 → UART4_RX（MUX8）          */
#define USART_LOG_TX_PIN        GPIO_PINS_3     /* PH3 → UART4_TX（MUX8）          */
#define USART_LOG_MUX           GPIO_MUX_8      /* 参考手册 Table 6-8 Port H 复用表 */

/* 接收环形缓冲大小（必须是 2 的幂，便于用位与取模） */
#define USART_LOG_RX_BUF_SIZE   256U

/* 单次格式化输出的最大长度（超出部分被截断，不会越界） */
#define USART_LOG_TX_BUF_SIZE   256U

/* 默认波特率 */
#define USART_LOG_DEFAULT_BAUD  115200U

/* ---- 对外的打印宏 -------------------------------------------------------- */
/* 直接当 printf 用；带编译期格式检查（若参数类型与格式串不匹配会告警） */
#define USART_LOG(...)          usart_log_printf(__VA_ARGS__)

/**
  * @brief  初始化 UART4：PH2=RX / PH3=TX，开接收中断
  * @param  baudrate: 波特率（传 0 则使用 USART_LOG_DEFAULT_BAUD）
  * @note   APB1 时钟 144MHz 下 115200 的分频误差为 0。
  *         main() 里调用一次即可，其它外设初始化前后均可。
  * @retval none
  */
void usart_log_init(uint32_t baudrate);

/**
  * @brief  运行时修改波特率（UEN/TEN 保持使能，不会打断正在发送的数据之外的东西）
  * @param  baudrate: 新波特率，例如 9600 / 115200
  * @note   KT6368A 出厂默认 115200；若用 AT+CTxx 改过模块波特率，
  *         可用本函数让 MCU 侧跟随，无需重新初始化整个串口。
  * @retval none
  */
void usart_log_set_baud(uint32_t baudrate);

/**
  * @brief  格式化打印（内部使用 vsnprintf，支持 %d/%u/%x/%s/%c/%f/%.Nf 等）
  * @param  fmt: 格式串，用法同 printf
  * @note   非阻塞设计：若上一次打印尚未发送完，本次打印会被直接丢弃，
  *         以保证在中断中调用也不会死等、不会破坏 PWM 时序。
  *         输出长度上限 USART_LOG_TX_BUF_SIZE-1 字节，超出截断。
  * @retval none
  */
void usart_log_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/**
  * @brief  发送一段原始字节（阻塞直到全部写入发送寄存器并发送完成）
  * @param  data: 数据首地址；len: 字节数
  * @retval none
  */
void usart_log_write(const uint8_t *data, uint16_t len);

/**
  * @brief  发送单个字节（阻塞等待发送缓冲空）
  * @param  ch: 待发送字节
  * @retval none
  */
void usart_log_putc(uint8_t ch);

/**
  * @brief  从接收缓冲取 1 字节（非阻塞）
  * @param  ch: 输出缓冲；取到数据时写入该字节
  * @retval 1=取到数据  0=缓冲为空
  */
int usart_log_getc(uint8_t *ch);

/**
  * @brief  查询接收缓冲中待读取的字节数
  * @retval 当前可读字节数
  */
uint16_t usart_log_rx_available(void);

#endif /* USART_LOG_H */
