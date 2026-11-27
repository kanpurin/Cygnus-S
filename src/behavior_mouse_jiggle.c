#define DT_DRV_COMPAT cygnus_mouse_jiggle

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <drivers/behavior.h>
#include <zmk/behavior.h>

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#if DT_NODE_HAS_STATUS(DT_NODELABEL(jiggle_listener), okay)
#include <zephyr/input/input.h>
#include <zephyr/drivers/led.h>
#include <zmk/keymap.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>

#define JIGGLE_INTERVAL_MS 180000

static K_MUTEX_DEFINE(jiggle_lock);
static bool enabled;
static bool click_enabled;
static bool click_down;
static int next_step = 1;
static bool indicator_lit;
static const struct device *const led_device = DEVICE_DT_GET(DT_PARENT(DT_ALIAS(led_red)));
static const uint8_t led_indices[] = {
    DT_NODE_CHILD_IDX(DT_ALIAS(led_red)),
    DT_NODE_CHILD_IDX(DT_ALIAS(led_green)),
    DT_NODE_CHILD_IDX(DT_ALIAS(led_blue)),
};
static const uint8_t layer_colors[] = {
    CONFIG_RGBLED_WIDGET_LAYER_0_COLOR, CONFIG_RGBLED_WIDGET_LAYER_1_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_2_COLOR, CONFIG_RGBLED_WIDGET_LAYER_3_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_4_COLOR, CONFIG_RGBLED_WIDGET_LAYER_5_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_6_COLOR, CONFIG_RGBLED_WIDGET_LAYER_7_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_8_COLOR,
};

static void set_indicator_color(uint8_t color) {
    for (size_t index = 0; index < ARRAY_SIZE(led_indices); index++) {
        if (color & BIT(index)) {
            led_on(led_device, led_indices[index]);
        } else {
            led_off(led_device, led_indices[index]);
        }
    }
}

static uint8_t active_indicator_color(void) {
    return (enabled ? 5 : 0) | (click_enabled ? 2 : 0);
}

static void update_indicator(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(indicator_work, update_indicator);

static void refresh_indicator(void) {
    uint8_t color = active_indicator_color();
    if (color != 0) {
        indicator_lit = true;
        set_indicator_color(color);
        k_work_reschedule(&indicator_work, K_MSEC(500));
    } else {
        k_work_cancel_delayable(&indicator_work);
        uint8_t layer = zmk_keymap_highest_layer_active();
        set_indicator_color(layer < ARRAY_SIZE(layer_colors) ? layer_colors[layer] : 0);
    }
}

static void update_indicator(struct k_work *work) {
    k_mutex_lock(&jiggle_lock, K_FOREVER);
    uint8_t color = active_indicator_color();
    if (color != 0) {
        indicator_lit = !indicator_lit;
        set_indicator_color(indicator_lit ? color : 0);
        k_work_reschedule(&indicator_work, K_MSEC(500));
    }
    k_mutex_unlock(&jiggle_lock);
}

static void perform_click(struct k_work *work);
static void finish_click(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(click_work, perform_click);
static K_WORK_DELAYABLE_DEFINE(click_release_work, finish_click);

static void release_click(void) {
    if (click_down) {
        zmk_hid_mouse_button_release(0);
        click_down = false;
        zmk_endpoints_send_mouse_report();
    }
}

static void finish_click(struct k_work *work) {
    k_mutex_lock(&jiggle_lock, K_FOREVER);
    release_click();
    k_mutex_unlock(&jiggle_lock);
}

static void perform_click(struct k_work *work) {
    k_mutex_lock(&jiggle_lock, K_FOREVER);
    if (click_enabled) {
        // Leave physical dragging alone and keep our press/release balanced.
        if (!click_down && !(zmk_hid_get_mouse_report()->body.buttons & BIT(0))) {
            if (zmk_hid_mouse_button_press(0) == 0) {
                click_down = true;
                if (zmk_endpoints_send_mouse_report() == 0) {
                    k_work_reschedule(&click_release_work, K_MSEC(50));
                } else {
                    release_click();
                }
            }
        }
        k_work_reschedule(&click_work, K_MSEC(5000));
    }
    k_mutex_unlock(&jiggle_lock);
}

static void move_cursor(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(jiggle_work, move_cursor);

static void move_cursor(struct k_work *work) {
    k_mutex_lock(&jiggle_lock, K_FOREVER);
    if (enabled) {
        int result = input_report_rel(DEVICE_DT_INST_GET(0), INPUT_REL_X, next_step,
                                      true, K_NO_WAIT);
        if (result == 0) {
            next_step = -next_step;
        }
        k_work_reschedule(&jiggle_work, K_MSEC(JIGGLE_INTERVAL_MS));
    }
    k_mutex_unlock(&jiggle_lock);
}
#endif

static int toggle_pressed(struct zmk_behavior_binding *binding,
                          struct zmk_behavior_binding_event event) {
#if DT_NODE_HAS_STATUS(DT_NODELABEL(jiggle_listener), okay)
    k_mutex_lock(&jiggle_lock, K_FOREVER);
    enabled = !enabled;
    if (enabled) {
        next_step = 1;
        k_work_reschedule(&jiggle_work, K_MSEC(JIGGLE_INTERVAL_MS));
    } else {
        k_work_cancel_delayable(&jiggle_work);
    }
    refresh_indicator();
    k_mutex_unlock(&jiggle_lock);
#endif
    return ZMK_BEHAVIOR_OPAQUE;
}

static int click_toggle_pressed(struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event) {
#if DT_NODE_HAS_STATUS(DT_NODELABEL(jiggle_listener), okay)
    k_mutex_lock(&jiggle_lock, K_FOREVER);
    click_enabled = !click_enabled;
    if (click_enabled) {
        k_work_reschedule(&click_work, K_MSEC(5000));
    } else {
        k_work_cancel_delayable(&click_work);
        k_work_cancel_delayable(&click_release_work);
        release_click();
    }
    refresh_indicator();
    k_mutex_unlock(&jiggle_lock);
#endif
    return ZMK_BEHAVIOR_OPAQUE;
}

static int toggle_released(struct zmk_behavior_binding *binding,
                           struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api jiggle_api = {
    .binding_pressed = toggle_pressed,
    .binding_released = toggle_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                       CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &jiggle_api);

#if DT_NODE_HAS_STATUS(DT_NODELABEL(auto_click_toggle), okay)
static const struct behavior_driver_api auto_click_api = {
    .binding_pressed = click_toggle_pressed,
    .binding_released = toggle_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

BEHAVIOR_DT_DEFINE(DT_NODELABEL(auto_click_toggle), NULL, NULL, NULL, NULL, POST_KERNEL,
                  CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &auto_click_api);
#endif

#endif
