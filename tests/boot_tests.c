#include "boot_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "Check failed at %d: %s\n", __LINE__, #expr); abort(); } } while (0)
static uint8_t image[BOARD_APP_IMAGE_CAPACITY];
static void put(uint8_t *where, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) where[i] = (uint8_t)(value >> (i * 8));
}
static void prepare(uint32_t length) {
    memset(image, 0, sizeof(image));
    put(image, 0x324d5453U);
    put(image + 72, 0x00020300U);
    put(image + 76, length);
    put(image + BOARD_IMAGE_HEADER_SIZE, BOARD_APP_RAM_END);
    put(image + BOARD_IMAGE_HEADER_SIZE + 4, BOARD_APP_VECTOR_BASE + 9U);
    uint32_t checksum = 0;
    for (uint32_t i = 0; i < length; ++i) checksum += image[BOARD_IMAGE_HEADER_SIZE + i];
    put(image + 68, checksum);
}
int main(void) {
    /* Synthetic v2.3 development headers exercise the actual FSBL parser.
     * They are not a substitute for signing-tool or board boot validation. */
    prepare(65537);
    CHECK(BootImage_Validate(image, sizeof(image)) == BOARD_IMAGE_HEADER_SIZE + 65537U);
    CHECK(BootImage_Validate(image, 65537) == 0);
    image[0] ^= 1; CHECK(BootImage_Validate(image, sizeof(image)) == 0); image[0] ^= 1;
    image[73] = 2; CHECK(BootImage_Validate(image, sizeof(image)) == 0); image[73] = 3;
    put(image + 76, UINT32_MAX); CHECK(BootImage_Validate(image, sizeof(image)) == 0);
    prepare(65537);
    image[BOARD_IMAGE_HEADER_SIZE + 65536] = 1; CHECK(BootImage_Validate(image, sizeof(image)) == 0);
    prepare(65537);
    put(image + BOARD_IMAGE_HEADER_SIZE, BOARD_APP_RAM_END + 8U); CHECK(BootImage_Validate(image, sizeof(image)) == 0);
    prepare(65537);
    put(image + BOARD_IMAGE_HEADER_SIZE + 4, BOARD_APP_VECTOR_BASE + 8U); CHECK(BootImage_Validate(image, sizeof(image)) == 0);
    prepare(65537);
    put(image + BOARD_IMAGE_HEADER_SIZE + 4, BOARD_DMA_RAM_BASE + 1U); CHECK(BootImage_Validate(image, sizeof(image)) == 0);
    prepare(BOARD_APP_PAYLOAD_CAPACITY);
    CHECK(BootImage_Validate(image, sizeof(image)) == sizeof(image));
    put(image + 76, BOARD_APP_PAYLOAD_CAPACITY + 1U); CHECK(BootImage_Validate(image, sizeof(image)) == 0);
    CHECK(BootImage_Validate(NULL, sizeof(image)) == 0);
    puts("Passed boot header, checksum, vectors and copy-capacity checks");
}
