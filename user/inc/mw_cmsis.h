/**
  ******************************************************************************
  * @file    mw_cmsis.h
  * @brief   CMSIS-DSP 轻量兼容垫片（shim）—— 供 Simulink ERT 生成代码使用
  *
  *  背景：Simulink（R2025b，ARM Cortex-M 目标）生成的代码会 include "mw_cmsis.h"
  *        并调用 arm_sin_f32 / arm_cos_f32 / arm_scale_f32 / arm_offset_f32
  *        等 CMSIS-DSP 函数。本工程没有链接完整 CMSIS-DSP 库，
  *        这里用 newlib libm（sinf/cosf）和内联循环实现同名同语义函数，
  *        使生成的 VF.c 等文件【无需任何修改】即可直接编译。
  *
  *  性能：sinf/cosf 约 100~200 周期；10kHz 控制周期（100us）下，
  *        2 次三角 + 几次向量乘加总耗时 <5us，占比 <5%，足够。
  *        若以后控制频率大幅提高、或模型用了更多 DSP 函数，
  *        可再改为引入官方 CMSIS-DSP 源码（删除本文件即可，注意函数重名）。
  *
  *  扩展：生成代码若用到新的 arm_xxx 函数，在本文件按相同方式补实现。
  ******************************************************************************
  */

#ifndef MW_CMSIS_H
#define MW_CMSIS_H

#include <math.h>
#include <stdint.h>

/* CMSIS-DSP 类型别名（本工程只用到 float32） */
typedef float float32_t;

/*===========================================================================*
  * 快速三角函数
  * 注意：生成代码按【返回值】形式调用（rtb = arm_sin_f32(x)），
  *       与旧版 CMSIS-DSP 的指针输出形式不同，这里与生成代码保持一致。
  *===========================================================================*/
static inline float32_t arm_sin_f32(float32_t x)
{
    return sinf(x);
}

static inline float32_t arm_cos_f32(float32_t x)
{
    return cosf(x);
}

/*===========================================================================*
  * 向量缩放：pDst[i] = pSrc[i] * scale
  *===========================================================================*/
static inline void arm_scale_f32(const float32_t *pSrc,
                                 float32_t scale,
                                 float32_t *pDst,
                                 uint32_t blockSize)
{
    uint32_t i;
    for(i = 0U; i < blockSize; i++)
    {
        pDst[i] = pSrc[i] * scale;
    }
}

/*===========================================================================*
  * 向量偏置：pDst[i] = pSrc[i] + offset
  *===========================================================================*/
static inline void arm_offset_f32(const float32_t *pSrc,
                                  float32_t offset,
                                  float32_t *pDst,
                                  uint32_t blockSize)
{
    uint32_t i;
    for(i = 0U; i < blockSize; i++)
    {
        pDst[i] = pSrc[i] + offset;
    }
}

/*===========================================================================*
  * MathWorks 包装宏（与官方 mw_cmsis.h 中同名宏同义，VF.c 直接调用它们）
  *===========================================================================*/
#define mw_arm_scale_1_f32(scaleValue, pSrc, pDst, blockSize) \
    arm_scale_f32((float32_t *)(pSrc), *(scaleValue), (float32_t *)(pDst), blockSize)

#define mw_arm_bias_2_f32(pSrc, biasValue, pDst, blockSize) \
    arm_offset_f32((float32_t *)(pSrc), *(biasValue), (float32_t *)(pDst), blockSize)

#endif /* MW_CMSIS_H */
