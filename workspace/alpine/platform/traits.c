#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "defines.h"
#include "traits.h"
#include "utils.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/input.h>

#define TRAITS_PATH "/mnt/sdcard/.minime/traits"
#define NA "na"

//////////////////////////////////////
// Canonical Hardware Variables

char device_id[MINIME_TRAIT_NAME_MAX] = "";
char device_model[MINIME_TRAIT_PATH_MAX] = "";

int screen_width = 640;
int screen_height = 480;
int screen_rotation = 0;
int screen_padding = 10;
int screen_row_count = 6;
MinimeScreenAspect screen_aspect = MINIME_ASPECT_4x3;
int screen_refresh_rate = 60;
char screen_backlight_path[MINIME_TRAIT_PATH_MAX] = "";
int screen_backlight_max = 255;
char screen_blank_path[MINIME_TRAIT_PATH_MAX] = "";

char gpu_device[MINIME_TRAIT_PATH_MAX] = "";
char gpu_hdmi_state_path[MINIME_TRAIT_PATH_MAX] = "";
int gpu_hdmi_width = 1280;
int gpu_hdmi_height = 720;
char gpu_devfreq_path[MINIME_TRAIT_PATH_MAX] = "";
int gpu_clock_min = -1;
int gpu_clock_max = -1;

char cpu_governor_path[MINIME_TRAIT_PATH_MAX] = "";
char cpu_clock_path[MINIME_TRAIT_PATH_MAX] = "";

char audio_card[MINIME_TRAIT_NAME_MAX] = "default";
char audio_mixer[MINIME_TRAIT_NAME_MAX] = "Master";
char audio_jack_device_name[MINIME_TRAIT_NAME_MAX] = "";

char power_battery_sysfs[MINIME_TRAIT_PATH_MAX] = "";
char power_charger_online_path[MINIME_TRAIT_PATH_MAX] = "";
char power_led_path[MINIME_TRAIT_PATH_MAX] = "";

char wifi_interface[MINIME_TRAIT_NAME_MAX] = "";
char bluetooth_interface[MINIME_TRAIT_NAME_MAX] = "";

int button_keycodes[BTN_ID_COUNT];
int axis_lx = -1;
int axis_ly = -1;
int axis_rx = -1;
int axis_ry = -1;
int axis_hat_x = 16;
int axis_hat_y = 17;
int axis_min = -1;
int axis_center = -1;
int axis_max = -1;
int axis_lx_invert = 0;
int axis_ly_invert = 0;
int axis_rx_invert = 0;
int axis_ry_invert = 0;
char input_gamepad[MINIME_TRAIT_NAME_MAX] = "";
char input_stick[MINIME_TRAIT_NAME_MAX] = "";
char input_power[MINIME_TRAIT_NAME_MAX] = "";
char input_volume[MINIME_TRAIT_NAME_MAX] = "";
char input_menu[MINIME_TRAIT_NAME_MAX] = "";
char input_rumble[MINIME_TRAIT_NAME_MAX] = "";
char input_lid[MINIME_TRAIT_NAME_MAX] = "";

static int initialized = 0;
static int valid = 0;

//////////////////////////////////////
// Internal Schema Table

typedef enum {
	TYPE_STRING,
	TYPE_INT,
	TYPE_ASPECT,
} TraitType;

typedef struct {
	const char* key;
	TraitType type;
	void* dest;
	size_t max_len;
} TraitField;

#define STR_BIND(key, var) {key, TYPE_STRING, var, sizeof(var)}
#define INT_BIND(key, var) {key, TYPE_INT, &var, 0}
#define ASPECT_BIND(key, var) {key, TYPE_ASPECT, &var, 0}

