#ifndef BOARD_MEMORY_H
#define BOARD_MEMORY_H

/* NUCLEO-N657X0-Q: use only the first two 1 MiB AXI SRAM banks of the
 * STM32N657X0. No external PSRAM, DTCM, NPU memory or extra SRAM bank is
 * required. Keep these boundaries synchronized with the linker assertions.
 * The signed application includes a 1 KiB STM32 header before its vectors.
 * DMA storage may reuse the FSBL RAM only AFTER control has left the FSBL;
 * it is NOLOAD and is never written by the bootloader's application copy. */
#define BOARD_APP_IMAGE_BASE       0x34000000U
#define BOARD_IMAGE_HEADER_SIZE    0x00000400U
#define BOARD_APP_VECTOR_BASE      0x34000400U
#define BOARD_APP_RAM_END          0x34070000U
#define BOARD_DMA_RAM_BASE         0x34070000U
#define BOARD_DMA_RAM_SIZE         0x00180000U
#define BOARD_USB_RAM_BASE         0x341f0000U
#define BOARD_USB_RAM_SIZE         0x00010000U
#define BOARD_FSBL_IMAGE_BASE      0x34180000U
#define BOARD_NOR_APP_BASE         0x70100000U
#define BOARD_APP_IMAGE_CAPACITY   (BOARD_APP_RAM_END - BOARD_APP_IMAGE_BASE)
#define BOARD_APP_PAYLOAD_CAPACITY (BOARD_APP_RAM_END - BOARD_APP_VECTOR_BASE)
#define BOARD_VENC_ARENA_SIZE      (768U * 1024U)

#endif
