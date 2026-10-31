/**
  ******************************************************************************
  * @file    app_key.c
  * @brief   按键"检测 + 发布订阅"纯 C 核心（基于开源库 FlexibleButton）
  *          —— 不依赖任何 RTOS，裸机/RTOS 都能用。
  *
  *          硬件：KEY1=PB4(启动)  KEY2=PB5(暂停)  KEY3=PB6(预留)
  *          接法：按键一端接引脚、一端接 GND，内部上拉
  *                → 未按下=高电平(1)，按下=低电平(0)
  *
  *  发布-订阅模型（广播）：
  *
  *            app_key_scan() 产生事件
  *                     │
  *                     ▼  app_key_publish()
  *        ┌────────────┼────────────┐
  *        ▼            ▼            ▼
  *   订阅者 A 队列   订阅者 B 队列  订阅者 C 队列      ← 每个订阅者一份
  *   (环形缓冲)      (环形缓冲)     (环形缓冲)
  *        │            │            │
  *     A 自己 get    B 自己 get    C 自己 get         ← 各自在自己的上下文取
  *
  *  文件组织（方便移植）：
  *    上半部分【移植配置区】换板子/换芯片时，只需要改这一区；
  *    下半部分【通用实现区】与硬件/RTOS 无关，正常移植时无需修改。
  *
  *  移植 checklist（按键数量在 app_key.h 的枚举里改）：
  *    [ ] 改引脚映射表 s_gpio（哪个键接哪个 GPIO）
  *    [ ] 改 app_key_gpio_init()（开时钟、上拉/下拉、输入模式）
  *    [ ] 确认 APP_KEY_PRESSED_LEVEL（按下接地=0，按下接VCC=1）
  *    [ ] 按需调整时间阈值、订阅者数量（多数情况用默认即可）
  *
  *  裸机用法：
  *    app_key_init();
  *    订阅时 notify 传 NULL；
  *    main 循环里每 20ms 调 app_key_scan()，
  *    再用 app_key_get() 非阻塞取事件处理。
  *  RTOS 用法：见 app_key_rtos.h（提供扫描线程 + 信号量阻塞等待）。
  ******************************************************************************
  */

#include "app_key.h"
#include "at32f435_437.h"     /* 仅芯片 GPIO 库，不引入 RTOS */

/* ############################################################################
 * ##  【移植配置区】 换板子/换芯片时，只需要修改本区域                         ##
 * ##########################################################################*/

/* ---- ① 按键判定时间阈值（单位 ms，内部自动换算成扫描次数） --------------- */
#define APP_KEY_SHORT_MS        1000    /* 按下时间 < 该值抬起 → 单击/双击 */
#define APP_KEY_LONG_MS         2000    /* 按下时间 ≥ 该值 → 长按(LONG_START) */
#define APP_KEY_HOLD_MS         3000    /* 按下时间 ≥ 该值 → 长按保持(LONG_HOLD，连发) */

/* ---- ② 按下有效电平 ------------------------------------------------------ */
/* 0 = 按键按下时引脚被拉到 GND（低有效，本板如此）；
 * 1 = 按键按下时引脚被拉到 VCC（高有效）。 */
#define APP_KEY_PRESSED_LEVEL   0

/* ---- ③ 最多支持多少个订阅者（同时有多少个线程/模块在监听按键） ----------- */
#define APP_KEY_MAX_SUBS        4
/* 队列深度 APP_KEY_QUEUE_SIZE 由 app_key.h 统一给出（它与 app_key_sub_t.q[]
 * 的数组长度是同一个宏，此处绝不能再定义一次，否则就是越界写） */


/* ---- ④ 引脚映射表：每个按键编号对应哪个 GPIO（换板子改这里） ------------ */
typedef struct
{
    gpio_type *port;    /* GPIO 端口 */
    uint16_t   pin;     /* 引脚位掩码（GPIO_PINS_x） */
} app_key_gpio_t;

static const app_key_gpio_t s_gpio[APP_KEY_NUM] =
{
    [APP_KEY_1] = { GPIOB, GPIO_PINS_4 },   /* 启动 */
    [APP_KEY_2] = { GPIOB, GPIO_PINS_5 },   /* 暂停 */
    [APP_KEY_3] = { GPIOB, GPIO_PINS_6 },   /* 预留 */
};

