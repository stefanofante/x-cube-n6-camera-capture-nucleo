/**
 ******************************************************************************
 * @file    app_config.h
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
#ifndef APP_CONFIG
#define APP_CONFIG

/* ifndef so you can change below without code modification */
#ifndef IMG_STREAMS
#ifdef STM32N6570_DK_REV
#define IMG_STREAMS {{UVCL_PAYLOAD_UNCOMPRESSED_YUY2, 224, 224, 30}, \
                     {UVCL_PAYLOAD_UNCOMPRESSED_YUY2, 256, 256, 30}, \
                     {UVCL_PAYLOAD_UNCOMPRESSED_YUY2, 480, 480, 30}, \
                     {UVCL_PAYLOAD_UNCOMPRESSED_YUY2, 640, 480, 30}, \
                     {UVCL_PAYLOAD_UNCOMPRESSED_YUY2, 800, 480, 30}, \
                     {UVCL_PAYLOAD_FB_H264,          1280, 720, 30}};
#define MAX_IMG_FRAME_SIZE (1280 * 720 * 2)
#else
/* Nucleo (no external PSRAM): single H264 stream at 480x272@30fps.
 * Capture path uses YUY2 (proven working). The VENC extracts luma and uses
 * a static grey UV plane to produce a black-and-white H264 stream. */
#define IMG_STREAMS {{UVCL_PAYLOAD_FB_H264, 480, 272, 30}};
#define MAX_IMG_FRAME_SIZE (480 * 272 * 2)
#endif
#else
#ifndef MAX_IMG_FRAME_SIZE
#error MAX_IMG_FRAME_SIZE must be define
#endif
#endif

/* Max compressed H264 frame size (worst-case) for 480x272@30fps. 64 KB is
 * very comfortable: typical I-frame at the VBR settings used in app_enc.c is
 * well below 20 KB. */
#ifndef H264_OUT_BUFFER_SIZE
#define H264_OUT_BUFFER_SIZE (64 * 1024)
#endif

#endif
