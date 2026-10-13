#ifndef H264_PADDING_H
#define H264_PADDING_H
#include <stddef.h>
#include <stdint.h>

/* Align the next hardware output address to eight bytes using a complete
 * Annex-B filler_data NAL: four-byte start code, nal_ref_idc=0/type=12,
 * optional ff bytes, and rbsp_trailing_bits. Nothing is written on failure. */
static inline int H264_AppendPadding(uint8_t *output, size_t capacity, size_t *written)
{
    if (!output || !written) return -1;
    *written = 0;
    const size_t remainder = (uintptr_t)output & 7U;
    if (!remainder) return 0;
    size_t length = 8U - remainder;
    if (length < 6U) length += 8U;
    if (length > capacity) return -1;
    output[0] = output[1] = output[2] = 0;
    output[3] = 1;
    output[4] = 0x0c;
    for (size_t i = 5; i + 1 < length; ++i) output[i] = 0xff;
    output[length - 1] = 0x80;
    *written = length;
    return 0;
}
#endif