/**
  * @brief  按键 GPIO 初始化（硬件相关：换芯片/换端口时修改本函数）
  * @note   本板 PB4/5/6 配置为上拉输入，按键按下接地。
  *         PB4 默认复用为 JTAG-NJTRST，初始化为普通输入即释放该复用
  *         （SWD 走 PA13/PA14，不受影响）。
  */
static void app_key_gpio_init(void)
{
    gpio_init_type g;

    crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);

    gpio_default_para_init(&g);
    g.gpio_pins = GPIO_PINS_4 | GPIO_PINS_5 | GPIO_PINS_6;
    g.gpio_mode = GPIO_MODE_INPUT;
    g.gpio_pull = GPIO_PULL_UP;
    gpio_init(GPIOB, &g);
}

/**
  * @brief  读取单个按键的物理电平（实现 FlexibleButton 的 usr_button_read 回调）
  * @param  arg: 库在扫描时把对应的按键实例指针原样传回，借此取出 id
  * @retval 1 = 引脚为高电平；0 = 引脚为低电平
  * @note   只返回"裸电平"，不判断哪个电平算按下；
  *         按下是否有效由 APP_KEY_PRESSED_LEVEL（pressed_logic_level）决定。
  */
static uint8_t app_key_read(void *arg)
{
    flex_button_t *btn = (flex_button_t *)arg;
    app_key_id_t   id  = (app_key_id_t)btn->id;

    return (uint8_t)(gpio_input_data_bit_read(s_gpio[id].port,
                                              s_gpio[id].pin) != RESET);
}



/* ############################################################################
 * ##  【通用实现区】 以下代码与硬件、RTOS 无关，正常移植时不需要修改           ##
 * ##########################################################################*/

/* 3 个按键的库实例（库内部用单向链表把它们串起来） */
static flex_button_t s_btn[APP_KEY_NUM];

/* 订阅者表：每个订阅者一个指针，事件产生时遍历广播 */
static app_key_sub_t *s_subs[APP_KEY_MAX_SUBS];



/**
  * @brief  向一个订阅者的环形队列写入一个事件
  * @param  s: 订阅者句柄；id/event: 按键与事件。
  * @note   队列满时丢弃最新事件（不覆盖最旧，保证计数不越界），
  *         只在成功入队后才调用 notify，使"通知次数 = 队列中事件数"。
  *         单生产者(扫描端)单消费者(订阅者)，head/tail 各只由一方修改。
  */
static void app_key_ring_put(app_key_sub_t *s, uint8_t id, uint8_t event)
{
    uint16_t next = (uint16_t)((s->head + 1u) & APP_KEY_QUEUE_MASK);

    if (next == s->tail)
    {
        return;   /* 队列满：消费者来不及取，丢弃本次事件 */
    }

    s->q[s->head].id    = id;
    s->q[s->head].event = event;
    s->head             = next;

    if (s->notify != 0)
    {
        s->notify(s->notify_user);   /* 通知订阅者（RTOS 下即释放信号量） */
    }
}

/**
  * @brief  发布事件：广播给所有已订阅者（每个订阅者队列各收到一份）
  * @param  id: 按键编号；event: 事件。
  */
static void app_key_publish(app_key_id_t id, flex_button_event_t event)
{
    uint8_t i;

    for (i = 0; i < APP_KEY_MAX_SUBS; i++)
    {
        if (s_subs[i] != 0)
        {
            app_key_ring_put(s_subs[i], (uint8_t)id, (uint8_t)event);
        }
    }
}

/**
  * @brief  按键事件回调（实现 FlexibleButton 的 cb 回调）
  * @param  arg: 库把产生事件的按键实例指针传回。
  * @note   本函数在"调用 app_key_scan() 的上下文"执行（RTOS 下即扫描线程，
  *         裸机下即 main/SysTick）。这里只做纯 C 广播，不做任何业务。
  */
static void app_key_on_event(void *arg)
{
    flex_button_t *btn = (flex_button_t *)arg;

    app_key_publish((app_key_id_t)btn->id, (flex_button_event_t)btn->event);
}

/**
  * @brief  扫描一次（对外 API，纯 C）
  * @note   调一次 flex_button_scan()，内部完成"读所有键 → 消抖/状态机
  *         → 产生事件回调(广播)"。调用者必须保证固定周期（默认 20ms）。
  */
void app_key_scan(void)
{
    flex_button_scan();
}

/**
  * @brief  按键模块初始化（对外 API，纯 C）
  * @note   完成：清空订阅表 → GPIO 初始化 → 注册全部按键到库。
  *         不创建任何线程；扫描由外部周期调用 app_key_scan() 驱动。
  */
