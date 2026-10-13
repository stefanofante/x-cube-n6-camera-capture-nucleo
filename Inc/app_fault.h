#ifndef APP_FAULT_H
#define APP_FAULT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#define APP_NORETURN [[noreturn]]
#else
#define APP_NORETURN _Noreturn
#endif

/* Fatal failures remain fatal with NDEBUG. The record is retained in NOLOAD
 * SRAM for debugger inspection; reporting never needs UART, heap or locks. */
APP_NORETURN void App_Fatal(const char *file, uint32_t line, const char *reason);
#define APP_FATAL(reason) App_Fatal(__FILE__, __LINE__, (reason))
#define APP_REQUIRE(condition) do { if (!(condition)) APP_FATAL(#condition); } while (0)
#ifdef __cplusplus
}
#endif
#endif
