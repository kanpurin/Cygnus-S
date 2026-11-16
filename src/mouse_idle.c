#include <zephyr/devicetree.h>

#if defined(CONFIG_ZMK_POINTING) && DT_NODE_HAS_STATUS(DT_NODELABEL(trackball), okay)

#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/keymap.h>

#define MOUSE_LAYER 4
#define MOUSE_IDLE_MS 10000U

static atomic_t last_activity_ms;
static void expire_mouse_layer(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(mouse_idle_work, expire_mouse_layer);

static void record_activity(void) {
    atomic_set(&last_activity_ms, (atomic_val_t)k_uptime_get_32());
    k_work_reschedule(&mouse_idle_work, K_MSEC(MOUSE_IDLE_MS));
}

static void expire_mouse_layer(struct k_work *work) {
    if (!zmk_keymap_layer_active(MOUSE_LAYER)) {
        return;
    }

    const uint32_t elapsed = k_uptime_get_32() - (uint32_t)atomic_get(&last_activity_ms);
    if (elapsed < MOUSE_IDLE_MS) {
        // A newer report can arrive while the delayable work is already running.
        k_work_reschedule(&mouse_idle_work, K_MSEC(MOUSE_IDLE_MS - elapsed));
        return;
    }

    zmk_keymap_layer_deactivate(MOUSE_LAYER);
}

static void trackball_activity(struct input_event *event) {
    if (event->type == INPUT_EV_REL && event->value != 0 &&
        zmk_keymap_layer_active(MOUSE_LAYER)) {
        record_activity();
    }
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_NODELABEL(trackball)), trackball_activity);

static int mouse_idle_listener(const zmk_event_t *event) {
    const struct zmk_layer_state_changed *layer = as_zmk_layer_state_changed(event);
    if (layer && layer->layer == MOUSE_LAYER) {
        if (layer->state) {
            record_activity();
        } else {
            k_work_cancel_delayable(&mouse_idle_work);
        }
    }

    const struct zmk_position_state_changed *key = as_zmk_position_state_changed(event);
    // K, L and the scroll key are the mouse layer's three active positions.
    if (key && key->position >= 19 && key->position <= 21 &&
        zmk_keymap_layer_active(MOUSE_LAYER)) {
        record_activity();
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(cygnus_mouse_idle, mouse_idle_listener);
ZMK_SUBSCRIPTION(cygnus_mouse_idle, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(cygnus_mouse_idle, zmk_position_state_changed);

#endif
