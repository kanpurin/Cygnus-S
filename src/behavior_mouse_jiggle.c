#define DT_DRV_COMPAT cygnus_mouse_jiggle

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <drivers/behavior.h>
#include <zmk/behavior.h>

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#if DT_NODE_HAS_STATUS(DT_NODELABEL(jiggle_listener), okay)
#include <zephyr/input/input.h>

#define JIGGLE_INTERVAL_MS 180000

static K_MUTEX_DEFINE(jiggle_lock);
static bool enabled;
static int next_step = 1;

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
