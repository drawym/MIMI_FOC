#ifndef APP_KEY_H
#define APP_KEY_H

/* ============================================================================
 *  app_key.h —— 按键"检测 + 发布订阅"纯 C 核心（不依赖任何 RTOS）
 *
 *  分层：
 *    本文件 + app_key.c 是【平台无关核心】：
 *      - 读电平、消抖、状态机（FlexibleButton 库）
 *      - 发布-订阅：每个订阅者一个独立的环形事件队列（纯 C）
 *      - 一次按键事件会广播给所有订阅者
 *    完全不含 RTOS 调用。裸机可直接用；RTOS 下由 app_key_rtos 层提供
 *    扫描线程和阻塞等待。
 * ==========================================================================*/

#include <stdint.h>
#include "flexible_button.h"   /* 只借它的事件枚举 flex_button_event_t（纯 C 库） */

/* 板载按键编号（与物理按键一一对应；按键数量在这里改） */
typedef enum
{
    APP_KEY_1 = 0,   /* PB4：启动 */
    APP_KEY_2,       /* PB5：暂停 */
    APP_KEY_3,       /* PB6：预留 */
    APP_KEY_NUM      /* 按键总数，仅用于遍历/数组大小，不是真实按键 */
} app_key_id_t;

/* 单个按键事件（纯 C，可放进任意队列，不依赖 OS） */
typedef struct
{
    uint8_t id;       /* 按键编号 app_key_id_t */
    uint8_t event;    /* 事件 flex_button_event_t */
} app_key_msg_t;

/* 每个订阅者的事件队列深度（必须是 2 的幂，便于用掩码回绕）。
 *
 * ⚠ 本宏同时决定 app_key_sub_t.q[] 的数组长度，【只能在这里定义一次】。
 *   任何 .c 文件都不得再 #define 覆盖它：否则结构体按旧值分配数组、
 *   掩码却按新值回绕，环形缓冲会越界写到 head/tail/notify 上（历史 bug）。
 * 可在 include 本头文件之前用 -DAPP_KEY_QUEUE_SIZE=xx 覆盖。 */
#ifndef APP_KEY_QUEUE_SIZE
#define APP_KEY_QUEUE_SIZE   64u
#endif
#define APP_KEY_QUEUE_MASK   (APP_KEY_QUEUE_SIZE - 1u)

/* 编译期校验：必须是 2 的幂，否则掩码回绕不成立 */
#if (APP_KEY_QUEUE_SIZE == 0u) || ((APP_KEY_QUEUE_SIZE & (APP_KEY_QUEUE_SIZE - 1u)) != 0u)
#error "APP_KEY_QUEUE_SIZE must be a power of two (e.g. 16 / 32 / 64)"
#endif

/* 事件到达通知钩子（平台无关）：
 * 纯 C 核心在"成功投递一个事件到某订阅者队列"后调用它。
 *  - RTOS 适配层：把它接到"释放信号量"，订阅线程即可阻塞唤醒；
 *  - 裸机：钩子置 NULL，主循环用 app_key_get() 非阻塞轮询。
 * user 为订阅时透传的指针（通常指向订阅者自己的 OS 对象）。 */
typedef void (*app_key_notify_fn)(void *user);

/* 订阅者句柄（由应用层静态分配，保证生命周期）。
 * 每个订阅者一个独立环形队列，因此事件是"广播"：
 * 一次按键，所有订阅者的队列都会收到一份，互不抢占。 */
typedef struct
{
    app_key_msg_t     q[APP_KEY_QUEUE_SIZE]; /* 事件环形缓冲 */
    volatile uint16_t head;                  /* 生产者(扫描端)写位置 */
    volatile uint16_t tail;                  /* 消费者(订阅者)读位置 */
    app_key_notify_fn notify;                /* 事件到达通知钩子(可 NULL) */
    void             *notify_user;           /* 透传给 notify 的用户数据 */
} app_key_sub_t;

/* 初始化按键：GPIO 配置 + 按键注册 + 订阅表清空。
 * 纯 C，不启动任何线程、不依赖 RTOS。 */
void app_key_init(void);

/* 扫描一次：读电平 → 消抖/状态机 → 产生事件 → 广播给所有订阅者。
 * 必须被以固定周期（1000/FLEX_BTN_SCAN_FREQ_HZ ms，默认 20ms）调用：
 *   RTOS：由一个线程周期调用（见 app_key_rtos）；
 *   裸机：在 main 循环或 SysTick 里周期调用。 */
void app_key_scan(void);

/* 订阅按键事件（广播）。
 * @param  sub        : 应用层静态分配的订阅者句柄；
 * @param  notify/user: 事件到达通知钩子及用户数据（裸机可传 NULL/NULL）。
 * @retval 返回 sub（成功）；订阅者已满或 sub 为 NULL 时返回 NULL。 */
app_key_sub_t *app_key_subscribe(app_key_sub_t *sub,
                                 app_key_notify_fn notify, void *user);

/* 取消订阅：之后不再收到事件。 */
void app_key_unsubscribe(app_key_sub_t *sub);

/* 非阻塞取一个事件（在订阅者自己的上下文调用）。
 * @param  id/event: 输出按键编号和事件（可传 NULL）。
 * @retval 1=取到一个事件（id/event 被填充）；0=队列空。 */
uint8_t app_key_get(app_key_sub_t *sub, app_key_id_t *id,
                    flex_button_event_t *event);

/* 查询订阅者队列中未读事件个数（0..APP_KEY_QUEUE_SIZE-1）。 */
uint8_t app_key_pending(app_key_sub_t *sub);

#endif /* APP_KEY_H */
