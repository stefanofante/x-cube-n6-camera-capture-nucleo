#ifndef BOOT_IMAGE_H
#define BOOT_IMAGE_H

#include <stddef.h>
#include <stdint.h>
#include "board_memory.h"

/* Validate the development STM32 v2.3 image envelope before copying anything.
 * This checks integrity and memory safety, NOT cryptographic authentication:
 * the normal development image is signed with STM32 SigningTool's -nk option.
 * Return the complete header+payload length, or zero for an invalid image.
 * available is the mapped source capacity, not a length trusted from flash. */
uint32_t BootImage_Validate(const uint8_t *image, size_t available);

#endif
