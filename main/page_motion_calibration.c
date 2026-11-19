/* SPDX-License-Identifier: Apache-2.0 */
#include "page_motion_calibration.h"

#include <stdint.h>
#include "actuator_control.h"
#include "app_language.h"
#include "motion_manager.h"
#include "ui_page_manager.h"
#include "ui_page_style.h"

LV_FONT_DECLARE(font_puhui_16_4);
LV_FONT_DECLARE(font_puhui_20_4);

#define CALIBRATION_REFRESH_MS 50U
#define CALIBRATION_TEST_TTL_MS 120U
#define CALIBRATION_WHEEL_TEST_MAX_MS 1500U
#define CALIBRATION_SERVO_TEST_MAX_MS 3000U

static lv_obj_t *s_root;
static lv_obj_t *s_overlay;
static lv_obj_t *s_content;
static lv_obj_t *s_notice;
static lv_obj_t *s_servo_button;
static lv_obj_t *s_servo_slider;
static lv_obj_t *s_servo_value;
static lv_obj_t *s_wheel_labels[3];
static lv_obj_t *s_confirm_popup;
static motion_calibration_t s_draft;
static motion_calibration_t s_original;
static motion_calibration_closed_cb_t s_closed;
static bool s_root_was_scrollable;
static bool s_servo_tab;
static bool s_edit_high;
static bool s_testing;
static bool s_auto_servo_preview;
static bool s_wheel_prompt_done;
static bool s_servo_prompt_done;
static bool s_any_change_saved;
static motion_request_t s_test_request;
static uint32_t s_test_started_ms;
static uint32_t s_test_last_ms;
static uint32_t s_test_limit_ms;

typedef enum {
    CALIBRATION_PROMPT_NONE = 0,
    CALIBRATION_PROMPT_WHEELS,
    CALIBRATION_PROMPT_SERVO,
} calibration_prompt_t;

static calibration_prompt_t s_prompt_kind;

static void build_content(void);

static void notice(const char *zh, const char *en)
{
    if (s_notice != NULL) {
        lv_label_set_text(s_notice, app_language_text(zh, en));
    }
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                            int y, int height)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, LV_HOR_RES - 32);
    lv_obj_set_height(label, height);
    lv_obj_set_pos(label, 16, y);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, &font_puhui_16_4, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xD5D9E0), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(label, text);
    return label;
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text,
                             int x, int y, int width, int height,
                             lv_event_cb_t callback, intptr_t value,
                             lv_event_code_t event_code)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, height);
    ui_page_style_button(button, 0x234A63, 0x337CA5, 9);
    lv_obj_t *label = lv_label_create(button);
    lv_obj_set_style_text_font(label, &font_puhui_16_4, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, callback, event_code, (void *)value);
    return label;
}

static lv_obj_t *make_button_object(lv_obj_t *parent, const char *text,
                                    int x, int y, int width, int height,
                                    uint32_t color, uint32_t pressed_color,
                                    uint16_t radius, lv_event_cb_t callback)
{
    lv_obj_t *button = lv_btn_create(parent);
    if (button == NULL) {
        return NULL;
    }
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, height);
    ui_page_style_button(button, color, pressed_color, radius);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(button);
    if (label != NULL) {
        lv_obj_set_style_text_font(label, &font_puhui_16_4, 0);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(label, text);
        lv_obj_center(label);
    }
    return button;
}

static bool wheel_config_changed(void)
{
    return s_draft.reverse_a != s_original.reverse_a ||
           s_draft.reverse_b != s_original.reverse_b ||
           s_draft.swap_motors != s_original.swap_motors;
}

static bool servo_config_changed(void)
{
    return s_draft.servo_low_us != s_original.servo_low_us ||
           s_draft.servo_high_us != s_original.servo_high_us;
}

static void stop_test(void)
{
    s_testing = false;
    s_auto_servo_preview = false;
    if (s_servo_button != NULL) {
        lv_obj_clear_state(s_servo_button, LV_STATE_DISABLED);
    }
    if (s_overlay != NULL) {
        motion_manager_stop_all();
    }
}

