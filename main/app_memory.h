/*
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 仅在普通任务上下文调用，不在 ISR、临界区或持有实时保护锁时采样。 */
void app_memory_log_heap(const char *checkpoint);

/* 仅扫描当前任务，名称和栈在采样期间始终有效；剩余栈单位为字节。 */
void app_memory_log_current_task(const char *checkpoint);

/* 每个任务独占一个初始为 0 的时间戳；间隔设为 0 时关闭周期采样。 */
void app_memory_log_current_task_periodic(const char *checkpoint,
                                         int64_t *last_sample_us);

#ifdef __cplusplus
}
#endif
