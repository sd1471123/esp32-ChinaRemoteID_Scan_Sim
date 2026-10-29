#pragma once
#include "FreeRTOS.h"

int xTaskCreate(void (*fn)(void *), const char *name, unsigned stack,
                void *arg, unsigned prio, TaskHandle_t *handle);
