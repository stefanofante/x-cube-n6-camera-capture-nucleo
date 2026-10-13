#include "boot_image.h"

/* Read the documented little-endian common STM32 header fields without
 * packed-structure casts, unaligned loads or compiler-specific padding. */
static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

uint32_t BootImage_Validate(const uint8_t *image, size_t available)
{
    if (!image || available < BOARD_IMAGE_HEADER_SIZE + 8U)
        return 0;

    /* Magic is the byte sequence 'S', 'T', 'M', 0x32. Header version bytes
     * encode variant, minor, major, reserved; this project uses exactly 2.3. */
    if (read_le32(image) != 0x324d5453U || read_le32(image + 72) != 0x00020300U)
        return 0;

    const uint32_t length = read_le32(image + 76);
    if (length < 8U || length > BOARD_APP_PAYLOAD_CAPACITY ||
        length > available - BOARD_IMAGE_HEADER_SIZE)
        return 0;

    const uint8_t *payload = image + BOARD_IMAGE_HEADER_SIZE;
    const uint32_t stack = read_le32(payload);
    const uint32_t reset = read_le32(payload + 4);
    /* The initial stack must match the fixed exclusive upper RAM boundary.
     * Reset must be a Thumb address inside the bytes that will be copied. */
    if ((stack & 7U) || stack != BOARD_APP_RAM_END ||
        !(reset & 1U) || (reset & ~1U) < BOARD_APP_VECTOR_BASE + 8U ||
        (reset & ~1U) - BOARD_APP_VECTOR_BASE >= length)
        return 0;

    uint32_t checksum = 0;
    for (uint32_t i = 0; i < length; ++i)
        checksum += payload[i];
    if (checksum != read_le32(image + 68))
        return 0;

    return BOARD_IMAGE_HEADER_SIZE + length;
}