static void close_overlay(bool saved)
{
    if (s_overlay == NULL) {
        return;
    }
    s_testing = false;
    s_auto_servo_preview = false;
    if (s_confirm_popup != NULL) {
        lv_obj_del(s_confirm_popup);
        s_confirm_popup = NULL;
    }
    s_prompt_kind = CALIBRATION_PROMPT_NONE;
    if (!saved) {
        (void)motion_manager_calibration_finish(false);
    }
    /* 保存完成后其他来源已恢复，不再调用全局急停去清除新到的命令。 */
    lv_obj_del(s_overlay);
    s_overlay = NULL;
    s_content = NULL;
    s_notice = NULL;
    s_servo_button = NULL;
    s_servo_slider = NULL;
    s_servo_value = NULL;
    s_wheel_labels[0] = NULL;
    s_wheel_labels[1] = NULL;
    s_wheel_labels[2] = NULL;
    if (s_root_was_scrollable) {
        lv_obj_add_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    }
    s_root = NULL;
    motion_calibration_closed_cb_t callback = s_closed;
    s_closed = NULL;
    if (callback != NULL) {
        callback(saved);
    }
}

void page_motion_calibration_close(void)
{
    close_overlay(false);
}

bool page_motion_calibration_is_open(void)
{
    return s_overlay != NULL;
}

static void finalize_exit(void);
static void advance_exit_prompt(void);

static void confirm_save_cb(lv_event_t *event)
{
    (void)event;
    const calibration_prompt_t prompt = s_prompt_kind;
    if (s_confirm_popup != NULL) {
        lv_obj_del(s_confirm_popup);
        s_confirm_popup = NULL;
    }
    s_prompt_kind = CALIBRATION_PROMPT_NONE;
    if (prompt == CALIBRATION_PROMPT_WHEELS) {
        s_wheel_prompt_done = true;
    } else if (prompt == CALIBRATION_PROMPT_SERVO) {
        s_servo_prompt_done = true;
    }
    s_any_change_saved = true;
    advance_exit_prompt();
}

static void confirm_cancel_cb(lv_event_t *event)
{
    (void)event;
    const calibration_prompt_t prompt = s_prompt_kind;
    if (s_confirm_popup != NULL) {
        lv_obj_del(s_confirm_popup);
        s_confirm_popup = NULL;
    }
    s_prompt_kind = CALIBRATION_PROMPT_NONE;
    if (prompt == CALIBRATION_PROMPT_WHEELS) {
        s_draft.reverse_a = s_original.reverse_a;
        s_draft.reverse_b = s_original.reverse_b;
        s_draft.swap_motors = s_original.swap_motors;
        s_wheel_prompt_done = true;
    } else if (prompt == CALIBRATION_PROMPT_SERVO) {
        s_draft.servo_low_us = s_original.servo_low_us;
        s_draft.servo_high_us = s_original.servo_high_us;
        s_servo_prompt_done = true;
    }
    advance_exit_prompt();
}

