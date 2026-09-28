#ifndef MINIME_TRAITS_H
#define MINIME_TRAITS_H

#include <stddef.h>
#include "defines.h"

#define MINIME_TRAIT_PATH_MAX 256
#define MINIME_TRAIT_NAME_MAX 64

//////////////////////////////////////
// Display Aspect & Dimensions

typedef enum {
	MINIME_ASPECT_4x3,
	MINIME_ASPECT_3x2,
	MINIME_ASPECT_16x9,
	MINIME_ASPECT_1x1,
	MINIME_ASPECT_UNKNOWN,
} MinimeScreenAspect;

//////////////////////////////////////
// Canonical Hardware Traits

// [device]
extern char device_id[MINIME_TRAIT_NAME_MAX];
extern char device_model[MINIME_TRAIT_PATH_MAX];

// [screen]
extern int screen_width;
extern int screen_height;
extern int screen_rotation;
extern int screen_padding;
extern int screen_row_count;
extern MinimeScreenAspect screen_aspect;
extern int screen_refresh_rate;
extern char screen_backlight_path[MINIME_TRAIT_PATH_MAX];
extern int screen_backlight_max;
extern char screen_blank_path[MINIME_TRAIT_PATH_MAX];

// [gpu]
extern char gpu_device[MINIME_TRAIT_PATH_MAX];
extern char gpu_hdmi_state_path[MINIME_TRAIT_PATH_MAX];
extern int gpu_hdmi_width;
extern int gpu_hdmi_height;
extern char gpu_devfreq_path[MINIME_TRAIT_PATH_MAX];
extern int gpu_clock_min;
extern int gpu_clock_max;

// [cpu]
extern char cpu_governor_path[MINIME_TRAIT_PATH_MAX];
extern char cpu_clock_path[MINIME_TRAIT_PATH_MAX];

// [audio]
extern char audio_card[MINIME_TRAIT_NAME_MAX];
extern char audio_mixer[MINIME_TRAIT_NAME_MAX];
extern char audio_jack_device_name[MINIME_TRAIT_NAME_MAX];

// [power]
extern char power_battery_sysfs[MINIME_TRAIT_PATH_MAX];
extern char power_charger_online_path[MINIME_TRAIT_PATH_MAX];
extern char power_led_path[MINIME_TRAIT_PATH_MAX];

// [wireless]
extern char wifi_interface[MINIME_TRAIT_NAME_MAX];
extern char bluetooth_interface[MINIME_TRAIT_NAME_MAX];

// [input]
extern int button_keycodes[BTN_ID_COUNT];
extern int axis_lx;
extern int axis_ly;
extern int axis_rx;
extern int axis_ry;
extern int axis_hat_x;
extern int axis_hat_y;
extern int axis_min;
extern int axis_center;
extern int axis_max;
extern int axis_lx_invert;
extern int axis_ly_invert;
extern int axis_rx_invert;
extern int axis_ry_invert;
extern char input_gamepad[MINIME_TRAIT_NAME_MAX];
extern char input_stick[MINIME_TRAIT_NAME_MAX];
extern char input_power[MINIME_TRAIT_NAME_MAX];
extern char input_volume[MINIME_TRAIT_NAME_MAX];
extern char input_menu[MINIME_TRAIT_NAME_MAX];
extern char input_rumble[MINIME_TRAIT_NAME_MAX];
extern char input_lid[MINIME_TRAIT_NAME_MAX];

//////////////////////////////////////
// Public Trait APIs

int MINIME_traitsInit(void);
int MINIME_traitAvailable(const char* value);
int MINIME_inputOpenByName(const char* expected);
int MINIME_isHDMIConnected(void);

#endif // MINIME_TRAITS_H
