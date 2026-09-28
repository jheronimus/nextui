// minime platform
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <msettings.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

// clang-format off
#include "defines.h"
#include "platform.h"
#include "api.h"
#include "scaler.h"
#include "traits.h"
#include "utils.h"
// clang-format on

#include <linux/input.h>

//////////////////////////////////////
// Platform Lifecycle & Device Traits

int on_hdmi = 0;

static inline void load_traits(void) {
	if (MINIME_traitsInit() != 0) exit(1);
}

static inline void ensure_traits(void) {
	if (device_id[0] == '\0') {
		load_traits();
	}
}

static int openShortcutDevices(int *fds, size_t max_fds) {
	if (!fds) return 0;
	const char *names[] = {
		input_gamepad, input_stick, input_power, input_volume, input_menu,
	};
	int count = 0;
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]) && (size_t)count < max_fds; i++) {
		int fd = MINIME_inputOpenByName(names[i]);
		if (fd >= 0) fds[count++] = fd;
	}
	return count;
}

static int normalizeAxis(int value, int invert) {
	if (axis_min >= axis_center || axis_center >= axis_max) return 0;
	int normalized;
	if (value < axis_center) {
		normalized = -((axis_center - value) * 32767) / (axis_center - axis_min);
	} else {
		normalized = ((value - axis_center) * 32767) / (axis_max - axis_center);
	}
	return invert ? -normalized : normalized;
}

int PLAT_getScreenRotation(void) { return screen_rotation; }

//////////////////////////////////////
// Input Handling & Gamepad

#define INPUT_COUNT 5
static int inputs[INPUT_COUNT];

static void updateButtonState(int btn, int pressed, int id, uint32_t tick) {
	if (btn == BTN_NONE || id < 0 || id >= BTN_ID_COUNT) return;

	if (pressed) {
		if ((pad.is_pressed & btn) == BTN_NONE) {
			pad.just_pressed |= btn;
			pad.just_repeated |= btn;
			pad.is_pressed |= btn;
			pad.repeat_at[id] = tick + PAD_REPEAT_DELAY;
		}
	} else if (pad.is_pressed & btn) {
		pad.just_released |= btn;
		pad.is_pressed &= ~btn;
	}
}

static void drainInputFd(int input) {
	if (input < 0) return;
	struct input_event event;
	while (read(input, &event, sizeof(event)) == sizeof(event)) {}
}

static void drainAllInputs(void) {
	for (int i = 0; i < INPUT_COUNT; i++) {
		drainInputFd(inputs[i]);
	}
}

void PLAT_initInput(void) {
	ensure_traits();
	for (int i = 0; i < INPUT_COUNT; i++)
		inputs[i] = -1;

	openShortcutDevices(inputs, INPUT_COUNT);
	for (int i = 0; i < INPUT_COUNT; i++) {
		if (inputs[i] >= 0) fcntl(inputs[i], F_SETFL, O_NONBLOCK);
	}

	drainAllInputs();
}

void PLAT_quitInput(void) {
	for (int i = 0; i < INPUT_COUNT; i++) {
		if (inputs[i] >= 0) {
			close(inputs[i]);
			inputs[i] = -1;
		}
	}
}

static int checkPowerRelease(int fd) {
	struct input_event event;
	while (read(fd, &event, sizeof(event)) == sizeof(event)) {
		if (event.type == EV_KEY && event.code == button_keycodes[BTN_ID_POWER] && event.value == 0) {
			return 1;
		}
	}
	return 0;
}

static void handleKeyEvent(int code, int pressed, uint32_t tick) {
	if (code == button_keycodes[BTN_ID_SELECT] && button_keycodes[BTN_ID_MENU] < 0) {
		updateButtonState(BTN_MENU, pressed, BTN_ID_MENU, tick);
	}

	for (int id = 0; id < BTN_ID_COUNT; id++) {
		if (button_keycodes[id] == code) {
			updateButtonState(1 << id, pressed, id, tick);
		}
	}
}

static void handleAbsEvent(int code, int value) {
	if (code == axis_lx) {
		pad.laxis.x = normalizeAxis(value, axis_lx_invert);
	} else if (code == axis_ly) {
		pad.laxis.y = normalizeAxis(value, axis_ly_invert);
	} else if (code == axis_rx) {
		pad.raxis.x = normalizeAxis(value, axis_rx_invert);
	} else if (code == axis_ry) {
		pad.raxis.y = normalizeAxis(value, axis_ry_invert);
	}
}

static void pollInputEvents(int fd, uint32_t tick) {
	struct input_event event;
	while (read(fd, &event, sizeof(event)) == sizeof(event)) {
		if (event.type == EV_KEY) {
			handleKeyEvent(event.code, event.value, tick);
		} else if (event.type == EV_ABS) {
			handleAbsEvent(event.code, event.value);
		}
	}
}

