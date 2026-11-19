"""内存优化与诊断的源码合约检查，不调用编译器或硬件。"""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def section(source, start, end):
    return source.split(start, 1)[1].split(end, 1)[0]


class MemoryOptimizationContractTest(unittest.TestCase):
    def test_h264_decoder_and_iram_are_explicitly_disabled_in_both_configs(self):
        for path in ("sdkconfig", "sdkconfig.defaults"):
            config = read(path)
            for key in ("VIDEO_DECODER_SW_H264_SUPPORT", "ESP_H264_DECODER_IRAM"):
                self.assertIn(f"# CONFIG_{key} is not set", config)
                self.assertNotIn(f"CONFIG_{key}=y", config)
        self.assertNotIn(
            "# default:\n# CONFIG_VIDEO_DECODER_SW_H264_SUPPORT is not set",
            read("sdkconfig"),
        )

    def test_mjpeg_decoders_and_player_format_guards_remain_enabled(self):
        config = read("sdkconfig")
        self.assertIn("CONFIG_VIDEO_DECODER_SW_MJPEG_SUPPORT=y", config)
        self.assertIn("CONFIG_VIDEO_DECODER_HW_MJPEG_SUPPORT=y", config)
        player = read("main/app_video_player.c")
        self.assertIn("video->codec != APP_AVI_VIDEO_CODEC_MJPEG", player)
        self.assertIn("video->codec != APP_MP4_VIDEO_CODEC_MJPEG", player)
        self.assertIn(".codec_type = ESP_VIDEO_CODEC_TYPE_MJPEG", player)

    def test_photo_response_uses_psram_and_keeps_failure_cleanup(self):
        app = read("main/esp_xiaozhi_chat_app.c")
        response = section(app, "char *response = heap_caps_calloc(",
                           "size_t response_len = 0;")
        self.assertIn("1, 4096, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT", response)
        self.assertIn("if (response == NULL)", response)
        self.assertIn("app_camera_release_jpeg(jpeg)", response)
        self.assertIn("return esp_mcp_value_create_bool(false)", response)

    def test_memory_module_is_in_build_and_header_supports_cpp(self):
        self.assertIn('"app_memory.c"', read("main/CMakeLists.txt"))
        header = read("main/app_memory.h")
        self.assertIn('extern "C"', header)
        self.assertIn("int64_t *last_sample_us", header)

    def test_heap_logs_capability_specific_low_water_and_largest_block(self):
        memory = read("main/app_memory.c")
        for metric in ("total_free_bytes", "minimum_free_bytes", "largest_free_block"):
            self.assertIn(f"info.{metric}", memory)
        for region in ('"internal"', '"internal_dma"', '"psram"'):
            self.assertIn(region, memory)
        self.assertIn("MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT", memory)
        self.assertIn("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT", memory)

    def test_diagnostics_do_not_allocate_or_suspend_the_scheduler(self):
        memory = read("main/app_memory.c")
        self.assertNotRegex(memory, r"\b(?:malloc|calloc|realloc|heap_caps_malloc|xTaskCreate\w*)\s*\(")
        self.assertNotIn("uxTaskGetSystemState(", memory)
        self.assertNotIn("vTaskSuspendAll(", memory)
        self.assertIn("uxTaskGetStackHighWaterMark(NULL)", memory)
        self.assertIn("pcTaskGetName(NULL)", memory)
        self.assertIn("xTaskGetStackStart(NULL)", memory)
        self.assertNotRegex(memory, r"uxTaskGetStackHighWaterMark\(NULL\)\s*\*")
        self.assertIn("#if INCLUDE_uxTaskGetStackHighWaterMark == 1", memory)

    def test_periodic_interval_is_consistent_and_can_be_disabled(self):
        for path in ("sdkconfig", "sdkconfig.defaults"):
            self.assertIn("CONFIG_APP_MEMORY_DIAGNOSTICS_INTERVAL_SECONDS=60", read(path))
        self.assertIn("range 0 3600", read("main/Kconfig"))
        memory = read("main/app_memory.c")
        self.assertIn("#if CONFIG_APP_MEMORY_DIAGNOSTICS_INTERVAL_SECONDS > 0", memory)
        self.assertIn("now - *last_sample_us < interval_us", memory)
        self.assertIn("*last_sample_us = now == 0 ? 1 : now", memory)

    def test_lifecycle_timeout_only_samples_and_does_not_restart_ai(self):
        worker = section(read("main/esp_xiaozhi_chat_app.c"),
                         "static void xiaozhi_runtime_task(void *arg)\n{",
                         "void esp_xiaozhi_chat_app_notify_language_changed(void)")
        timeout = section(worker,
                          "if (ulTaskNotifyTake(pdTRUE, diagnostics_wait) == 0) {",
                          'app_memory_log_heap("runtime_before");')
        self.assertIn('app_memory_log_heap("periodic")', timeout)
        self.assertIn('app_memory_log_current_task("periodic")', timeout)
        self.assertIn("continue;", timeout)
        self.assertNotIn("xiaozhi_runtime_enable", timeout)
        self.assertNotIn("xiaozhi_runtime_disable", timeout)
        self.assertNotIn("s_runtime_state =", timeout)
        self.assertIn(": portMAX_DELAY", worker)
        self.assertIn('app_memory_log_heap("runtime_after")', worker)

    def test_page_diagnostics_run_after_releasing_lvgl_lock(self):
        manager = read("main/ui_page_manager.c")
        block = section(manager, 'ESP_LOGI(TAG, "switched to page',
                        "void ui_page_manager_allow_face_follow_once(void)")
        self.assertLess(block.index("lvgl_port_unlock()"),
                        block.index("app_memory_log_heap("))
        self.assertIn('app_memory_log_current_task("page_switch")', block)

    def test_existing_workers_sample_their_own_stack(self):
        for path, checkpoint in (
            ("main/battery_monitor.c", "battery_sample"),
            ("main/ui_touch.c", "touch_dispatch"),
            ("main/http_preview.c", "camera_snapshot"),
            ("main/http_preview.c", "camera_stream"),
            ("main/motion_web_server.cpp", "motion_http"),
        ):
            self.assertIn(f'app_memory_log_current_task_periodic("{checkpoint}"', read(path))
        battery = section(read("main/battery_monitor.c"),
                          "static void battery_protection_task(void *arg)",
                          "static void battery_display_task(void *arg)")
        self.assertNotIn("app_memory_log_", battery)

    def test_routine_lcd_and_battery_logs_do_not_hide_sram_diagnostics(self):
        display = read("main/esp_xiaozhi_chat_display.c")
        battery = read("main/battery_monitor.c")
        self.assertRegex(display, r'ESP_LOGD\(TAG,\s*"LCD perf:')
        self.assertRegex(battery, r'ESP_LOGD\(TAG,\s*"USB输入：')
        self.assertRegex(battery, r'ESP_LOGD\(TAG,\s*"valid=%d USB=')
        self.assertRegex(battery, r'ESP_LOGW\(TAG,\s*"battery sample failed')
        self.assertRegex(read("main/app_memory.c"), r'ESP_LOGI\(TAG,\s*"%s %s: free=')
        self.assertIn('static const char *TAG = "APP_MEMORY"', read("main/app_memory.c"))

    def test_cache_disabled_stacks_and_memory_safety_settings_are_preserved(self):
        app = read("main/esp_xiaozhi_chat_app.c")
        runtime_create = section(app, "BaseType_t runtime_task_ret =",
                                 "if (runtime_task_ret != pdPASS)")
        self.assertIn("MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT", runtime_create)
        config = read("sdkconfig")
        for expected in (
            "CONFIG_ESP_WIFI_EXTRA_IRAM_OPT=y",
            "CONFIG_ESP_WIFI_SLP_IRAM_OPT=y",
            "CONFIG_ESP_PHY_DISABLE_PLL_TRACK=y",
            "CONFIG_ESP_WIFI_STATIC_TX_BUFFER_NUM=8",
            "# CONFIG_ESP_SYSTEM_ALLOW_RTC_FAST_MEM_AS_HEAP is not set",
            "CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY=y",
        ):
            self.assertIn(expected, config)
        for path in ("main/http_preview.c", "main/motion_web_server.cpp"):
            self.assertIn("config.stack_size = 8192", read(path))


if __name__ == "__main__":
    unittest.main()
