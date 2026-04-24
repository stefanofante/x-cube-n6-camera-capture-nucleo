/**
  ******************************************************************************
  * @file    stm32_assert.h
  * @brief   STM32 LL drivers parameter-check helper.
  *          Copied from stm32_assert_template.h (MCD Application Team).
  ******************************************************************************
  */
#ifndef STM32_ASSERT_H
#define STM32_ASSERT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#ifdef USE_FULL_ASSERT
#define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
void assert_failed(uint8_t *file, uint32_t line);
#else
#define assert_param(expr) ((void)0U)
#endif /* USE_FULL_ASSERT */

#ifdef __cplusplus
}
#endif

#endif /* STM32_ASSERT_H */