static const TraitField TRAIT_FIELDS[] = {
	STR_BIND("device_id", device_id),
	STR_BIND("device_model", device_model),
	INT_BIND("screen_width", screen_width),
	INT_BIND("screen_height", screen_height),
	INT_BIND("screen_rotation", screen_rotation),
	ASPECT_BIND("screen_aspect", screen_aspect),
	INT_BIND("screen_refresh_rate", screen_refresh_rate),
	STR_BIND("screen_backlight_path", screen_backlight_path),
	INT_BIND("screen_backlight_max", screen_backlight_max),
	STR_BIND("screen_blank_path", screen_blank_path),
	STR_BIND("cpu_governor_path", cpu_governor_path),
	STR_BIND("cpu_clock_path", cpu_clock_path),
	STR_BIND("gpu_device", gpu_device),
	STR_BIND("gpu_hdmi_state_path", gpu_hdmi_state_path),
	INT_BIND("gpu_clock_min", gpu_clock_min),
	INT_BIND("gpu_clock_max", gpu_clock_max),
	STR_BIND("audio_card", audio_card),
	STR_BIND("audio_mixer", audio_mixer),
	STR_BIND("audio_jack_device_name", audio_jack_device_name),
	STR_BIND("input_gamepad_device_name", input_gamepad),
	STR_BIND("input_stick_device_name", input_stick),
	STR_BIND("input_power_device_name", input_power),
	STR_BIND("input_volume_device_name", input_volume),
	STR_BIND("input_menu_device_name", input_menu),
	STR_BIND("input_lid_device_name", input_lid),
	STR_BIND("input_rumble_device_name", input_rumble),
	INT_BIND("key_up", button_keycodes[BTN_ID_DPAD_UP]),
	INT_BIND("key_down", button_keycodes[BTN_ID_DPAD_DOWN]),
	INT_BIND("key_left", button_keycodes[BTN_ID_DPAD_LEFT]),
	INT_BIND("key_right", button_keycodes[BTN_ID_DPAD_RIGHT]),
	INT_BIND("key_a", button_keycodes[BTN_ID_A]),
	INT_BIND("key_b", button_keycodes[BTN_ID_B]),
	INT_BIND("key_c", button_keycodes[BTN_ID_C]),
	INT_BIND("key_x", button_keycodes[BTN_ID_X]),
	INT_BIND("key_y", button_keycodes[BTN_ID_Y]),
	INT_BIND("key_z", button_keycodes[BTN_ID_Z]),
	INT_BIND("key_l1", button_keycodes[BTN_ID_L1]),
	INT_BIND("key_r1", button_keycodes[BTN_ID_R1]),
	INT_BIND("key_l2", button_keycodes[BTN_ID_L2]),
	INT_BIND("key_r2", button_keycodes[BTN_ID_R2]),
	INT_BIND("key_l3", button_keycodes[BTN_ID_L3]),
	INT_BIND("key_r3", button_keycodes[BTN_ID_R3]),
	INT_BIND("key_start", button_keycodes[BTN_ID_START]),
	INT_BIND("key_select", button_keycodes[BTN_ID_SELECT]),
	INT_BIND("key_menu", button_keycodes[BTN_ID_MENU]),
	INT_BIND("key_power", button_keycodes[BTN_ID_POWER]),
	INT_BIND("key_vol_up", button_keycodes[BTN_ID_PLUS]),
	INT_BIND("key_vol_down", button_keycodes[BTN_ID_MINUS]),
	INT_BIND("input_axis_lx", axis_lx),
	INT_BIND("input_axis_ly", axis_ly),
	INT_BIND("input_axis_rx", axis_rx),
	INT_BIND("input_axis_ry", axis_ry),
	INT_BIND("input_axis_min", axis_min),
	INT_BIND("input_axis_center", axis_center),
	INT_BIND("input_axis_max", axis_max),
	INT_BIND("input_axis_lx_invert", axis_lx_invert),
	INT_BIND("input_axis_ly_invert", axis_ly_invert),
	INT_BIND("input_axis_rx_invert", axis_rx_invert),
	INT_BIND("input_axis_ry_invert", axis_ry_invert),
	STR_BIND("wifi_interface", wifi_interface),
	STR_BIND("bluetooth_interface", bluetooth_interface),
	STR_BIND("power_battery_sysfs", power_battery_sysfs),
	STR_BIND("power_charger_online_path", power_charger_online_path),
	STR_BIND("power_led_path", power_led_path),
};

#define TRAIT_FIELD_COUNT (sizeof(TRAIT_FIELDS) / sizeof(TRAIT_FIELDS[0]))

//////////////////////////////////////
// Value Parsers & Helpers

static void copyText(char* dst, size_t size, const char* src) {
	if (!dst || !size)
		return;
	snprintf(dst, size, "%s", src ? src : "");
}

static int parseInt(const char* value) {
	char* end;
	long parsed;

	if (!value || !strcmp(value, NA))
		return -1;
	parsed = strtol(value, &end, 10);
	return *end ? -1 : (int)parsed;
}

static const TraitField* findField(const char* key) {
	for (size_t i = 0; i < TRAIT_FIELD_COUNT; i++) {
		if (!strcmp(key, TRAIT_FIELDS[i].key))
			return &TRAIT_FIELDS[i];
	}
	return NULL;
}

static MinimeScreenAspect parseAspect(const char* value) {
	if (!value) return MINIME_ASPECT_UNKNOWN;
	if (!strcmp(value, "4:3")) return MINIME_ASPECT_4x3;
	if (!strcmp(value, "3:2")) return MINIME_ASPECT_3x2;
	if (!strcmp(value, "16:9")) return MINIME_ASPECT_16x9;
	if (!strcmp(value, "1:1")) return MINIME_ASPECT_1x1;
	return MINIME_ASPECT_UNKNOWN;
}

static int setValue(const char* key, const char* value) {
	const TraitField* field = findField(key);
	if (!field)
		return 0; // Unused trait, silently ignored

	if (field->type == TYPE_STRING) {
		copyText((char*)field->dest, field->max_len, value);
		return 0;
	}

	if (field->type == TYPE_ASPECT) {
		*(MinimeScreenAspect*)field->dest = parseAspect(value);
		return 0;
	}

	int parsed = parseInt(value);
	if (parsed < 0 && strcmp(value, NA)) {
		fprintf(stderr, "Minime traits: invalid integer '%s' for '%s'\n", value, key);
		return -1;
	}
	*(int*)field->dest = parsed;
	return 0;
}