static void show_exit_prompt(calibration_prompt_t prompt)
{
    if (s_overlay == NULL || s_confirm_popup != NULL) {
        return;
    }
    s_prompt_kind = prompt;
    s_confirm_popup = lv_obj_create(s_overlay);
    if (s_confirm_popup == NULL) {
        s_prompt_kind = CALIBRATION_PROMPT_NONE;
        return;
    }
    lv_obj_set_pos(s_confirm_popup, 0, 0);
    lv_obj_set_size(s_confirm_popup, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_confirm_popup, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_confirm_popup, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_confirm_popup, 0, 0);
    lv_obj_set_style_radius(s_confirm_popup, 0, 0);
    lv_obj_set_style_pad_all(s_confirm_popup, 0, 0);
    lv_obj_clear_flag(s_confirm_popup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_confirm_popup, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *panel = lv_obj_create(s_confirm_popup);
    if (panel == NULL) {
        lv_obj_del(s_confirm_popup);
        s_confirm_popup = NULL;
        s_prompt_kind = CALIBRATION_PROMPT_NONE;
        return;
    }
    lv_obj_set_size(panel, LV_HOR_RES - 40, 142);
    lv_obj_center(panel);
    ui_page_style_panel(panel);
    lv_obj_set_style_pad_all(panel, 12, 0);

    const bool wheels = prompt == CALIBRATION_PROMPT_WHEELS;
    lv_obj_t *message = lv_label_create(panel);
    lv_obj_set_width(message, LV_HOR_RES - 64);
    lv_obj_set_height(message, 58);
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(message, &font_puhui_20_4, 0);
    lv_obj_set_style_text_color(message, lv_color_white(), 0);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(message, wheels ?
        app_language_text("车轮方向已更改，是否保存？", "Wheel direction changed. Save?") :
        app_language_text("舵机位置已更改，是否保存？", "Servo position changed. Save?"));
    lv_obj_align(message, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t *save = make_button_object(
        panel, app_language_text("保存", "Save"), 12, 92, 108, 36,
        0x34C759, 0x269A43, 18, confirm_save_cb);
    lv_obj_t *cancel = make_button_object(
        panel, app_language_text("取消", "Cancel"), 140, 92, 108, 36,
        0xE5484D, 0xB83232, 18, confirm_cancel_cb);
    if (save == NULL || cancel == NULL) {
        lv_obj_del(s_confirm_popup);
        s_confirm_popup = NULL;
        s_prompt_kind = CALIBRATION_PROMPT_NONE;
    }
}

static void advance_exit_prompt(void)
{
    if (s_confirm_popup != NULL) {
        return;
    }
    if (!s_wheel_prompt_done && wheel_config_changed()) {
        show_exit_prompt(CALIBRATION_PROMPT_WHEELS);
        return;
    }
    if (!s_servo_prompt_done && servo_config_changed()) {
        show_exit_prompt(CALIBRATION_PROMPT_SERVO);
        return;
    }
    finalize_exit();
}

static void finalize_exit(void)
{
    if (!s_any_change_saved) {
        close_overlay(false);
        return;
    }
    if (!motion_calibration_valid(&s_draft)) {
        notice("两端至少相差 100 μs", "Endpoints must differ by at least 100 us");
        s_wheel_prompt_done = true;
        s_servo_prompt_done = false;
        return;
    }
    esp_err_t ret = motion_manager_calibration_preview(&s_draft);
    if (ret == ESP_OK) {
        /* LVGL task 使用内部 RAM 栈；停止试转后再写 NVS。 */
        ret = motion_manager_calibration_finish(true);
    }
    if (ret != ESP_OK) {
        notice("保存失败，请重试或取消", "Save failed; retry or cancel");
        s_wheel_prompt_done = true;
        s_servo_prompt_done = false;
        return;
    }
    close_overlay(true);
}

static void request_exit(void)
{
    if (s_confirm_popup != NULL) {
        return;
    }
    stop_test();
    s_wheel_prompt_done = true;
    s_servo_prompt_done = false;
    s_any_change_saved = false;
    advance_exit_prompt();
}

static void cancel_cb(lv_event_t *event)
{
    (void)event;
    request_exit();
}

static void save_cb(lv_event_t *event)
{
    (void)event;
    stop_test();
    if (!motion_calibration_valid(&s_draft)) {
        notice("两端至少相差 100 μs", "Endpoints must differ by at least 100 us");
        return;
    }
    esp_err_t ret = motion_manager_calibration_preview(&s_draft);
    if (ret == ESP_OK) {
        /* 保存按钮直接提交当前草稿，不再额外弹出保存确认。 */
        ret = motion_manager_calibration_finish(true);
    }
    if (ret != ESP_OK) {
        notice("保存失败，请重试", "Save failed; try again");
        return;
    }
    close_overlay(true);
}

static void tab_cb(lv_event_t *event)
{
    stop_test();
    s_servo_tab = (intptr_t)lv_event_get_user_data(event) != 0;
    build_content();
}

static void refresh_wheel_labels(void)
{
    lv_label_set_text(s_wheel_labels[0], app_language_text(
        s_draft.reverse_a ? "A 反向:开" : "A 反向:关",
        s_draft.reverse_a ? "A rev:ON" : "A rev:OFF"));
    lv_label_set_text(s_wheel_labels[1], app_language_text(
        s_draft.reverse_b ? "B 反向:开" : "B 反向:关",
        s_draft.reverse_b ? "B rev:ON" : "B rev:OFF"));
    lv_label_set_text(s_wheel_labels[2], app_language_text(
        s_draft.swap_motors ? "交换:开" : "交换:关",
        s_draft.swap_motors ? "Swap:ON" : "Swap:OFF"));
}

static void wheel_option_cb(lv_event_t *event)
{
    stop_test();
    const intptr_t option = (intptr_t)lv_event_get_user_data(event);
    if (option == 0) {
        s_draft.reverse_a = !s_draft.reverse_a;
    } else if (option == 1) {
        s_draft.reverse_b = !s_draft.reverse_b;
    } else {
        s_draft.swap_motors = !s_draft.swap_motors;
    }
    refresh_wheel_labels();
    notice("按住方向键测试，确认后保存", "Hold a direction to test, then save");
}

static void reported_direction_preset_cb(lv_event_t *event)
{
    (void)event;
    stop_test();
    /* 针对“左变前、右变后、后变右、前变左”，在打开前的配置上
     * 反转逻辑 A；若此前交换过轮子，就应反转实际 B，且重复点击幂等。 */
    s_draft.reverse_a = s_original.reverse_a;
    s_draft.reverse_b = s_original.reverse_b;
    s_draft.swap_motors = s_original.swap_motors;
    if (s_draft.swap_motors) {
        s_draft.reverse_b = !s_draft.reverse_b;
    } else {
        s_draft.reverse_a = !s_draft.reverse_a;
    }
    refresh_wheel_labels();
    notice("已套用错向修正，请试转后保存", "Direction fix selected; test before saving");
}

static int selected_servo_pulse(void)
{
    return s_edit_high ? s_draft.servo_high_us : s_draft.servo_low_us;
}

static void refresh_servo_value(void)
{
    if (s_servo_value != NULL) {
        lv_label_set_text_fmt(s_servo_value,
            app_language_is_english() ? "Low %d / High %d us" : "最低 %d / 最高 %d μs",
            s_draft.servo_low_us, s_draft.servo_high_us);
    }
}

static void servo_minimum_cb(lv_event_t *event)
{
    (void)event;
    if (s_testing) {
        return;
    }

    motion_calibration_t candidate = s_draft;
    candidate.servo_low_us = ACTUATOR_SERVO_PULSE_MIN_US;
    /* 保证点击归零后草稿仍是可保存的配置；正常配置不会触发此分支。 */
    if (candidate.servo_high_us - candidate.servo_low_us <
        MOTION_CALIBRATION_SERVO_MIN_SPAN_US) {
        candidate.servo_high_us = ACTUATOR_SERVO_PULSE_MAX_US;
    }
    if (!motion_calibration_valid(&candidate) ||
        motion_manager_calibration_preview(&candidate) != ESP_OK) {
        notice("舵机最低点设置失败，请重试", "Could not set servo minimum; try again");
        return;
    }

    s_draft = candidate;
    s_edit_high = false;
    s_test_request = (motion_request_t) {
        .ttl_ms = CALIBRATION_TEST_TTL_MS,
        .ramp_ms = 180,
        .servo_pulse_us = ACTUATOR_SERVO_PULSE_MIN_US,
        .servo_enabled = true,
    };
    s_auto_servo_preview = true;
    s_test_started_ms = ui_page_manager_now_ms();
    s_test_last_ms = s_test_started_ms;
    s_test_limit_ms = CALIBRATION_SERVO_TEST_MAX_MS;
    s_testing = motion_manager_submit(MOTION_SOURCE_CALIBRATION,
                                       &s_test_request) == ESP_OK;
    if (!s_testing) {
        stop_test();
        notice("舵机归零请求被拒绝", "Servo minimum request rejected");
        return;
    }
    if (s_servo_button != NULL) {
        lv_obj_add_state(s_servo_button, LV_STATE_DISABLED);
    }
    notice("舵机正在转到最低点，请稍候", "Moving to the minimum; please wait");
}

static void start_servo_preview(bool automatic)
{
    /* 舵机调节连续更新同一条校准请求，不要每次滑动都 stop/resume PWM。
     * 仅从车轮试转切换过来时先停车；实际脉宽不经过端点映射。 */
    if (s_testing && !s_test_request.servo_enabled) {
        stop_test();
    }
    s_test_request = (motion_request_t) {
        .ttl_ms = CALIBRATION_TEST_TTL_MS,
        .ramp_ms = 180,
        .servo_pulse_us = selected_servo_pulse(),
        .servo_enabled = true,
    };
    s_auto_servo_preview = automatic;
    s_test_started_ms = ui_page_manager_now_ms();
    s_test_last_ms = s_test_started_ms;
    s_test_limit_ms = CALIBRATION_SERVO_TEST_MAX_MS;
    s_testing = motion_manager_submit(MOTION_SOURCE_CALIBRATION,
                                      &s_test_request) == ESP_OK;
    if (!s_testing) {
        stop_test();
        notice("舵机试位请求被拒绝", "Servo preview request rejected");
    }
}

static void set_servo_endpoint(int pulse_us)
{
    if (pulse_us < ACTUATOR_SERVO_PULSE_MIN_US) {
        pulse_us = ACTUATOR_SERVO_PULSE_MIN_US;
    } else if (pulse_us > ACTUATOR_SERVO_PULSE_MAX_US) {
        pulse_us = ACTUATOR_SERVO_PULSE_MAX_US;
    }
    /* 草稿端点可独立调整、试位，保存时再检查两端间距。
     * 否则把最高点向最低点拖动时会被恢复到旧最高值。 */
    if (s_edit_high) {
        s_draft.servo_high_us = pulse_us;
    } else {
        s_draft.servo_low_us = pulse_us;
    }
    if (motion_calibration_valid(&s_draft)) {
        notice("正在短时试位，随后自动释放 PWM", "Previewing; PWM releases automatically");
    } else {
        notice("可单独试位，保存时两端须相差 100 μs", "Preview allowed; save needs a 100 us span");
    }
    lv_slider_set_value(s_servo_slider, pulse_us, LV_ANIM_OFF);
    refresh_servo_value();
    start_servo_preview(true);
}

static void servo_select_cb(lv_event_t *event)
{
    s_edit_high = (intptr_t)lv_event_get_user_data(event) != 0;
    build_content();
    start_servo_preview(true);
}

static void servo_step_cb(lv_event_t *event)
{
    set_servo_endpoint(selected_servo_pulse() +
                       (int)(intptr_t)lv_event_get_user_data(event));
}

static void servo_slider_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_VALUE_CHANGED) {
        set_servo_endpoint((int)lv_slider_get_value(lv_event_get_target_obj(event)));
    } else if (code == LV_EVENT_PRESSED) {
        start_servo_preview(true);
    }
}

