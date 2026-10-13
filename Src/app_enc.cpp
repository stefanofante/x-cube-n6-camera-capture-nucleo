 /**
 ******************************************************************************
 * @file    app_enc.cpp
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
#include "app_enc.h"
#include "app_config.h"

#include <assert.h>
#include <climits>
#include "board_memory.h"
#include "static_storage.hpp"
#include "h264_padding.h"
#include <stdlib.h>
#include <string.h>

#include "h264encapi.h"
#include "jpegencapi.h"
#include "stm32n6xx_hal.h"
#include "utils.h"
#include "ewl.h"

/* The Nucleo has no external PSRAM. This arena shares the dedicated 1.5 MiB
 * internal DMA region with two YUY2 frames and two compressed outputs. */
#define VENC_ALLOCATOR_SIZE BOARD_VENC_ARENA_SIZE
#define RATE_CTRL_QP 25

enum {
  VENC_RATE_CTRL_QP_CONSTANT,
  VENC_RATE_CTRL_VBR,
};

static std::byte venc_hw_allocator_buffer[VENC_ALLOCATOR_SIZE] ALIGN_32 IN_DMA_MEMORY;
static constinit camera::LinearArena venc_arena{std::span{venc_hw_allocator_buffer}};
static constinit struct VENC_Context {
  H264EncInst hdl;
  int is_sps_pps_done;
  uint32_t pic_cnt;
  int gop_len;
  camera::EncoderClock clock;
} VENC_Instance{};

static void VENC_SetupConstantQp(H264EncRateCtrl *rate, int qp)
{
  rate->pictureRc = 0;
  rate->mbRc = 0;
  rate->pictureSkip = 0;
  rate->hrd = 0;
  rate->qpHdr = qp;
  rate->qpMin = qp;
  rate->qpMax = qp;
}

static void VENC_SetupVbr(H264EncRateCtrl *rate, int bitrate, int gopLen, int qp)
{
  rate->pictureRc = 1;
  rate->mbRc = 1;
  rate->pictureSkip = 0;
  rate->hrd = 0;
  rate->qpHdr = qp;
  rate->qpMin = 10;
  rate->qpMax = 51;
  rate->gopLen = gopLen;
  rate->bitPerSecond = bitrate;
  rate->intraQpDelta = 0;
}

static int VENC_EncodeStart(struct VENC_Context *p_ctx, uint8_t *p_out, size_t out_len, size_t *p_out_len)
{
  H264EncOut enc_out = {};
  H264EncIn enc_in = {};
  size_t start_len;
  size_t pad_len;
  int ret;

  enc_in.pOutBuf = (u32 *) p_out;
  enc_in.busOutBuf = (ptr_t) p_out;
  enc_in.outBufSize = out_len;
  ret = H264EncStrmStart(p_ctx->hdl, &enc_in, &enc_out);
  if (ret)
    return ret;

  start_len = enc_out.streamSize;
  if (start_len > out_len)
    return H264ENC_OUTPUT_BUFFER_OVERFLOW;
  ret = H264_AppendPadding(&p_out[start_len], out_len - start_len, &pad_len);
  if (ret)
    return ret;

  *p_out_len = start_len + pad_len;

  return 0;
}

static int VENC_EncodeFrame(struct VENC_Context *p_ctx, uint8_t *p_in, uint8_t *p_out, size_t out_len,
                            size_t *p_out_len, int is_intra_force, uint32_t timestamp_ms)
{
  H264EncOut enc_out = {};
  H264EncIn enc_in = {};
  int ret;

  /* In both N6_VENC_INPUT_YUV2 and N6_VENC_INPUT_RGB565 only busLuma is used */
  enc_in.busLuma = (ptr_t) p_in;
  enc_in.busChromaU = 0;
  enc_in.busChromaV = 0;
  enc_in.pOutBuf = (u32 *) p_out;
  enc_in.busOutBuf = (ptr_t) p_out;
  enc_in.outBufSize = out_len;
  enc_in.codingType = (p_ctx->pic_cnt % (p_ctx->gop_len + 1) == 0) ? H264ENC_INTRA_FRAME : H264ENC_PREDICTED_FRAME;
  enc_in.codingType = is_intra_force ? H264ENC_INTRA_FRAME : enc_in.codingType;
  enc_in.timeIncrement = p_ctx->clock.increment(timestamp_ms);
  enc_in.ipf = H264ENC_REFERENCE_AND_REFRESH; /* FIXME : can be H264ENC_NO_REFERENCE_NO_REFRESH in I only mode */
  enc_in.ltrf = H264ENC_NO_REFERENCE_NO_REFRESH;
  enc_in.lineBufWrCnt = 0;
  enc_in.sendAUD = 0;

  ret = H264EncStrmEncode(p_ctx->hdl, &enc_in, &enc_out, NULL, NULL, NULL);
  if (ret != H264ENC_FRAME_READY)
    return ret < 0 ? ret : H264ENC_ERROR;
  if (enc_out.streamSize > out_len)
    return H264ENC_OUTPUT_BUFFER_OVERFLOW;

  p_ctx->clock.commit(timestamp_ms);
  p_ctx->pic_cnt++;
  *p_out_len = enc_out.streamSize;

  return 0;
}