void PLAT_pollInput(void) {
	uint32_t tick = SDL_GetTicks();
	pad.just_pressed = BTN_NONE;
	pad.just_released = BTN_NONE;
	pad.just_repeated = BTN_NONE;

	if (PLAT_hasLeftStick()) {
		PAD_setAnalog(BTN_ID_ANALOG_LEFT, BTN_ID_ANALOG_RIGHT, pad.laxis.x, tick + PAD_REPEAT_DELAY);
		PAD_setAnalog(BTN_ID_ANALOG_UP, BTN_ID_ANALOG_DOWN, pad.laxis.y, tick + PAD_REPEAT_DELAY);
	}

	for (int id = 0; id < BTN_ID_COUNT; id++) {
		int btn = 1 << id;
		if ((pad.is_pressed & btn) && (tick >= pad.repeat_at[id])) {
			pad.just_repeated |= btn;
			pad.repeat_at[id] = tick + PAD_REPEAT_INTERVAL;
		}
	}

	for (int i = 0; i < INPUT_COUNT; i++) {
		if (inputs[i] >= 0) {
			pollInputEvents(inputs[i], tick);
		}
	}
}

int PLAT_shouldWake(void) {
	int lid_open = 1;
	if (lid.has_lid && PLAT_lidChanged(&lid_open) && lid_open) return 1;

	for (int i = 0; i < INPUT_COUNT; i++) {
		if (inputs[i] >= 0 && checkPowerRelease(inputs[i])) {
			if (lid.has_lid && !lid.is_open) return 0;
			return 1;
		}
	}
	return 0;
}

int PLAT_is6Button(void) {
	ensure_traits();
	return (button_keycodes[BTN_ID_L3] >= 0 && button_keycodes[BTN_ID_R3] >= 0);
}

int PLAT_hasMenuButton(void) {
	ensure_traits();
	return button_keycodes[BTN_ID_MENU] >= 0;
}

int PLAT_hasL3(void) {
	ensure_traits();
	return button_keycodes[BTN_ID_L3] >= 0;
}

int PLAT_hasR3(void) {
	ensure_traits();
	return button_keycodes[BTN_ID_R3] >= 0;
}

int PLAT_hasLeftStick(void) {
	ensure_traits();
	return axis_lx >= 0;
}

int PLAT_hasRightStick(void) {
	ensure_traits();
	return axis_rx >= 0;
}

//////////////////////////////////////
// Clamshell Lid Sensor

static int lid_fd = -1;

int PLAT_hasLid(void) {
	ensure_traits();
	return MINIME_traitAvailable(input_lid);
}

void PLAT_initLid(void) {
	ensure_traits();
	lid_fd = MINIME_inputOpenByName(input_lid);
	lid.has_lid = lid_fd >= 0;
	if (lid.has_lid) {
		unsigned long sw[SW_MAX / 8 / sizeof(unsigned long) + 1] = {0};
		if (ioctl(lid_fd, EVIOCGSW(sizeof(sw)), sw) >= 0) lid.is_open = !((sw[0] >> SW_LID) & 1);
	}
}

int PLAT_lidChanged(int *state) {
	if (!lid.has_lid) return 0;
	struct input_event event;
	while (read(lid_fd, &event, sizeof(event)) == sizeof(event)) {
		if (event.type == EV_SW && event.code == SW_LID) {
			int lid_open = !event.value;
			if (lid_open != lid.is_open) {
				lid.is_open = lid_open;
				if (state) *state = lid_open;
				return 1;
			}
		}
	}
	return 0;
}

//////////////////////////////////////
// Wireless Network Interface

const char *PLAT_getWifiInterface(void) {
	ensure_traits();
	return MINIME_traitAvailable(wifi_interface) ? wifi_interface : "wlan0";
}

//////////////////////////////////////
// Power, Backlight & System Management

void PLAT_enableBacklight(int enable) {
	if (enable) {
		if (MINIME_traitAvailable(screen_blank_path)) putInt(screen_blank_path, 0);
		SetBrightness(GetBrightness());
		if (MINIME_traitAvailable(power_led_path)) putInt(power_led_path, 0);
	} else {
		if (MINIME_traitAvailable(screen_blank_path)) putInt(screen_blank_path, 4);
		SetRawBrightness(0);
		if (MINIME_traitAvailable(power_led_path)) putInt(power_led_path, 1);
	}
}

void PLAT_powerOff(int reboot) {
	system("rm -f /tmp/minui_exec && sync");
	sleep(1);

	SetRawVolume(MUTE_VOLUME_RAW);
	PLAT_enableBacklight(0);
	if (MINIME_traitAvailable(power_led_path)) putInt(power_led_path, 1);
	SND_quit();
	VIB_quit();
	PWR_quit();
	GFX_quit();

	if (reboot) {
		system("reboot");
	} else {
		system("poweroff");
	}
	exit(0);
}

//////////////////////////////////////
// Haptics

void PLAT_setRumble(int strength) {
	if (GetHDMI()) return;
	if (!MINIME_traitAvailable(input_rumble)) return;

	int fd = MINIME_inputOpenByName(input_rumble);
	if (fd < 0) return;

	struct ff_effect effect;
	memset(&effect, 0, sizeof(effect));
	effect.type = FF_RUMBLE;
	effect.id = -1;
	if (strength > 0) {
		effect.u.rumble.strong_magnitude = 0xffff;
		effect.u.rumble.weak_magnitude = 0xffff;
	}
	if (ioctl(fd, EVIOCSFF, &effect) < 0 && strength > 0) {
		if (errno != ENODEV) close(fd);
		return;
	}
	close(fd);
}