static void test_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        stop_test();
        return;
    }
    if (code != LV_EVENT_PRESSED) {
        return;
    }
    const intptr_t direction = (intptr_t)lv_event_get_user_data(event);
    if (direction == 4) {
        start_servo_preview(false);
        return;
    }
    stop_test();
    motion_calibration_t wheel_preview = s_draft;
    if (!motion_calibration_valid(&wheel_preview)) {
        /* 草稿舵机端点尚未完成时，也允许独立测试车轮方向。 */
        wheel_preview.servo_low_us = s_original.servo_low_us;
        wheel_preview.servo_high_us = s_original.servo_high_us;
    }
    if (motion_manager_calibration_preview(&wheel_preview) != ESP_OK) {
        notice("校准试运行不可用", "Calibration test unavailable");
        return;
    }
    s_test_request = (motion_request_t) {
        .ttl_ms = CALIBRATION_TEST_TTL_MS,
        .ramp_ms = 180,
        .servo_pulse_us = selected_servo_pulse(),
        .servo_enabled = false,
    };
    if (direction == 0) s_test_request.linear = 0.18f;
    if (direction == 1) s_test_request.linear = -0.18f;
    if (direction == 2) s_test_request.angular = 0.18f;
    if (direction == 3) s_test_request.angular = -0.18f;
    s_test_started_ms = ui_page_manager_now_ms();
    s_test_last_ms = s_test_started_ms;
    s_test_limit_ms = CALIBRATION_WHEEL_TEST_MAX_MS;
    s_testing = motion_manager_submit(MOTION_SOURCE_CALIBRATION,
                                      &s_test_request) == ESP_OK;
    if (!s_testing) {
        notice("试运行请求被拒绝", "Test request rejected");
    }
}

