#pragma once
#include "FreeRTOS.h"

QueueHandle_t xQueueCreate(unsigned length, unsigned item_size);
int xQueueSend(QueueHandle_t q, const void *item, TickType_t wait);
int xQueueReceive(QueueHandle_t q, void *item, TickType_t wait);
void vQueueDelete(QueueHandle_t q);
