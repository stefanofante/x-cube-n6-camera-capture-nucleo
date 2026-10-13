 /**
 ******************************************************************************
 * @file    app_cam.c
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
#include <assert.h>
#include "cmw_camera.h"
#include "app_cam.h"
#include "app_config.h"
#include "stm32n6xx.h"
#include "utils.h"
#include "app.h"

/* Define sensor orientation */
#if CAMERA_SELFY == 1
#define SENSOR_IMX335_FLIP CMW_MIRRORFLIP_MIRROR
#define SENSOR_VD66GY_FLIP CMW_MIRRORFLIP_FLIP
#define SENSOR_VD55G1_FLIP CMW_MIRRORFLIP_FLIP
#define SENSOR_VD1943_FLIP CMW_MIRRORFLIP_MIRROR
#else
#define SENSOR_IMX335_FLIP CMW_MIRRORFLIP_NONE
#define SENSOR_VD66GY_FLIP CMW_MIRRORFLIP_FLIP_MIRROR
#define SENSOR_VD55G1_FLIP CMW_MIRRORFLIP_FLIP_MIRROR
#define SENSOR_VD1943_FLIP CMW_MIRRORFLIP_NONE
#endif

/* Define sensor width x height size. 0x0 means full frame */
#define SENSOR_WIDTH     0
#define SENSOR_HEIGHT    0

static const char *sensor_names[] = {
  "CMW_UNKNOWN",
  "CMW_VD66GY",
  "CMW_IMX335",
  "CMW_VD55G1",
  "CMW_VD1943",
};

static CMW_Sensor_Name_t sensor;
static int is_sensor_valid = 0;

static int CAM_getFlipMode(CMW_Sensor_Name_t sensor)
{
  int sensor_mirror_flip = CMW_MIRRORFLIP_NONE;
  int sensor_name_idx = 0;

  switch (sensor) {
  case CMW_VD66GY_Sensor:
    sensor_mirror_flip = SENSOR_VD66GY_FLIP;
    sensor_name_idx = 1;
    break;
  case CMW_IMX335_Sensor:
    sensor_mirror_flip = SENSOR_IMX335_FLIP;
    sensor_name_idx = 2;
    break;
  case CMW_VD55G1_Sensor:
    sensor_mirror_flip = SENSOR_VD55G1_FLIP;
    sensor_name_idx = 3;
    break;
  case CMW_VD1943_Sensor:
    sensor_mirror_flip = SENSOR_VD1943_FLIP;
    sensor_name_idx = 4;
    break;
  default:
    return -1;
  }
  printf("Detected %s\n", sensor_names[sensor_name_idx]);

  return sensor_mirror_flip;
}

static int CAM_FormatToBpp(int dcmipp_output_format)
{
  int bpp = 0;

  switch (dcmipp_output_format)
  {
  case DCMIPP_PIXEL_PACKER_FORMAT_MONO_Y8_G8_1:
    bpp = 1;
    break;
  case DCMIPP_PIXEL_PACKER_FORMAT_RGB565_1:
  case DCMIPP_PIXEL_PACKER_FORMAT_YUV422_1:
    bpp = 2;
    break;
  case DCMIPP_PIXEL_PACKER_FORMAT_RGB888_YUV444_1:
    bpp = 3;
    break;
  default:
    return 0;
  }

  return bpp;
}

/* Keep display output aspect ratio using crop area */
static int CAM_InitCropConfig(CMW_Manual_roi_area_t *roi, int sensor_width, int sensor_height, CAM_conf_t *conf)
{
  const float ratiox = (float)sensor_width / conf->capture_width;
  const float ratioy = (float)sensor_height / conf->capture_height;
  const float ratio = MIN(ratiox, ratioy);

  if (ratio < 1 || ratio >= 64)
    return CMW_ERROR_WRONG_PARAM;

  roi->width = (uint32_t) MIN(conf->capture_width * ratio, sensor_width);
  roi->height = (uint32_t) MIN(conf->capture_height * ratio, sensor_height);
  roi->offset_x = (sensor_width - roi->width + 1) / 2;
  roi->offset_y = (sensor_height - roi->height + 1) / 2;
  return CMW_ERROR_NONE;
}

static int CAM_EnableYuv(uint32_t Pipe)
{
  DCMIPP_ColorConversionConfTypeDef color_conf = {
    .ClampOutputSamples = ENABLE,
    .OutputSamplesType = DCMIPP_CLAMP_YUV,
    .RR = 131, .RG = -119, .RB = -12, .RA = 128,
    .GR =  55, .GG =  183, .GB =  18, .GA =   0,
    .BR = -30, .BG = -101, .BB = 131, .BA = 128,
  };
  int ret;

  /* only pipe1 can do yuv */
  if (Pipe != DCMIPP_PIPE1) return CMW_ERROR_WRONG_PARAM;
  ret = HAL_DCMIPP_PIPE_SetYUVConversionConfig(CMW_CAMERA_GetDCMIPPHandle(), Pipe, &color_conf);
  if (ret != HAL_OK) return CMW_ERROR_PERIPH_FAILURE;
  ret = HAL_DCMIPP_PIPE_EnableYUVConversion(CMW_CAMERA_GetDCMIPPHandle(), Pipe);
  return ret == HAL_OK ? CMW_ERROR_NONE : CMW_ERROR_PERIPH_FAILURE;
}