/* 简化界面不再显示手动停止按钮；自动归零仍由硬截止时间和 stop_test() 保护。
 * app_language_text("停止试位", "Stop preview") 作为旧配置兼容文案保留。 */
static void stop_cb(lv_event_t *event)
{
    (void)event;
    stop_test();
}

static void build_content(void)
{
    lv_obj_clean(s_content);
    s_servo_button = NULL;
    s_servo_slider = NULL;
    s_servo_value = NULL;
    s_notice = make_label(s_content, app_language_text(
        "点击圆按钮，舵机自动转到安全最低点，再安装齿轮",
        "Tap the circle to move the servo to its safe minimum, then install the gear."),
        0, 38);

    s_servo_button = make_button_object(
        s_content, app_language_text("舵机\n归零", "Servo\nMIN"),
        (LV_HOR_RES - 112) / 2, 40, 112, 112,
        0x2094FA, 0x1769B5, 56, servo_minimum_cb);
    if (s_servo_button != NULL) {
        lv_obj_set_style_border_width(s_servo_button, 0, 0);
    }

    lv_obj_t *status = lv_label_create(s_content);
    lv_obj_set_width(status, LV_HOR_RES - 24);
    lv_obj_set_height(status, 26);
    lv_obj_set_pos(status, 12, 154);
    lv_obj_set_style_text_font(status, &font_puhui_16_4, 0);
    lv_obj_set_style_text_color(status, lv_color_hex(0xD5D9E0), 0);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);
    lv_label_set_text(status, app_language_text(
        "归零后释放舵机，请在此时安装齿轮",
        "The servo releases after reaching MIN; install the gear then."));
}