void app_key_init(void)
{
    int i;

    for (i = 0; i < APP_KEY_MAX_SUBS; i++)
    {
        s_subs[i] = 0;
    }

    app_key_gpio_init();

    for (i = 0; i < APP_KEY_NUM; i++)
    {
        s_btn[i].id                     = (uint8_t)i;
        s_btn[i].usr_button_read        = app_key_read;      /* 库如何读电平 */
        s_btn[i].cb                     = app_key_on_event;  /* 库如何上报事件 */
        s_btn[i].pressed_logic_level    = APP_KEY_PRESSED_LEVEL;
        s_btn[i].short_press_start_tick = FLEX_MS_TO_SCAN_CNT(APP_KEY_SHORT_MS);
        s_btn[i].long_press_start_tick  = FLEX_MS_TO_SCAN_CNT(APP_KEY_LONG_MS);
        s_btn[i].long_hold_start_tick   = FLEX_MS_TO_SCAN_CNT(APP_KEY_HOLD_MS);
        flex_button_register(&s_btn[i]);
    }
}

/**
  * @brief  订阅按键事件（对外 API，纯 C）
  * @param  sub: 应用层静态分配的订阅者句柄；
  * @param  notify/user: 事件到达通知钩子及用户数据（裸机可 NULL）。
  * @retval 成功返回 sub；订阅表已满或 sub 为空返回 NULL。
  */
app_key_sub_t *app_key_subscribe(app_key_sub_t *sub,
                                 app_key_notify_fn notify, void *user)
{
    int i;
    int free_slot = -1;

    if (sub == 0)
    {
        return 0;
    }

    /* 先查重，再申请空位。
     * 若先找空位后查重，当 sub 已在表中而前面存在空位时，会被重复插入两次：
     * 同一个事件投递两份、信号量计数多于队列长度，表现为"偶发丢键"。
     * 重复订阅只更新钩子，不重置队列（避免丢弃未读事件）。 */
    for (i = 0; i < APP_KEY_MAX_SUBS; i++)
    {
        if (s_subs[i] == sub)
        {
            sub->notify      = notify;
            sub->notify_user = user;
            return sub;
        }
    }

    /* 初始化该订阅者的队列 */
    sub->head        = 0;
    sub->tail        = 0;
    sub->notify      = notify;
    sub->notify_user = user;

    for (i = 0; i < APP_KEY_MAX_SUBS; i++)
    {
        if (s_subs[i] == 0)
        {
            free_slot = i;
            break;
        }
    }

    if (free_slot < 0)
    {
        return 0;   /* 订阅者已满 */
    }

    s_subs[free_slot] = sub;
    return sub;
}

/**
  * @brief  取消订阅（对外 API，纯 C）
  */
void app_key_unsubscribe(app_key_sub_t *sub)
{
    int i;

    for (i = 0; i < APP_KEY_MAX_SUBS; i++)
    {
        if (s_subs[i] == sub)
        {
            s_subs[i] = 0;
            return;
        }
    }
}

/**
  * @brief  非阻塞取一个事件（对外 API，纯 C）
  * @param  sub: 订阅者句柄；id/event: 输出（可 NULL）。
  * @retval 1=取到一个事件；0=队列空或 sub 为空。
  */
uint8_t app_key_get(app_key_sub_t *sub, app_key_id_t *id,
                    flex_button_event_t *event)
{
    if (sub == 0)
    {
        return 0;
    }
    if (sub->tail == sub->head)
    {
        return 0;   /* 队列空 */
    }

    if (id != 0)
    {
        *id = (app_key_id_t)sub->q[sub->tail].id;
    }
    if (event != 0)
    {
        *event = (flex_button_event_t)sub->q[sub->tail].event;
    }

    sub->tail = (uint16_t)((sub->tail + 1u) & APP_KEY_QUEUE_MASK);
    return 1;
}

/**
  * @brief  查询未读事件个数（对外 API，纯 C）
  */
uint8_t app_key_pending(app_key_sub_t *sub)
{
    if (sub == 0)
    {
        return 0;
    }

    /* 用无符号差 + 掩码求环形长度：对任意 head/tail 组合都正确，
     * 不依赖 int16_t 的符号判断，也不会因队列长度变大而溢出 */
    return (uint8_t)((uint16_t)(sub->head - sub->tail) & APP_KEY_QUEUE_MASK);
}