static int VENC_Encode(uint8_t *p_in, uint8_t *p_out, size_t out_len, size_t *p_out_len, int is_intra_force, uint32_t timestamp_ms)
{
  struct VENC_Context *p_ctx = &VENC_Instance;
  size_t start_len = 0;
  size_t frame_len;
  int ret;

  if (!p_ctx->is_sps_pps_done)
  {
    ret = VENC_EncodeStart(p_ctx, p_out, out_len, &start_len);
    if (ret)
      return ret;
    p_ctx->is_sps_pps_done = 1;
  }

  ret = VENC_EncodeFrame(p_ctx, p_in, &p_out[start_len], out_len - start_len, &frame_len, is_intra_force, timestamp_ms);
  if (ret)
    return ret;

  *p_out_len = start_len + frame_len;

  return 0;
}

int ENC_Init(const ENC_Conf_t *p_conf)
{
  const int rate_ctrl_mode = VENC_RATE_CTRL_VBR;
  struct VENC_Context *p_ctx = &VENC_Instance;
  H264EncPreProcessingCfg cfg;
  H264EncCodingCtrl ctrl;
  H264EncConfig config;
  H264EncRateCtrl rate;
  int target_bitrate;
  int ret;

  if (!p_conf || p_conf->width <= 0 || p_conf->height <= 0 ||
      p_conf->width % 16 || p_conf->height % 16 || p_conf->fps <= 0 || p_conf->fps > 60 ||
      static_cast<uint64_t>(p_conf->width) * p_conf->height * 2U > MAX_IMG_FRAME_SIZE || p_ctx->hdl)
    return H264ENC_INVALID_ARGUMENT;
  memset(&config, 0, sizeof(config));
  p_ctx->gop_len = p_conf->fps - 1;
  /* init encoder */
  config.streamType = H264ENC_BYTE_STREAM;
  config.viewMode = H264ENC_BASE_VIEW_SINGLE_BUFFER;
  /* Level 3.1 accommodates the configured macroblock rate and bitrate for
   * this fixed SRAM-bounded profile without advertising unnecessary 5.1. */
  config.level = H264ENC_LEVEL_3_1;
  config.width = p_conf->width;
  config.height = p_conf->height;
  config.frameRateNum = p_conf->fps * 1000U;
  config.frameRateDenom = 1000U;
  p_ctx->clock.configure(p_conf->fps);
  config.refFrameAmount = 1;
  ret = H264EncInit(&config, &p_ctx->hdl);
  if (ret != H264ENC_OK)
    goto failed;

  /* setup source format: camera pipe outputs YUY2 which the HANTRO encoder
   * consumes as YUV422 interleaved. */
  ret = H264EncGetPreProcessing(p_ctx->hdl, &cfg);
  if (ret != H264ENC_OK)
    goto failed;
  cfg.inputType = H264ENC_YUV422_INTERLEAVED_YUYV;
  ret = H264EncSetPreProcessing(p_ctx->hdl, &cfg);
  if (ret != H264ENC_OK)
    goto failed;

  /* setup coding ctrl */
  ret = H264EncGetCodingCtrl(p_ctx->hdl, &ctrl);
  if (ret != H264ENC_OK)
    goto failed;
  ctrl.idrHeader = 1;
  ret = H264EncSetCodingCtrl(p_ctx->hdl, &ctrl);
  if (ret != H264ENC_OK)
    goto failed;

  /* setup rate ctrl */
  ret = H264EncGetRateCtrl(p_ctx->hdl, &rate);
  if (ret != H264ENC_OK)
    goto failed;
  target_bitrate = static_cast<int>((static_cast<uint64_t>(p_conf->width) * p_conf->height * 12U * p_conf->fps) / 30U);
  if (rate_ctrl_mode == VENC_RATE_CTRL_QP_CONSTANT)
  {
    VENC_SetupConstantQp(&rate, RATE_CTRL_QP);
  } else if (rate_ctrl_mode == VENC_RATE_CTRL_VBR)
  {
    VENC_SetupVbr(&rate, target_bitrate, p_conf->fps, RATE_CTRL_QP);
  } else
  {
    assert(0);
  }
  ret = H264EncSetRateCtrl(p_ctx->hdl, &rate);
  if (ret != H264ENC_OK)
    goto failed;
  return H264ENC_OK;
failed:
  if (p_ctx->hdl)
    H264EncRelease(p_ctx->hdl);
  *p_ctx = {};
  venc_arena.reset();
  return ret;
}

