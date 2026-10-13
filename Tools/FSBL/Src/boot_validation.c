#include "boot_image.h"
#include "stm32_boot_lrun.h"

extern void Error_Handler(void);

/* Override the middleware's fixed-size weak hook. Stop before its copy/jump
 * path on invalid flash, rather than returning zero and letting it jump to
 * an old or uninitialized vector table. The copy fits below 0x34070000 and
 * cannot overwrite the currently executing FSBL at 0x34180400. */
uint32_t BOOT_GetApplicationSize(uint32_t image_address)
{
    if (image_address != BOARD_NOR_APP_BASE) {
        Error_Handler();
        for (;;) {}
    }
    const uint32_t size = BootImage_Validate((const uint8_t *)image_address,
                                           BOARD_APP_IMAGE_CAPACITY);
    if (!size) {
        Error_Handler();
        for (;;) {}
    }
    return size;
}
