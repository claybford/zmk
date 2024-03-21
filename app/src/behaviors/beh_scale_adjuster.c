// beh_scale_adjuster.c
// Hacked-in mouse key speed change

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zmk/input_listener_custom.h>
#include <zmk/keymap.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

extern void input_listener_set_temp_scale(uint16_t multiplier, uint16_t divisor);
extern void input_listener_toggle_use_temp_scale(bool use_temp);

struct beh_scale_adjuster_config {
    uint16_t temp_multiplier;
    uint16_t temp_divisor;
};

static int beh_scale_adjuster_init(const struct device *dev) {
    LOG_INF("beh_scale_adjuster_init called"); // Log initialization
    return 0; // Initialization success
}

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    LOG_DBG("Keymap binding pressed"); // Log key press event
    const struct device *dev = device_get_binding(binding->behavior_dev);
    if (!dev) {
        LOG_ERR("Failed to get device binding");
        return -EINVAL;
    }
    const struct beh_scale_adjuster_config *cfg = dev->config;

    // Set the temporary scale values
    LOG_DBG("Setting temp scale: multiplier=%d, divisor=%d", cfg->temp_multiplier, cfg->temp_divisor);
    input_listener_set_temp_scale(cfg->temp_multiplier, cfg->temp_divisor);
    // Enable temporary scale values
    input_listener_toggle_use_temp_scale(true);

    return 0;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    LOG_DBG("Keymap binding released"); // Log key release event
    const struct device *dev = device_get_binding(binding->behavior_dev);
    if (!dev) {
        // Consistent error handling for device binding in the release function
        LOG_ERR("Failed to get device binding on release");
        return -EINVAL;
    }
    // Revert to default scale values when the key is released
    input_listener_toggle_use_temp_scale(false);

    return 0;
}

static const struct behavior_driver_api beh_scale_adjuster_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
};

#define DEFINE_BEH_SCALE_ADJUSTER(n) \
static const struct beh_scale_adjuster_config beh_scale_adjuster_config_##n = { \
    .temp_multiplier = DT_INST_PROP(n, temp_multiplier), \
    .temp_divisor = DT_INST_PROP(n, temp_divisor), \
}; \
BEHAVIOR_DT_INST_DEFINE(n, \
                      beh_scale_adjuster_init, \
                      NULL, \
                      NULL, \
                      &beh_scale_adjuster_config_##n, \
                      POST_KERNEL, \
                      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, \
                      &beh_scale_adjuster_driver_api);

DT_INST_FOREACH_STATUS_OKAY(DEFINE_BEH_SCALE_ADJUSTER)
