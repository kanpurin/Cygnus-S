#include <zephyr/devicetree.h>

#if defined(CONFIG_ZMK_POINTING) && DT_NODE_HAS_STATUS(DT_NODELABEL(trackball), okay)

#include <drivers/input_processor.h>
#include <dt-bindings/zmk/keys.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/keymap.h>

#define MOUSE_LAYER 4
#define MOUSE_IDLE_MS 10000U
#define WHEEL_CLICK_POSITION 32
#define GESTURE_POSITION 33
#define GESTURE_THRESHOLD 24

static struct k_spinlock gesture_lock;
static bool gesture_held;
static bool arrow_sent;
static int32_t gesture_x;

static void set_gesture_held(bool held) {
    k_spinlock_key_t key = k_spin_lock(&gesture_lock);
    gesture_held = held;
    arrow_sent = false;
    gesture_x = 0;
    k_spin_unlock(&gesture_lock, key);
}

#if DT_NODE_HAS_STATUS(DT_NODELABEL(cygnus_arrow_gesture), okay)
static int process_arrow_gesture(const struct device *dev, struct input_event *event,
                                 uint32_t param1, uint32_t param2,
                                 struct zmk_input_processor_state *state) {
    if (event->type != INPUT_EV_REL ||
        (event->code != INPUT_REL_X && event->code != INPUT_REL_Y)) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    uint32_t arrow = 0;
    k_spinlock_key_t key = k_spin_lock(&gesture_lock);
    if (gesture_held) {
        if (event->code == INPUT_REL_X && !arrow_sent) {
            gesture_x += event->value;
            if (gesture_x <= -GESTURE_THRESHOLD || gesture_x >= GESTURE_THRESHOLD) {
                arrow = gesture_x < 0 ? LEFT_ARROW : RIGHT_ARROW;
                arrow_sent = true;
            }
        }
        event->value = 0;
    }
    k_spin_unlock(&gesture_lock, key);

    if (arrow) {
        int64_t now = k_uptime_get();
        raise_zmk_keycode_state_changed_from_encoded(arrow, true, now);
        raise_zmk_keycode_state_changed_from_encoded(arrow, false, now);
    }
    return ZMK_INPUT_PROC_CONTINUE;
}

static const struct zmk_input_processor_driver_api arrow_gesture_api = {
    .handle_event = process_arrow_gesture,
};

DEVICE_DT_DEFINE(DT_NODELABEL(cygnus_arrow_gesture), NULL, NULL, NULL, NULL, POST_KERNEL,
                 CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &arrow_gesture_api);
#endif

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
            set_gesture_held(false);
            k_work_cancel_delayable(&mouse_idle_work);
        }
    }

    const struct zmk_position_state_changed *key = as_zmk_position_state_changed(event);
    if (key) {
        bool active = zmk_keymap_layer_active(MOUSE_LAYER);
        if (key->position == GESTURE_POSITION && (!key->state || active)) {
            set_gesture_held(key->state);
        }
        if (active && ((key->position >= 19 && key->position <= 21) ||
                       key->position == WHEEL_CLICK_POSITION ||
                       key->position == GESTURE_POSITION)) {
            record_activity();
        }
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(cygnus_mouse_idle, mouse_idle_listener);
ZMK_SUBSCRIPTION(cygnus_mouse_idle, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(cygnus_mouse_idle, zmk_position_state_changed);

#endif