int MINIME_traitAvailable(const char* value) {
	return value && value[0] && strcmp(value, NA);
}

//////////////////////////////////////
// Trait Validation

static int validateRequiredKeys(void) {
	const int required[] = {
		button_keycodes[BTN_ID_DPAD_UP], button_keycodes[BTN_ID_DPAD_DOWN],
		button_keycodes[BTN_ID_DPAD_LEFT], button_keycodes[BTN_ID_DPAD_RIGHT],
		button_keycodes[BTN_ID_A], button_keycodes[BTN_ID_B],
		button_keycodes[BTN_ID_X], button_keycodes[BTN_ID_Y],
		button_keycodes[BTN_ID_START], button_keycodes[BTN_ID_SELECT],
		button_keycodes[BTN_ID_MENU], button_keycodes[BTN_ID_POWER],
		button_keycodes[BTN_ID_PLUS], button_keycodes[BTN_ID_MINUS]};
	for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); i++) {
		if (required[i] < 0) return 0;
	}
	return 1;
}

static int validateDisplay(void) {
	return (screen_width > 0 && screen_height > 0 &&
			screen_rotation >= 0 &&
			MINIME_traitAvailable(gpu_device) &&
			MINIME_traitAvailable(screen_backlight_path) &&
			screen_backlight_max > 0);
}

static int validateInputs(void) {
	return (MINIME_traitAvailable(input_gamepad) &&
			MINIME_traitAvailable(input_power) &&
			MINIME_traitAvailable(input_volume) &&
			validateRequiredKeys());
}

static int validate(void) {
	if (!device_id[0] || !device_model[0] ||
		!validateDisplay() || !validateInputs()) {
		fprintf(stderr, "Invalid required Minime traits in %s\n", TRAITS_PATH);
		return -1;
	}
	return 0;
}

//////////////////////////////////////
// Initialization & Loading

static void initTraitDefaults(void) {
	for (int i = 0; i < BTN_ID_COUNT; i++) button_keycodes[i] = -1;
	axis_lx = axis_ly = -1;
	axis_rx = axis_ry = -1;
	axis_min = axis_center = axis_max = -1;
	axis_hat_x = 16; // ABS_HAT0X
	axis_hat_y = 17; // ABS_HAT0Y
	screen_width = 640;
	screen_height = 480;
	screen_rotation = 0;
	screen_padding = 0;
	screen_row_count = 0;
	screen_aspect = MINIME_ASPECT_4x3;
	screen_refresh_rate = 60;
	screen_backlight_max = 255;
	gpu_hdmi_width = 1280;
	gpu_hdmi_height = 720;
	copyText(audio_card, sizeof(audio_card), "default");
	copyText(audio_mixer, sizeof(audio_mixer), "Master");
}

static int parseTraitsFile(const char* path) {
	FILE* file = fopen(path, "r");
	if (!file) {
		fprintf(stderr, "Missing Minime traits: %s\n", path);
		return -1;
	}

	char line[512];
	while (fgets(line, sizeof(line), file)) {
		char* key = trimWhitespace(line);
		if (!key[0] || key[0] == '#' || key[0] == '[')
			continue;
		char* value = strchr(key, '=');
		if (!value)
			continue;
		*value++ = '\0';
		if (setValue(trimWhitespace(key), trimWhitespace(value)) != 0) {
			fclose(file);
			return -1;
		}
	}
	fclose(file);
	return 0;
}

static void deriveFallbacks(void) {
	if (screen_padding <= 0)
		screen_padding = (screen_width >= 720) ? 40 : 10;
	if (screen_row_count <= 0)
		screen_row_count = (screen_width >= 720) ? 8 : 6;
}

int MINIME_traitsInit(void) {
	if (initialized)
		return valid ? 0 : -1;
	initialized = 1;
	initTraitDefaults();

	if (parseTraitsFile(TRAITS_PATH) != 0)
		return -1;

	deriveFallbacks();

	valid = (validate() == 0);
	return valid ? 0 : -1;
}

//////////////////////////////////////
// Hardware State Probing

int MINIME_isHDMIConnected(void) {
	if (!MINIME_traitAvailable(gpu_hdmi_state_path))
		return 0;
	char status[16] = "";
	getFile(gpu_hdmi_state_path, status, sizeof(status));
	if (status[0] != '\0') {
		if (prefixMatch("connected", status))
			return 1;
		if (prefixMatch("disconnected", status) || prefixMatch("unknown", status))
			return 0;
	}
	return getInt(gpu_hdmi_state_path);
}

int MINIME_inputOpenByName(const char* expected) {
	if (!MINIME_traitAvailable(expected))
		return -1;
	int fd = open(expected, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd >= 0)
		return fd;
	char name[256];
	char path[64];
	for (int i = 0; i < 32; i++) {
		snprintf(path, sizeof(path), "/dev/input/event%d", i);
		fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0)
			continue;
		if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0 && !strcmp(name, expected))
			return fd;
		close(fd);
	}
	return -1;
}
