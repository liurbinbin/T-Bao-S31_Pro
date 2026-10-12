#include <string.h>

#include "esp_board_device.h"
#include "esp_err.h"
#include "esp_io_expander.h"
#include "esp_io_expander_xl9555.h"
#include "esp_lcd_st7796.h"
#include "esp_lcd_touch_ft6x36.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "TBAO_S31_SETUP";

#define TBAO_XL_P00_TOUCH_INT (1U << 0)
#define TBAO_XL_P01_TOUCH_RST (1U << 1)

esp_err_t io_expander_factory_entry_t(i2c_master_bus_handle_t i2c_handle,
                                      const uint16_t dev_addr,
                                      esp_io_expander_handle_t *handle_ret)
{
    return esp_io_expander_new_i2c_xl9555(i2c_handle, dev_addr, handle_ret);
}

esp_err_t lcd_panel_factory_entry_t(esp_lcd_panel_io_handle_t io,
                                     const esp_lcd_panel_dev_config_t *panel_dev_config,
                                     esp_lcd_panel_handle_t *ret_panel)
{
    esp_lcd_panel_dev_config_t panel_config = {0};
    memcpy(&panel_config, panel_dev_config, sizeof(panel_config));
    esp_err_t ret = esp_lcd_new_panel_st7796(io, &panel_config, ret_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "create ST7796S panel failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t reset_touch_from_xl9555(void)
{
    esp_io_expander_handle_t *expander = NULL;
    esp_err_t ret = esp_board_device_get_handle("gpio_expander", (void **)&expander);
    if (ret != ESP_OK || expander == NULL || *expander == NULL) {
        ESP_LOGE(TAG, "get XL9555 handle failed: %s", esp_err_to_name(ret));
        return ret == ESP_OK ? ESP_ERR_INVALID_STATE : ret;
    }

    ESP_RETURN_ON_ERROR(esp_io_expander_set_dir(*expander, TBAO_XL_P00_TOUCH_INT,
                                                IO_EXPANDER_INPUT), TAG,
                        "configure touch interrupt");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_dir(*expander, TBAO_XL_P01_TOUCH_RST,
                                                IO_EXPANDER_OUTPUT), TAG,
                        "configure touch reset");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(*expander, TBAO_XL_P01_TOUCH_RST, 1),
                        TAG, "release touch reset");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(*expander, TBAO_XL_P01_TOUCH_RST, 0),
                        TAG, "assert touch reset");
    vTaskDelay(pdMS_TO_TICKS(15));
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(*expander, TBAO_XL_P01_TOUCH_RST, 1),
                        TAG, "release touch reset");
    vTaskDelay(pdMS_TO_TICKS(300));
    return ESP_OK;
}

esp_err_t lcd_touch_factory_entry_t(esp_lcd_panel_io_handle_t io,
                                    const esp_lcd_touch_config_t *touch_dev_config,
                                    esp_lcd_touch_handle_t *ret_touch)
{
    esp_err_t ret = reset_touch_from_xl9555();
    if (ret != ESP_OK) {
        return ret;
    }

    esp_lcd_touch_config_t touch_config = {0};
    memcpy(&touch_config, touch_dev_config, sizeof(touch_config));
    touch_config.rst_gpio_num = GPIO_NUM_NC;
    touch_config.int_gpio_num = GPIO_NUM_NC;
    ret = esp_lcd_touch_new_i2c_ft6x36(io, &touch_config, ret_touch);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "create FT6336/FT6336U touch failed: %s", esp_err_to_name(ret));
    }
    return ret;
}
