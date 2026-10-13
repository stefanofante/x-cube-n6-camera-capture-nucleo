/**
  ******************************************************************************
  * @file    console.c
  * @author  MDG Application Team
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the ST_LICENSE.md file
  * in the root directory of this software component.
  * If no ST_LICENSE.md file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

#include "main.h"
#include "app_fault.h"
#include "FreeRTOS.h"
#include "task.h"
#include <errno.h>
#include <unistd.h>

extern UART_HandleTypeDef huart1;

/* Logging is bounded and best-effort. Only the low-priority consumer owns
 * UART; producers cannot delay capture for an entire diagnostic line.
 * No DMA channel or dynamic allocation is required. */
enum { CONSOLE_CAPACITY = 1024, CONSOLE_CHUNK = 64 };
static char ring[CONSOLE_CAPACITY];
static size_t read_index, write_index, used;
volatile uint32_t console_dropped_bytes;
static StaticTask_t console_task;
static StackType_t console_stack[512];
static TaskHandle_t console_handle;

static void console_run(void *argument)
{
    (void)argument;
    char chunk[CONSOLE_CHUNK];
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        for (;;) {
            taskENTER_CRITICAL();
            const size_t count = used < sizeof(chunk) ? used : sizeof(chunk);
            for (size_t i = 0; i < count; ++i) {
                chunk[i] = ring[read_index];
                read_index = (read_index + 1U) % CONSOLE_CAPACITY;
            }
            used -= count;
            taskEXIT_CRITICAL();
            if (!count) break;
            if (HAL_UART_Transmit(&huart1, (uint8_t *)chunk, (uint16_t)count, 20U) != HAL_OK) {
                taskENTER_CRITICAL();
                console_dropped_bytes += (uint32_t)count;
                taskEXIT_CRITICAL();
            }
        }
    }
}

void CONSOLE_Start(void)
{
    console_handle = xTaskCreateStatic(console_run, "console", 512, NULL,
                                       tskIDLE_PRIORITY + 1U, console_stack, &console_task);
    APP_REQUIRE(console_handle != NULL);
}

int _write(int file, char *pointer, int length)
{
    if (file != STDOUT_FILENO && file != STDERR_FILENO) {
        errno = EBADF;
        return -1;
    }
    if (length < 0 || !pointer || !console_handle || __get_IPSR()) {
        errno = EINVAL;
        return -1;
    }
    taskENTER_CRITICAL();
    const size_t room = CONSOLE_CAPACITY - used;
    const size_t count = (size_t)length < room ? (size_t)length : room;
    for (size_t i = 0; i < count; ++i) {
        ring[write_index] = pointer[i];
        write_index = (write_index + 1U) % CONSOLE_CAPACITY;
    }
    used += count;
    console_dropped_bytes += (uint32_t)((size_t)length - count);
    taskEXIT_CRITICAL();
    xTaskNotifyGive(console_handle);
    /* Treat discarded diagnostic bytes as handled so stdio cannot retry in
     * the producer and defeat the bounded, lossy logging contract. */
    return length;
}
