/**
  ******************************************************************************
  * @file    app_key_rtos.c
  * @brief   按键模块的 RT-Thread 适配：扫描线程 + 监听器 + 事件泵
  *          本文件是唯一直接使用 RTOS API 的地方；
  *          检测/消抖/广播的核心在纯 C 的 app_key.c。
  ******************************************************************************
  */

#include <rtthread.h>
#include "app_key_rtos.h"

/* ---- 扫描线程参数（集中在这一处，移植时按需调整） ---------------------- */
#define KEY_SCAN_THREAD_PRIO   20      /* 优先级：数字越大越低，main 线程通常为 10 */
#define KEY_SCAN_THREAD_TICK   10      /* 时间片(tick) */
#define KEY_SCAN_STACK_SIZE    512     /* 扫描线程栈(字节)：只扫描+广播，不做业务 */
#define KEY_SCAN_PERIOD_MS     (1000U / FLEX_BTN_SCAN_FREQ_HZ)  /* 默认 20ms */

static rt_thread_t s_scan_thread = RT_NULL;  /* 扫描线程句柄（动态创建，栈由 RT-Thread 堆分配） */
static uint8_t     s_started = 0u;

/**
  * @brief  扫描线程：固定周期调用纯 C 的 app_key_scan()
  * @note   扫描到事件后，纯 C 核心会把事件广播给所有监听器。
  */
static void key_scan_entry(void *parameter)
{
    (void)parameter;

    while (1)
    {
        app_key_scan();
        rt_thread_mdelay(KEY_SCAN_PERIOD_MS);
    }
}

/**
  * @brief  全局初始化（对外 API）：纯 C 初始化 + 启动扫描线程
  */
void app_key_begin(void)
{
    if (s_started != 0u)
    {
        return;
    }

    app_key_init();   /* 纯 C：GPIO + 按键注册 */

    /* 动态创建：控制块和栈(KEY_SCAN_STACK_SIZE 字节)都从 RT-Thread 堆分配，
     * 无需自己定义静态数组；失败返回 RT_NULL（一般是堆不够）。 */
    s_scan_thread = rt_thread_create("key_scan",
                                     key_scan_entry,
                                     RT_NULL,
                                     KEY_SCAN_STACK_SIZE,
                                     KEY_SCAN_THREAD_PRIO,
                                     KEY_SCAN_THREAD_TICK);
    if (s_scan_thread != RT_NULL)
    {
        rt_thread_startup(s_scan_thread);
        s_started = 1u;
    }
}

/**
  * @brief  纯 C 核心的事件到达通知钩子：释放该监听器的信号量
  */
static void watcher_notify(void *user)
{
    app_key_watcher_t *w = (app_key_watcher_t *)user;

    rt_sem_release(&w->sem);
}

/**
  * @brief  初始化监听器（对外 API）
  * @note   清空回调表、初始化信号量、把该监听器加入纯 C 订阅名单。
  */
void app_key_watcher_init(app_key_watcher_t *w)
{
    if (w == RT_NULL)
    {
        return;
    }

    rt_memset(w, 0, sizeof(*w));
    rt_sem_init(&w->sem, "key_w", 0, RT_IPC_FLAG_FIFO);
    app_key_subscribe(&w->sub, watcher_notify, w);
}

/**
  * @brief  注册"键+事件→回调"（对外 API）
  */
void app_key_on(app_key_watcher_t *w,
                app_key_id_t key,
                flex_button_event_t event,
                app_key_cb_t cb)
{
    uint8_t i;

    if ((w == RT_NULL) || (cb == RT_NULL))
    {
        return;
    }

    for (i = 0; i < APP_KEY_BIND_MAX; i++)
    {
        if (w->binds[i].cb == RT_NULL)
        {
            w->binds[i].key   = (uint8_t)key;
            w->binds[i].event = (uint8_t)event;
            w->binds[i].cb    = cb;
            return;
        }
    }
}

/**
  * @brief  事件分发：执行监听器 w 上所有与 (key,event) 匹配的回调
  * @note   支持通配（任意键/任意事件）；在调用 pump 的线程里执行。
  */
static void key_dispatch(app_key_watcher_t *w,
                         app_key_id_t key,
                         flex_button_event_t event)
{
    uint8_t i;
    uint8_t k = (uint8_t)key;
    uint8_t e = (uint8_t)event;

    for (i = 0; i < APP_KEY_BIND_MAX; i++)
    {
        app_key_cb_t cb = w->binds[i].cb;
        if (cb == RT_NULL)
        {
            continue;
        }
        if (((w->binds[i].key == k) || (w->binds[i].key == (uint8_t)APP_KEY_ANY_ID)) &&
            ((w->binds[i].event == e) || (w->binds[i].event == (uint8_t)APP_KEY_ANY_EVENT)))
        {
            cb(key, event);
        }
    }
}

/**
  * @brief  事件泵（对外 API）：取一个事件并执行匹配回调
  * @param  w: 本线程的监听器；timeout: 阻塞/非阻塞（见头文件说明）。
  */
void app_key_pump(app_key_watcher_t *w, rt_int32_t timeout)
{
    app_key_id_t        key;
    flex_button_event_t event;

    if (w == RT_NULL)
    {
        return;
    }

    if (rt_sem_take(&w->sem, timeout) == RT_EOK)
    {
        if (app_key_get(&w->sub, &key, &event))
        {
            key_dispatch(w, key, event);
        }
    }
}
