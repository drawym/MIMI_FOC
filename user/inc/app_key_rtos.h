#ifndef APP_KEY_RTOS_H
#define APP_KEY_RTOS_H

/* ============================================================================
 *  app_key_rtos.h —— 按键模块（RT-Thread 版）：监听器 + "键/事件→回调"
 *
 *  一个线程想收按键，就给它一个"监听器(watcher)"，然后 3 步：
 *
 *    1) main 里调一次 app_key_begin()                —— 启动 GPIO + 扫描线程
 *    2) 线程里 app_key_watcher_init(&w) 初始化自己的监听器
 *       然后 app_key_on(&w, 键, 事件, 回调)          —— 注册想响应的动作
 *    3) 线程的 while(1) 里反复调 app_key_pump(&w, RT_WAITING_NO)
 *
 *  每个线程一个监听器，各有独立事件队列和回调表；同一个按键事件会【广播】
 *  给所有监听器，所以线程 A、线程 B 可以对同一个按键各自做出反应。
 *
 *  例子（线程 A 只关心 KEY1，线程 B 关心 KEY1/KEY2）：
 *
 *    app_key_begin();                      // main 里一次
 *
 *    // 线程 A：
 *    app_key_watcher_init(&wA);
 *    app_key_on(&wA, APP_KEY_1, FLEX_BTN_PRESS_CLICK, a_fun);
 *    while (1) { app_key_pump(&wA, RT_WAITING_NO); rt_thread_mdelay(10); }
 *
 *    // 线程 B：
 *    app_key_watcher_init(&wB);
 *    app_key_on(&wB, APP_KEY_1, FLEX_BTN_PRESS_CLICK, b_run);
 *    app_key_on(&wB, APP_KEY_2, FLEX_BTN_PRESS_CLICK, b_stop);
 *    while (1) { app_key_pump(&wB, RT_WAITING_NO); rt_thread_mdelay(10); }
 *
 *  本文件是唯一直接使用 RTOS 的接口；底层消抖/扫描/广播在纯 C 的 app_key.c。
 * ==========================================================================*/

#include <rtthread.h>
#include "app_key.h"

/* 每个监听器最多注册多少条"键+事件→回调" */
#define APP_KEY_BIND_MAX   6u

/* 通配符（用于 app_key_on）：
 *   APP_KEY_ANY_ID    表示任意按键；
 *   APP_KEY_ANY_EVENT 表示任意事件。 */
#define APP_KEY_ANY_ID     ((app_key_id_t)0xFF)
#define APP_KEY_ANY_EVENT  ((flex_button_event_t)0xFF)

/* 按键事件回调函数类型：在"调用 app_key_pump 的线程"里执行。
 *   key  : 实际触发的按键编号；
 *   event: 实际触发的事件。 */
typedef void (*app_key_cb_t)(app_key_id_t key, flex_button_event_t event);

/* 按键监听器：哪个线程想收按键，就给它一个（通常定义为 static）。
 * 内部字段由 app_key_watcher_init 初始化，之后不要手动改。 */
typedef struct app_key_watcher
{
    app_key_sub_t       sub;   /* 纯 C 事件队列（底层） */
    struct rt_semaphore sem;   /* 事件到达信号量（底层） */
    struct                    /* 键+事件→回调 注册表 */
    {
        uint8_t      key;      /* 监听哪个键，或 APP_KEY_ANY_ID */
        uint8_t      event;    /* 监听哪个事件，或 APP_KEY_ANY_EVENT */
        app_key_cb_t cb;       /* 触发时执行谁；为 0 表示该槽位空闲 */
    } binds[APP_KEY_BIND_MAX];
} app_key_watcher_t;

/* 全局初始化：配置 GPIO + 启动扫描线程。main 里只调一次。 */
void app_key_begin(void);

/* 初始化一个监听器（在线程入口里，或 main 里提前调用）。 */
void app_key_watcher_init(app_key_watcher_t *w);

/* 注册一条规则：当监听器 w 收到"键 key + 事件 event"时，执行回调 cb。
 * 可对同一监听器重复调用，注册不同键/事件（最多 APP_KEY_BIND_MAX 条）。 */
void app_key_on(app_key_watcher_t *w,
                app_key_id_t key,
                flex_button_event_t event,
                app_key_cb_t cb);

/* 事件泵：在自己线程的 while(1) 里反复调。
 * 取出一个事件并执行监听器 w 上匹配的回调。
 *   timeout: RT_WAITING_NO(0) 非阻塞立即返回（推荐，配合 rt_thread_mdelay）；
 *            RT_WAITING_FOREVER(-1) 一直阻塞到有事件；
 *            或具体 tick（最多等多久，期间有事件立即返回）。 */
void app_key_pump(app_key_watcher_t *w, rt_int32_t timeout);

#endif /* APP_KEY_RTOS_H */
