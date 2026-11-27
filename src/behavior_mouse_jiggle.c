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

#define JIGGLE_INTERVAL_MS 180000

static K_MUTEX_DEFINE(jiggle_lock);
static bool enabled;
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

static void update_indicator(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(indicator_work, update_indicator);

static void update_indicator(struct k_work *work) {
    k_mutex_lock(&jiggle_lock, K_FOREVER);
    if (enabled) {
        indicator_lit = !indicator_lit;
        set_indicator_color(indicator_lit ? 5 : 0);
        k_work_reschedule(&indicator_work, K_MSEC(500));
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
        indicator_lit = true;
        set_indicator_color(5);
        k_work_reschedule(&indicator_work, K_MSEC(500));
        k_work_reschedule(&jiggle_work, K_MSEC(JIGGLE_INTERVAL_MS));
    } else {
        k_work_cancel_delayable(&jiggle_work);
        k_work_cancel_delayable(&indicator_work);
        uint8_t layer = zmk_keymap_highest_layer_active();
        set_indicator_color(layer < ARRAY_SIZE(layer_colors) ? layer_colors[layer] : 0);
    }
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

#endif
