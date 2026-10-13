#ifndef UVCL_PACKET_H
#define UVCL_PACKET_H

#include <stdint.h>

/* Divide a frame into payloads after reserving the two-byte UVC header.
 * Quotient+remainder avoids overflow near INT_MAX. The final packet always
 * contains between one byte and a full payload, including exact multiples. */
static inline int UVCL_PlanPackets(int size, int packet_size, int *count, int *last)
{
    if (size <= 0 || packet_size <= 2 || !count || !last)
        return -1;
    const int payload = packet_size - 2;
    const int remainder = size % payload;
    *count = size / payload + (remainder != 0);
    *last = remainder ? remainder : payload;
    return 0;
}

#endif