//////////////////////////////////////
// Audio

int PLAT_pickSampleRate(int requested, int max) { return MIN(requested, max); }

//////////////////////////////////////
// Battery, Thermal & Platform Hooks

void PLAT_getBatteryStatusFine(int *is_charging, int *charge) {
	ensure_traits();

	char path[MINIME_TRAIT_PATH_MAX];

	if (is_charging) {
		*is_charging = 0;
		if (power_charger_online_path[0] && strcmp(power_charger_online_path, "na") != 0)
			*is_charging = (getInt(power_charger_online_path) == 1);
	}
	if (charge) {
		*charge = 0;
		if (power_battery_sysfs[0] && strcmp(power_battery_sysfs, "na") != 0) {
			snprintf(path, sizeof(path), "%s/capacity", power_battery_sysfs);
			*charge = getInt(path);
		}
	}
}

void PLAT_getBatteryStatus(int *is_charging, int *charge) {
	PLAT_getBatteryStatusFine(is_charging, charge);
	if (!charge) return;

	if (*charge > 80)
		*charge = 100;
	else if (*charge > 60)
		*charge = 80;
	else if (*charge > 40)
		*charge = 60;
	else if (*charge > 20)
		*charge = 40;
	else if (*charge > 10)
		*charge = 20;
	else
		*charge = 10;
}

int PLAT_isUSBConnected(void) {
	ensure_traits();

	DIR *dir = opendir("/sys/class/udc");
	if (!dir) return 0;

	int found = 0;
	char state[64];
	struct dirent *e;
	while ((e = readdir(dir))) {
		if (e->d_name[0] == '.') continue;
		char path[MINIME_TRAIT_PATH_MAX];
		snprintf(path, sizeof(path), "/sys/class/udc/%s/state", e->d_name);
		getFile(path, state, sizeof(state));
		if (strcmp(state, "configured") == 0) {
			found = 1;
			break;
		}
	}
	closedir(dir);
	return found;
}

void PLAT_getOsVersionInfo(char *output_str, size_t max_len) {
	ensure_traits();
	getFile("/etc/nextui-version", output_str, max_len);
	if (!output_str[0]) snprintf(output_str, max_len, "NextUI %s", device_model);
}

char *PLAT_getModel(void) {
	ensure_traits();
	return device_model;
}

void PLAT_initDefaultLeds(void) {
	ensure_traits();
	memset(lightsDefault, 0, sizeof(lightsDefault));
}

static int writeSysfs(const char *path, const char *value) {
	FILE *f = fopen(path, "w");
	if (!f) return -1;
	int n = fprintf(f, "%s", value);
	fclose(f);
	return (n > 0) ? 0 : -1;
}

void PLAT_setCPUSpeed(int speed) {
	ensure_traits();
	if (!cpu_governor_path[0] || strcmp(cpu_governor_path, "na") == 0) return;

	const char *mode;
	switch (speed) {
	case CPU_SPEED_AUTO:
		mode = "schedutil";
		break;
	case CPU_SPEED_PERFORMANCE:
		mode = "performance";
		break;
	case CPU_SPEED_POWERSAVE:
		mode = "powersave";
		break;
	default:
		return;
	}
	writeSysfs(cpu_governor_path, mode);
}

void PLAT_getNetworkStatus(int *is_online) {
	if (is_online) *is_online = WIFI_connected() ? 1 : 0;
}

ConnectionStrength PLAT_connectionStrength(void) {
	struct WIFI_connection c = {0};
	if (!WIFI_enabled()) return SIGNAL_STRENGTH_OFF;
	WIFI_connectionInfo(&c);
	if (!c.valid || c.rssi == -1) return SIGNAL_STRENGTH_OFF;
	if (c.rssi == 0) return SIGNAL_STRENGTH_DISCONNECTED;
	if (c.rssi >= -60) return SIGNAL_STRENGTH_HIGH;
	if (c.rssi >= -70) return SIGNAL_STRENGTH_MED;
	return SIGNAL_STRENGTH_LOW;
}

//////////////////////////////////////
// Generic implementations

// We use the generic video implementation here
#include "generic_video.c"

//////////////////////////////////////

// We use the generic wifi implementation here
static inline void connection_reset(struct WIFI_connection *connection_info) {
	if (!connection_info) return;
	connection_info->valid = false;
	connection_info->freq = -1;
	connection_info->link_speed = -1;
	connection_info->noise = -1;
	connection_info->rssi = -1;
	connection_info->ip[0] = '\0';
	connection_info->ssid[0] = '\0';
}

#define WIFI_SOCK_DIR "/var/run/wpa_supplicant"
#include "generic_wifi.c"

//////////////////////////////////////

// We use the generic bluetooth implementation here
#include "generic_bt.c"
