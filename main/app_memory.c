/*
 * SPDX-License-Identifier: Apache-2.0
 */
#include "app_memory.h"

#include "sdkconfig.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

static const char *TAG = "APP_MEMORY";

static void log_heap_region(const char *checkpoint, const char *region,
                            uint32_t caps)
{
    multi_heap_info_t info = {0};
    heap_caps_get_info(&info, caps);
    if ((caps & MALLOC_CAP_INTERNAL) != 0 &&
            info.largest_free_block < 2048) {
        ESP_LOGW(TAG, "%s %s: free=%u min=%u largest=%u B",
                 checkpoint, region, (unsigned)info.total_free_bytes,
                 (unsigned)info.minimum_free_bytes,
                 (unsigned)info.largest_free_block);
    } else {
        ESP_LOGI(TAG, "%s %s: free=%u min=%u largest=%u B",
                 checkpoint, region, (unsigned)info.total_free_bytes,
                 (unsigned)info.minimum_free_bytes,
                 (unsigned)info.largest_free_block);
    }
}

void app_memory_log_heap(const char *checkpoint)
{
    /* DMA 是内部堆的子集，不可把这两行的 free 相加作为总容量。 */
    log_heap_region(checkpoint, "internal",
                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    log_heap_region(checkpoint, "internal_dma",
                    MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    log_heap_region(checkpoint, "psram",
                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void app_memory_log_current_task(const char *checkpoint)
{
#if INCLUDE_uxTaskGetStackHighWaterMark == 1
    /* 不保留其他任务的 handle/name，也不暂停全系统来扫描全部栈。
     * ESP-IDF 返回字节数，不乘 sizeof(StackType_t)。 */
    const unsigned free_min = (unsigned)uxTaskGetStackHighWaterMark(NULL);
    const char *region = esp_ptr_external_ram(xTaskGetStackStart(NULL))
        ? "psram" : "internal";
    if (free_min < 768) {
        ESP_LOGW(TAG, "%s task=%s stack=%s stack_free_min=%u B",
                 checkpoint, pcTaskGetName(NULL), region, free_min);
    } else {
        ESP_LOGI(TAG, "%s task=%s stack=%s stack_free_min=%u B",
                 checkpoint, pcTaskGetName(NULL), region, free_min);
    }
#else
    (void)checkpoint;
#endif
}

void app_memory_log_current_task_periodic(const char *checkpoint,
                                         int64_t *last_sample_us)
{
#if CONFIG_APP_MEMORY_DIAGNOSTICS_INTERVAL_SECONDS > 0
    const int64_t now = esp_timer_get_time();
    const int64_t interval_us =
        (int64_t)CONFIG_APP_MEMORY_DIAGNOSTICS_INTERVAL_SECONDS * 1000000;
    if (*last_sample_us != 0 && now - *last_sample_us < interval_us) {
        return;
    }
    *last_sample_us = now == 0 ? 1 : now;
    app_memory_log_current_task(checkpoint);
#else
    (void)checkpoint;
    (void)last_sample_us;
#endif
}
