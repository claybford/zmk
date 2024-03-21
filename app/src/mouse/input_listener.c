/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_listener

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>

#include <zmk/mouse.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);


// Hacked-in mouse key speed change
#include <zephyr/types.h>
#include <stdbool.h>
#include <zmk/input_listener_custom.h>
static uint16_t temp_scale_multiplier = 23; // Temp value (can be updated dynamically)
static uint16_t temp_scale_divisor = 8; // Temp value (can be updated dynamically)
static bool use_temp_scale = false; // Flag to toggle scale value usage
// Function to update temporary scale multiplier and divisor
void input_listener_set_temp_scale(uint16_t multiplier, uint16_t divisor) {
    LOG_DBG("Input listener setting temp scale: multiplier=%d, divisor=%d", multiplier, divisor);
    temp_scale_multiplier = multiplier;
    temp_scale_divisor = divisor;
}
// Function to toggle the use of temporary scale values
void input_listener_toggle_use_temp_scale(bool use_temp) {
    LOG_DBG("Setting use_temp_scale to %s", use_temp ? "true" : "false");
    use_temp_scale = use_temp;
}



enum input_listener_xy_data_mode {
    INPUT_LISTENER_XY_DATA_MODE_NONE,
    INPUT_LISTENER_XY_DATA_MODE_REL,
    INPUT_LISTENER_XY_DATA_MODE_ABS,
};

struct input_listener_xy_data {
    enum input_listener_xy_data_mode mode;
    int16_t x;
    int16_t y;
};

struct input_listener_data {
    struct input_listener_xy_data data;
    struct input_listener_xy_data wheel_data;

    uint8_t button_set;
    uint8_t button_clear;
};

struct input_listener_config {
    bool xy_swap;
    bool x_invert;
    bool y_invert;
    uint16_t scale_multiplier;
    uint16_t scale_divisor;
};

static void handle_rel_code(struct input_listener_data *data, struct input_event *evt) {
    switch (evt->code) {
    case INPUT_REL_X:
        data->data.mode = INPUT_LISTENER_XY_DATA_MODE_REL;
        data->data.x += evt->value;
        break;
    case INPUT_REL_Y:
        data->data.mode = INPUT_LISTENER_XY_DATA_MODE_REL;
        data->data.y += evt->value;
        break;
    case INPUT_REL_WHEEL:
        data->wheel_data.mode = INPUT_LISTENER_XY_DATA_MODE_REL;
        data->wheel_data.y += evt->value;
        break;
    case INPUT_REL_HWHEEL:
        data->wheel_data.mode = INPUT_LISTENER_XY_DATA_MODE_REL;
        data->wheel_data.x += evt->value;
        break;
    default:
        break;
    }
}

static void handle_key_code(struct input_listener_data *data, struct input_event *evt) {
    int8_t btn;

    switch (evt->code) {
    case INPUT_BTN_0:
    case INPUT_BTN_1:
    case INPUT_BTN_2:
    case INPUT_BTN_3:
    case INPUT_BTN_4:
        btn = evt->code - INPUT_BTN_0;
        if (evt->value > 0) {
            WRITE_BIT(data->button_set, btn, 1);
        } else {
            WRITE_BIT(data->button_clear, btn, 1);
        }
        break;
    default:
        break;
    }
}

static void swap_xy(struct input_event *evt) {
    switch (evt->code) {
    case INPUT_REL_X:
        evt->code = INPUT_REL_Y;
        break;
    case INPUT_REL_Y:
        evt->code = INPUT_REL_X;
        break;
    }
}

static void filter_with_input_config(const struct input_listener_config *cfg,
                                     struct input_event *evt) {
    if (!evt->dev) {
        return;
    }

    if (cfg->xy_swap) {
        swap_xy(evt);
    }

    if ((cfg->x_invert && evt->code == INPUT_REL_X) ||
        (cfg->y_invert && evt->code == INPUT_REL_Y)) {
        evt->value = -(evt->value);
    }

    //evt->value = (int16_t)((evt->value * cfg->scale_multiplier) / cfg->scale_divisor);

    // Hacked-in mouse key speed change replacing the direct usage of cfg->scale_multiplier and cfg->scale_divisor
    // with logic that selects between cfg and temporary values based on use_temp_scale flag.
    evt->value = (int16_t)((evt->value * (use_temp_scale ? temp_scale_multiplier : cfg->scale_multiplier)) / 
                        (use_temp_scale ? temp_scale_divisor : cfg->scale_divisor));
}

static void clear_xy_data(struct input_listener_xy_data *data) {
    data->x = data->y = 0;
    data->mode = INPUT_LISTENER_XY_DATA_MODE_NONE;
}

static void input_handler(const struct input_listener_config *config,
                          struct input_listener_data *data, struct input_event *evt) {
    // First, filter to update the event data as needed.
    filter_with_input_config(config, evt);

    switch (evt->type) {
    case INPUT_EV_REL:
        handle_rel_code(data, evt);
        break;
    case INPUT_EV_KEY:
        handle_key_code(data, evt);
        break;
    }

    if (evt->sync) {
        if (data->wheel_data.mode == INPUT_LISTENER_XY_DATA_MODE_REL) {
            zmk_hid_mouse_scroll_set(data->wheel_data.x, data->wheel_data.y);
        }

        if (data->data.mode == INPUT_LISTENER_XY_DATA_MODE_REL) {
            zmk_hid_mouse_movement_set(data->data.x, data->data.y);
        }

        if (data->button_set != 0) {
            for (int i = 0; i < ZMK_HID_MOUSE_NUM_BUTTONS; i++) {
                if ((data->button_set & BIT(i)) != 0) {
                    zmk_hid_mouse_button_press(i);
                }
            }
        }

        if (data->button_clear != 0) {
            for (int i = 0; i < ZMK_HID_MOUSE_NUM_BUTTONS; i++) {
                if ((data->button_clear & BIT(i)) != 0) {
                    zmk_hid_mouse_button_release(i);
                }
            }
        }

        zmk_endpoints_send_mouse_report();
        zmk_hid_mouse_scroll_set(0, 0);
        zmk_hid_mouse_movement_set(0, 0);

        clear_xy_data(&data->data);
        clear_xy_data(&data->wheel_data);

        data->button_set = data->button_clear = 0;
    }
}

#define IL_INST(n)                                                                                 \
    static const struct input_listener_config config_##n = {                                       \
        .xy_swap = DT_INST_PROP(n, xy_swap),                                                       \
        .x_invert = DT_INST_PROP(n, x_invert),                                                     \
        .y_invert = DT_INST_PROP(n, y_invert),                                                     \
        .scale_multiplier = DT_INST_PROP(n, scale_multiplier),                                     \
        .scale_divisor = DT_INST_PROP(n, scale_divisor),                                           \
    };                                                                                             \
    static struct input_listener_data data_##n = {};                                               \
    void input_handler_##n(struct input_event *evt) {                                              \
        input_handler(&config_##n, &data_##n, evt);                                                \
    }                                                                                              \
    INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_INST_PHANDLE(n, device)), input_handler_##n);

DT_INST_FOREACH_STATUS_OKAY(IL_INST)
