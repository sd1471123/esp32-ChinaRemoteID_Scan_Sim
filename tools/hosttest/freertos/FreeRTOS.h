#pragma once
#include <stdint.h>
#include <stddef.h>

typedef void *QueueHandle_t;
typedef void *SemaphoreHandle_t;
typedef void *TaskHandle_t;
typedef uint32_t TickType_t;

#define portMAX_DELAY        0xFFFFFFFFu
#define pdTRUE               1
#define pdFALSE              0
#define pdPASS               1
#define pdMS_TO_TICKS(x)     (x)
#define tskIDLE_PRIORITY     0