esp_err_t page_motion_calibration_open(lv_obj_t *root,
                                       motion_calibration_closed_cb_t closed)
{
    if (root == NULL || s_overlay != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = motion_manager_calibration_begin(&s_draft);
    if (ret != ESP_OK) {
        return ret;
    }
    s_original = s_draft;
    s_root = root;
    s_closed = closed;
    s_servo_tab = false;
    s_edit_high = false;
    s_testing = false;
    s_auto_servo_preview = false;
    s_confirm_popup = NULL;
    s_prompt_kind = CALIBRATION_PROMPT_NONE;
    /* 车轮方向已固定在 motion_calibration.c，不在此页面产生车轮草稿。 */
    s_wheel_prompt_done = true;
    s_servo_prompt_done = false;
    s_any_change_saved = false;
    s_root_was_scrollable = lv_obj_has_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_scroll_to_y(root, 0, LV_ANIM_OFF);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    s_overlay = lv_obj_create(root);
    if (s_overlay == NULL) {
        (void)motion_manager_calibration_finish(false);
        if (s_root_was_scrollable) lv_obj_add_flag(root, LV_OBJ_FLAG_SCROLLABLE);
        s_root = NULL;
        s_closed = NULL;
        return ESP_ERR_NO_MEM;
    }
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_size(s_overlay, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_overlay, 0, 0);
    lv_obj_set_layout(s_overlay, LV_LAYOUT_NONE);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE |
                               LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN |
                               LV_OBJ_FLAG_SCROLLABLE |
                               LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_move_foreground(s_overlay);
    make_button(s_overlay, app_language_text("退出", "Exit"),
                12, 12, 68, 32, cancel_cb, 0, LV_EVENT_CLICKED);
    lv_obj_t *title = make_label(s_overlay, app_language_text("运动校准", "Calibration"), 18, 26);
    lv_obj_set_width(title, 144);
    lv_obj_set_x(title, (LV_HOR_RES - 144) / 2);
    make_button_object(s_overlay, app_language_text("保存", "Save"),
                       LV_HOR_RES - 80, 12, 68, 32,
                       0x34C759, 0x269A43, 16, save_cb);
    s_content = lv_obj_create(s_overlay);
    if (s_content == NULL) {
        close_overlay(false);
        return ESP_ERR_NO_MEM;
    }
    lv_obj_set_pos(s_content, 0, 50);
    lv_obj_set_size(s_content, LV_HOR_RES, LV_VER_RES - 50);
    ui_page_style_root(s_content);
    lv_obj_clear_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);
    build_content();
    return ESP_OK;
}

void page_motion_calibration_tick(uint32_t ms)
{
    if (!s_testing || s_overlay == NULL) {
        return;
    }
    if ((uint32_t)(ms - s_test_started_ms) >= s_test_limit_ms) {
        const bool automatic = s_auto_servo_preview;
        stop_test();
        if (automatic) {
            notice("已到最低点，PWM 已释放，请安装齿轮",
                   "Minimum reached; PWM released. Install the gear now.");
        } else {
            notice("单次试运行已停止，请松开后再按", "Test timed out. Release and press again.");
        }
        return;
    }
    if ((uint32_t)(ms - s_test_last_ms) >= CALIBRATION_REFRESH_MS) {
        s_test_last_ms = ms;
        if (motion_manager_submit(MOTION_SOURCE_CALIBRATION, &s_test_request) != ESP_OK) {
            stop_test();
            notice("试运行已停止", "Test stopped");
        }
    }
}
