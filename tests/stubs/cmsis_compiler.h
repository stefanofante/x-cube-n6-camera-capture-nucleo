#ifndef TEST_CMSIS_COMPILER_H
#define TEST_CMSIS_COMPILER_H
#include <stdint.h>
#define __PACKED __attribute__((packed))
static inline uint32_t __get_PRIMASK(void) { return 0; }
static inline void __disable_irq(void) {}
static inline void __set_PRIMASK(uint32_t flags) { (void)flags; }
static inline void __DMB(void) {}
#endif
