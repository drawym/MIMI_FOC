cmake_minimum_required(VERSION 3.20)

# 工具链必须放在 project() 之前
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/arm_gcc.cmake)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# 1️⃣ 项目基本信息
project(at32_app C ASM)

set(CMAKE_C_STANDARD 99)

# 2️⃣ 芯片型号 & 驱动使能定义（AT32F435RGT7 → AT32F435xG：1024K Flash / 320K RAM）
add_definitions(
    -DAT32F435RGT7
    -DUSE_STDPERIPH_DRIVER
)

# 3️⃣ 链接脚本与构建系统
set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/libraries/cmsis/cm4/device_support/startup/gcc/linker/AT32F435xG_FLASH.ld)
set(CMAKE_MAKE_PROGRAM "Ninja" CACHE PATH "Path to ninja")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 4️⃣ 头文件搜索路径
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/libraries/cmsis/cm4/core_support
    ${CMAKE_CURRENT_SOURCE_DIR}/libraries/cmsis/cm4/device_support
    ${CMAKE_CURRENT_SOURCE_DIR}/libraries/drivers/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/user/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/user/foc
    ${CMAKE_CURRENT_SOURCE_DIR}/user/key
    ${CMAKE_CURRENT_SOURCE_DIR}/rt-thread/include
    ${CMAKE_CURRENT_SOURCE_DIR}/rt-thread/bsp
    ${CMAKE_CURRENT_SOURCE_DIR}/rt-thread/libcpu/cortex-m4
    ${CMAKE_CURRENT_SOURCE_DIR}/rttview
)

# 5️⃣ 源文件清单（全部 AT32 外设驱动 + RT-Thread 内核 + SEGGER RTT + 应用）
# 驱动目录内文件增删时自动纳入构建（CONFIGURE_DEPENDS）
file(GLOB SOURCES_DRV CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/libraries/drivers/src/*.c)
set(SOURCES
    # --- AT32 固件库（全部驱动，按需由 --gc-sections 裁剪）---
    libraries/cmsis/cm4/device_support/system_at32f435_437.c
    libraries/cmsis/cm4/device_support/startup/gcc/startup_at32f435_437.s
    ${SOURCES_DRV}
    # --- RT-Thread 内核 ---
    rt-thread/src/clock.c
    rt-thread/src/components.c
    rt-thread/src/cpu.c
    rt-thread/src/idle.c
    rt-thread/src/ipc.c
    rt-thread/src/irq.c
    rt-thread/src/kservice.c
    rt-thread/src/mem.c
    rt-thread/src/object.c
    rt-thread/src/scheduler.c
    rt-thread/src/thread.c
    rt-thread/src/timer.c
    rt-thread/libcpu/cortex-m4/context_gcc.S
    rt-thread/libcpu/cortex-m4/cpuport.c
    rt-thread/bsp/board.c
    # --- SEGGER RTT（J-Link 打印）---
    rttview/RTT/SEGGER_RTT.c
    rttview/RTT/SEGGER_RTT_printf.c
    # --- 应用 ---
    user/src/main.c
    user/src/at32f435_437_clock.c
    user/src/tmr1_pwm.c
    user/src/curr_adc.c
    user/src/app_key.c
    user/src/app_key_rtos.c
    user/key/flexible_button.c
    user/foc/VF.c
    user/foc/VF_data.c
)

# 6️⃣ 编译选项：Cortex-M4 + 硬件 FPU
add_compile_options(-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
                     -ffunction-sections -fdata-sections -Wall)

# 7️⃣ 链接选项 & 生成可执行文件
# 注意：链接阶段同样需要指定架构/浮点参数，否则与目标文件 VFP/Thumb 状态不匹配
add_executable(${PROJECT_NAME}.elf ${SOURCES})

# 将链接脚本登记为链接依赖：以后改 .ld 只需 ninja -C build 即可自动重链，无需删 build
set_property(TARGET ${PROJECT_NAME}.elf APPEND PROPERTY LINK_DEPENDS ${LINKER_SCRIPT})

target_link_options(${PROJECT_NAME}.elf PRIVATE
    -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
    -T ${LINKER_SCRIPT} -Wl,--gc-sections -Wl,--print-memory-usage)
target_link_libraries(${PROJECT_NAME}.elf PRIVATE c m gcc nosys)

# 8️⃣ 生成烧录用的 .bin 和 .hex
add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}.elf> ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${PROJECT_NAME}.elf> ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.hex
    COMMENT "Generating .bin and .hex files"
    VERBATIM)



