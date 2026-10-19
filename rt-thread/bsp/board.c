/*
 * Copyright (c) 2006-2019, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2021-05-24                  the first version
 */

#include <rthw.h>
#include <rtthread.h>
#include "at32f435_437_clock.h"
#include "SEGGER_RTT.h"

#if defined(RT_USING_USER_MAIN) && defined(RT_USING_HEAP)
/*
 * Please modify RT_HEAP_SIZE if you enable RT_USING_HEAP
 * the RT_HEAP_SIZE max value = (sram size - ZI size), 1024 means 1024 bytes
 */
#define RT_HEAP_SIZE (20*1024)
static rt_uint8_t rt_heap[RT_HEAP_SIZE];

RT_WEAK void *rt_heap_begin_get(void)
{
    return rt_heap;
}

RT_WEAK void *rt_heap_end_get(void)
{
    return rt_heap + RT_HEAP_SIZE;
}
#endif

void rt_os_tick_callback(void)
{
    rt_interrupt_enter();
    
    rt_tick_increase();

    rt_interrupt_leave();
}

// 3) 在SysTick中断里调用tick回调
void SysTick_Handler(void)
{
    rt_os_tick_callback();
}

/**
 * This function will initial your board.
 */
void rt_hw_board_init(void)
{
    system_clock_config();      // 配置系统时钟

    /* 配置SysTick为RT-Thread系统节拍 */
    /* 以HCLK为时钟源，按RT_TICK_PER_SECOND频率产生中断 */
    SysTick->LOAD  = SystemCoreClock / RT_TICK_PER_SECOND - 1; /* 计数初值 */
    SysTick->VAL   = 0;                                        /* 清当前值 */
    SysTick->CTRL  = SysTick_CTRL_CLKSOURCE_Msk |              /* 时钟源：HCLK */
                     SysTick_CTRL_TICKINT_Msk   |              /* 使能中断 */
                     SysTick_CTRL_ENABLE_Msk;                  /* 使能计数 */

    /* 可选：配置SysTick中断优先级为最低，避免打断临界实时中断 */
#ifdef __NVIC_PRIO_BITS
    NVIC_SetPriority(SysTick_IRQn, (1UL << __NVIC_PRIO_BITS) - 1UL);
#endif

    /* Call components board initial (use INIT_BOARD_EXPORT()) */
#ifdef RT_USING_COMPONENTS_INIT
    rt_components_board_init();
#endif

#if defined(RT_USING_USER_MAIN) && defined(RT_USING_HEAP)
    rt_system_heap_init(rt_heap_begin_get(), rt_heap_end_get());
#endif
}

#ifdef RT_USING_CONSOLE

static int uart_init(void)
{
    /* 初始化SEGGER RTT（板级初始化阶段尽早建立 RTT 控制块，供 J-Link RTT Viewer 识别） */
    SEGGER_RTT_Init();
    return 0;
}
INIT_BOARD_EXPORT(uart_init);

void rt_hw_console_output(const char *str)
{
// #error "TODO 3: Output the string 'str' through the uart."
    /* 使用SEGGER RTT输出字符串 */
    SEGGER_RTT_WriteString(0, str);
}

#endif

