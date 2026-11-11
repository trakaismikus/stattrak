#ifndef _ESP_LOG_H
#define _ESP_LOG_H


#define ESP_LOGI(tag, fmt, ...)   printf("I (%lu) %s: " fmt "\n", 0UL, tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...)   printf("E (%lu) %s: " fmt "\n", 0UL, tag, ##__VA_ARGS__)


#endif // _ESP_LOG_H