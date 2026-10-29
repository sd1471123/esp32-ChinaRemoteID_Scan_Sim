#pragma once
#include <stdio.h>

// 主机测试用日志：全部打到 stderr，不参与断言
#define ESP_LOGI(tag, fmt, ...) fprintf(stderr, "[I %s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) fprintf(stderr, "[W %s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) fprintf(stderr, "[E %s] " fmt "\n", tag, ##__VA_ARGS__)
