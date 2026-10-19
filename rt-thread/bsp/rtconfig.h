/* RT-Thread 配置文件 */

#ifndef __RTTHREAD_CFG_H__
#define __RTTHREAD_CFG_H__

// <<< 在上下文菜单中使用配置向导 >>>
#define RT_USING_LIBC
// <h>基本配置
// <o>线程优先级最大级别 <8-256>
//  <i>默认值: 32
#define RT_THREAD_PRIORITY_MAX  32
// <o>每秒系统时钟节拍数
//  <i>默认值: 1000   (1ms)
#define RT_TICK_PER_SECOND  1000
// <o>CPU架构数据访问的对齐大小
//  <i>默认值: 4
#define RT_ALIGN_SIZE   4
// <o>对象名称的最大长度<2-16>
//  <i>默认值: 8
#define RT_NAME_MAX    8
// <c1>使用RT-Thread组件初始化
//  <i>使用RT-Thread组件初始化
#define RT_USING_COMPONENTS_INIT
// </c>

#define RT_USING_USER_MAIN

// <o>主线程栈大小<1-4086>
//  <i>默认值: 512
#define RT_MAIN_THREAD_STACK_SIZE     2048

// </h>

// <h>调试配置
// <c1>启用内核调试配置
//  <i>默认值: 启用内核调试配置
//#define RT_DEBUG
// </c>
// <o>启用组件初始化调试配置<0-1>
//  <i>默认值: 0
#define RT_DEBUG_INIT 0
// <c1>线程栈溢出检测
//  <i> 禁用线程栈溢出检测
//#define RT_USING_OVERFLOW_CHECK
// </c>
// </h>

// <h>钩子配置
// <c1>使用钩子
//  <i>使用钩子
//#define RT_USING_HOOK
// </c>
// <c1>使用空闲钩子
//  <i>使用空闲钩子
//#define RT_USING_IDLE_HOOK
// </c>
// </h>

// <e>软件定时器配置
// <i> 启用用户定时器
#define RT_USING_TIMER_SOFT         0
#if RT_USING_TIMER_SOFT == 0
    #undef RT_USING_TIMER_SOFT
#endif
// <o>定时器线程优先级级别 <0-31>
//  <i>默认值: 4
#define RT_TIMER_THREAD_PRIO        4
// <o>定时器线程栈大小 <0-8192>
//  <i>默认值: 512
#define RT_TIMER_THREAD_STACK_SIZE  512
// </e>

// <h>IPC(进程间通信) 配置
// <c1>使用信号量
//  <i>使用信号量
#define RT_USING_SEMAPHORE
// </c>
// <c1>使用互斥量
//  <i>使用互斥量
//#define RT_USING_MUTEX
// </c>
// <c1>使用事件
//  <i>使用事件
//#define RT_USING_EVENT
// </c>
// <c1>使用邮箱
//  <i>使用邮箱
//#define RT_USING_SIGNALS
// </c>
// <c1>使用信号
//  <i>使用信号
#define RT_USING_MAILBOX
// </c>
// <c1>使用消息队列
//  <i>使用消息队列
//#define RT_USING_MESSAGEQUEUE
// </c>
// </h>

// <h>内存管理配置
// <c1>内存池管理
//  <i>内存池管理
//#define RT_USING_MEMPOOL
// </c>
// <c1>动态堆管理(算法: 小内存)
//  <i>动态堆管理
#define RT_USING_HEAP
#define RT_USING_SMALL_MEM
#define RT_USING_SMALL_MEM_AS_HEAP
// </c>
// <c1>使用微小内存大小
//  <i>使用微小内存大小
//#define RT_USING_TINY_SIZE
// </c>
// </h>

// <h>控制台配置
// <c1>使用控制台
//  <i>使用控制台
#define RT_USING_CONSOLE
// </c>
// <o>控制台缓冲区大小 <1-1024>
//  <i>控制台缓冲区大小
//  <i>默认值: 128  (128字节)
#define RT_CONSOLEBUF_SIZE          256
// </h>

// <h>FinSH配置
// <c1>包含finsh配置
//  <i>如果使用FinSH请选择此项
//#include "finsh_config.h"
// </c>
// </h>

// <h>设备配置
// <c1>使用设备框架
//  <i>使用设备框架
//#define RT_USING_DEVICE
// </c>
// </h>

// <<< 配置部分结束 >>>

#endif
