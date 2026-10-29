#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#define ESP_PARTITION_TYPE_DATA 0x01

typedef struct {
    uint32_t    size;
    uint32_t    address;
    const char *label;
} esp_partition_t;

const esp_partition_t *esp_partition_find_first(uint8_t type, uint8_t subtype,
                                                const char *label);
esp_err_t esp_partition_read(const esp_partition_t *part, size_t offset,
                             void *dst, size_t size);
esp_err_t esp_partition_write(const esp_partition_t *part, size_t offset,
                              const void *src, size_t size);
esp_err_t esp_partition_erase_range(const esp_partition_t *part, size_t offset,
                                    size_t size);
