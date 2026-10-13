#include "app_fault.h"
#include "stm32n6xx_hal.h"

struct AppFaultRecord {
    uint32_t magic;
    const char *file;
    const char *reason;
    uint32_t line;
    uint32_t exception;
    uint32_t cfsr;
    uint32_t hfsr;
};

/* Startup does not clear .noinit. Write magic last to identify a complete
 * record, rather than uninitialized power-on RAM or an interrupted write. */
volatile struct AppFaultRecord app_fault_record __attribute__((section(".noinit")));

_Noreturn void App_Fatal(const char *file, uint32_t line, const char *reason)
{
    __disable_irq();
    app_fault_record.magic = 0;
    app_fault_record.file = file;
    app_fault_record.reason = reason;
    app_fault_record.line = line;
    app_fault_record.exception = __get_IPSR();
    app_fault_record.cfsr = SCB->CFSR;
    app_fault_record.hfsr = SCB->HFSR;
    __DMB();
    app_fault_record.magic = 0x4641554cU;
    if (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk)
        __BKPT(0);
    for (;;) __WFI();
}

/* newlib assertions use the same path, without printing from an ISR. */
void __assert_func(const char *file, int line, const char *function, const char *expression)
{
    (void)function;
    App_Fatal(file, (uint32_t)line, expression);
}

void vApplicationStackOverflowHook(void *task, char *name)
{
    (void)task;
    App_Fatal("FreeRTOS stack", 0, name);
}