int ENC_DeInit()
{
  struct VENC_Context *p_ctx = &VENC_Instance;
  int ret;

  if (!p_ctx->hdl)
    return H264ENC_OK;
  ret = H264EncRelease(p_ctx->hdl);
  if (ret != H264ENC_OK)
    return ret;

  /* Reset the linear HW allocator: without this, a Start / End / Start
   * sequence (e.g. AMCAP probing the device) quickly exhausts
   * VENC_ALLOCATOR_SIZE and the next H264EncInit() fails. */
  venc_arena.reset();
  p_ctx->is_sps_pps_done = 0;
  p_ctx->pic_cnt = 0;
  p_ctx->clock.configure(0);
  p_ctx->hdl = NULL;
  return H264ENC_OK;
}

size_t ENC_GetArenaHighWater(void)
{
  return venc_arena.highWater();
}

int ENC_EncodeFrame(uint8_t *p_in, uint8_t *p_out, size_t out_len, int is_intra_force, uint32_t timestamp_ms)
{
  if (!VENC_Instance.hdl || !p_in || !p_out || out_len > INT_MAX || out_len < 64 ||
      (reinterpret_cast<uintptr_t>(p_in) & 7U) || (reinterpret_cast<uintptr_t>(p_out) & 7U))
    return H264ENC_INVALID_ARGUMENT;
  size_t out_compressed_frame_len = 0;
  int ret;

  ret = VENC_Encode(p_in, p_out, out_len, &out_compressed_frame_len, is_intra_force, timestamp_ms);

  return ret ? ret : static_cast<int>(out_compressed_frame_len);
}

extern "C" void *EWLmalloc(u32 n)
{
  void *res = malloc(n);


  return res;
}

extern "C" void EWLfree(void *p)
{
  free(p);
}

extern "C" void *EWLcalloc(u32 n, u32 s)
{
  void *res = calloc(n, s);


  return res;
}

/* Every allocation is independently 32-byte aligned. Frame and DMA users
 * require at least eight bytes; cache-line alignment also makes future cache
 * maintenance safe. The arena is reset only after H264EncRelease succeeds. */
extern "C" i32 EWLMallocLinear(const void *instance, u32 size, EWLLinearMem_t *info)
{
  if (!instance || !info)
    return EWL_ERROR;
  const auto allocation = venc_arena.allocate(size, 32U);
  if (allocation.empty())
    return EWL_ERROR;
  info->size = size;
  info->virtualAddress = reinterpret_cast<u32 *>(allocation.data());
  info->busAddress = reinterpret_cast<ptr_t>(allocation.data());
  return EWL_OK;
}

/* Linear allocations belong to the encoder instance, not the generic EWL
 * chunk table. Per-chunk release only clears metadata; instance teardown is
 * the sole place allowed to reset the arena after the hardware is quiescent. */
extern "C" void EWLFreeLinear(const void *instance, EWLLinearMem_t *info)
{
  (void)instance;
  if (info) *info = {};
}
