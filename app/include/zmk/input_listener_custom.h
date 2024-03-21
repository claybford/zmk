#ifndef INPUT_LISTENER_CUSTOM_H
#define INPUT_LISTENER_CUSTOM_H

#include <zephyr/types.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Function to update temporary scale multiplier and divisor.
 * 
 * @param multiplier New temporary scale multiplier.
 * @param divisor New temporary scale divisor.
 */
void input_listener_set_temp_scale(uint16_t multiplier, uint16_t divisor);

/**
 * Function to toggle the use of temporary scale values.
 * 
 * @param use_temp True to use temporary scale values, false to use default.
 */
void input_listener_toggle_use_temp_scale(bool use_temp);

#ifdef __cplusplus
}
#endif

#endif // INPUT_LISTENER_CUSTOM_H
