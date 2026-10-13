 /**
 ******************************************************************************
 * @file    enc.h
 * @author  GPM Application Team
 *
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2023 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
#ifndef ENC
#define ENC

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int width;
  int height;
  int fps;
} ENC_Conf_t;

/* Initialization failures preserve the original H264Enc error code. */
int ENC_Init(const ENC_Conf_t *p_conf);
int ENC_DeInit(void);
/* Lifetime peak DMA-arena use, retained across stream restarts. */
size_t ENC_GetArenaHighWater(void);
int ENC_EncodeFrame(uint8_t *p_in, uint8_t *p_out, size_t out_len, int is_intra_force, uint32_t timestamp_ms);

#ifdef __cplusplus
}
#endif

#endif