static int DCMIPP_PipeInitCapture(CAM_conf_t *cam_conf, int sensor_width, int sensor_height, CAM_conf_t *conf)
{
  CMW_DCMIPP_Conf_t dcmipp_conf = {0};
  uint32_t hw_pitch;
  int ret;

  if (conf->capture_width < conf->capture_height) return CMW_ERROR_WRONG_PARAM;

  dcmipp_conf.output_width = conf->capture_width;
  dcmipp_conf.output_height = conf->capture_height;
  dcmipp_conf.output_format = cam_conf->dcmipp_output_format;
  dcmipp_conf.output_bpp = CAM_FormatToBpp(cam_conf->dcmipp_output_format);
  if (!dcmipp_conf.output_bpp) return CMW_ERROR_WRONG_PARAM;
  dcmipp_conf.mode = CMW_Aspect_ratio_manual_roi;
  dcmipp_conf.enable_swap = cam_conf->is_rgb_swap;
  dcmipp_conf.enable_gamma_conversion = 0;
  ret = CAM_InitCropConfig(&dcmipp_conf.manual_conf, sensor_width, sensor_height, conf);
  if (ret != CMW_ERROR_NONE) return ret;
  ret = CMW_CAMERA_SetPipeConfig(DCMIPP_PIPE1, &dcmipp_conf, &hw_pitch);
  if (ret != HAL_OK) return CMW_ERROR_PERIPH_FAILURE;
  if (hw_pitch != dcmipp_conf.output_width * dcmipp_conf.output_bpp)
    return CMW_ERROR_WRONG_PARAM;

  if (cam_conf->dcmipp_output_format == DCMIPP_PIXEL_PACKER_FORMAT_YUV422_1)
    return CAM_EnableYuv(DCMIPP_PIPE1);
  return CMW_ERROR_NONE;
}

int CAM_Init(CAM_conf_t *conf)
{
  CMW_CameraInit_t cam_conf = {0};
  int ret;

  if (!conf || conf->capture_width <= 0 || conf->capture_height <= 0 || conf->fps <= 0 ||
      (uint64_t)conf->capture_width * conf->capture_height * 2U > MAX_IMG_FRAME_SIZE)
    return CMW_ERROR_WRONG_PARAM;
  if (!is_sensor_valid) {
    ret = CMW_CAMERA_GetSensorName(&sensor);
    if (ret != CMW_ERROR_NONE) return ret;
    is_sensor_valid = 1;
  }

  cam_conf.width = SENSOR_WIDTH;
  cam_conf.height = SENSOR_HEIGHT;
  cam_conf.fps = conf->fps;
  cam_conf.mirror_flip = CAM_getFlipMode(sensor);
  if (cam_conf.mirror_flip < 0) return CMW_ERROR_WRONG_PARAM;

  ret = CMW_CAMERA_Init(&cam_conf, NULL);
  if (ret != CMW_ERROR_NONE) return ret;

  /* CMW_CAMERA_Init update width height */
  if (!cam_conf.width || !cam_conf.height) return CMW_ERROR_WRONG_PARAM;
  ret = DCMIPP_PipeInitCapture(conf, cam_conf.width, cam_conf.height, conf);
  if (ret != CMW_ERROR_NONE) return ret;
  /* The controller keeps cameraActive false throughout initialization.
   * Clear old completion flags before the new generation can receive IRQs. */
  __HAL_DCMIPP_CLEAR_FLAG(CMW_CAMERA_GetDCMIPPHandle(),
      DCMIPP_FLAG_PIPE1_FRAME | DCMIPP_FLAG_PIPE1_VSYNC | DCMIPP_FLAG_PIPE1_OVR);
  return CMW_ERROR_NONE;
}

int CAM_CapturePipe_Start(uint8_t *capture_pipe_dst, uint32_t cam_mode)
{
  int ret;

  ret = CMW_CAMERA_Start(DCMIPP_PIPE1, capture_pipe_dst, cam_mode);
  return ret;
}

int CAM_IspUpdate(void)
{
  int ret;

  ret = CMW_CAMERA_Run();
  return ret;
}

int CAM_Deinit()
{
  int ret;

  ret = CMW_CAMERA_DeInit();
  is_sensor_valid = 0;
  return ret;
}

void CMW_CAMERA_PIPE_ErrorCallback(uint32_t pipe)
{
  /* Keep the ISR bounded: the stream controller performs recovery in task
   * context after the camera worker has released the lifecycle mutex. */
  if (pipe == DCMIPP_PIPE1) App_CameraErrorFromISR();
}
