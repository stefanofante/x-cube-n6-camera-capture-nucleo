#ifndef TEST_STM32_HAL_H
#define TEST_STM32_HAL_H
#include <stdint.h>
#include "cmsis_compiler.h"
/* Declarations only: host tests link no simulated peripheral initialization. */
typedef struct { int unused; } PCD_TypeDef;
typedef struct { PCD_TypeDef *Instance; struct {
    int dev_endpoints, speed, dma_enable, phy_itface, Sof_enable, low_power_enable,
        lpm_enable, vbus_sensing_enable, use_dedicated_ep1, use_external_vbus;
} Init; } PCD_HandleTypeDef;
typedef struct { int OscillatorType, HSEState; } RCC_OscInitTypeDef;
#define RCC_OSCILLATORTYPE_HSE 1
#define RCC_HSE_ON 1
#define ENABLE 1
#define DISABLE 0
#define PCD_SPEED_HIGH 1
#define USB_OTG_HS_EMBEDDED_PHY 1
uint32_t HAL_GetTick(void);
int HAL_RCC_OscConfig(RCC_OscInitTypeDef *config);
int HAL_PCD_Init(PCD_HandleTypeDef *pcd);
int HAL_PCD_Start(PCD_HandleTypeDef *pcd);
void HAL_PCD_IRQHandler(PCD_HandleTypeDef *pcd);
void HAL_PCDEx_SetRxFiFo(PCD_HandleTypeDef *pcd, int size);
void HAL_PCDEx_SetTxFiFo(PCD_HandleTypeDef *pcd, int fifo, int size);
#endif
