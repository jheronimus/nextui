#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <linux/input.h>

#include <msettings.h>

#include "settings.h"
#include "traits.h"
#include "utils.h"

// for ev.value
#define RELEASED 0
#define PRESSED 1
#define REPEAT 2

#define SW_HEADPHONE_INSERT 0x02
#define AUDIO_SH "/usr/share/minime/scripts/audio.sh"
#define BLUETOOTHD_PID "/run/bluetoothd.pid"

static int input_fds[7] = {0};
static int input_count = 0;

static uint32_t now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

//////////////////////////////////////
// Background State Monitors

static pthread_t hdmi_pt;
static pthread_t power_pt;
static pthread_t bt_pt;

static void* watchHDMI(void* arg) {
	(void)arg;
	int has_hdmi = MINIME_isHDMIConnected();
	int had_hdmi = has_hdmi;
	SetHDMI(has_hdmi);

	if (!MINIME_traitAvailable(gpu_hdmi_state_path))
		return NULL;

	while (1) {
		sleep(2);
		has_hdmi = MINIME_isHDMIConnected();
		if (had_hdmi != has_hdmi) {
			had_hdmi = has_hdmi;
			SetHDMI(has_hdmi);
		}
	}
	return NULL;
}

static int estimateCapacityFromVoltage(const char* base_path) {
	char path[MINIME_TRAIT_PATH_MAX + 32];
	snprintf(path, sizeof(path), "%s/voltage_avg", base_path);
	int volt = getInt(path);
	if (volt <= 0)
		return 0;
	int pct = (volt - 3400000) * 100 / (4172000 - 3400000);
	if (pct > 100)
		return 100;
	if (pct < 0)
		return 0;
	return pct;
}

static int getBatteryStatus(int* charging, int* capacity) {
	if (!MINIME_traitAvailable(power_battery_sysfs) || !charging || !capacity)
		return -1;

	char path[MINIME_TRAIT_PATH_MAX + 32];
	snprintf(path, sizeof(path), "%s/capacity", power_battery_sysfs);
	*capacity = getInt(path);

	if (*capacity <= 0)
		*capacity = estimateCapacityFromVoltage(power_battery_sysfs);

	if (MINIME_traitAvailable(power_charger_online_path)) {
		*charging = getInt(power_charger_online_path);
		return 0;
	}

	snprintf(path, sizeof(path), "%s/status", power_battery_sysfs);
	FILE* f = fopen(path, "r");
	if (!f)
		return -1;
	char status[32] = "";
	(void)fgets(status, sizeof(status), f);
	fclose(f);
	*charging = (!strncmp(status, "Charging", 8) || !strncmp(status, "Full", 4));
	return 0;
}

static void* watchPower(void* arg) {
	while (1) {
		int charging = 0;
		int battery = 0;

		getBatteryStatus(&charging, &battery);
		SetCharging(charging);
		SetBattery(battery);
		sleep(2);
	}
	return 0;
}

static int find_bt_sink(char* out, size_t out_size) {
	FILE* p;
	char buf[1024];

	out[0] = '\0';
	// Delegate to BlueALSA via D-Bus: BlueALSA is the single source of truth
	// for connected audio endpoints (managed objects under /org/bluealsa).
	p = popen("gdbus call --system --dest org.bluealsa --object-path /org/bluealsa "
			  "--method org.freedesktop.DBus.ObjectManager.GetManagedObjects 2>/dev/null",
			  "r");
	if (!p)
		return 0;
	while (fgets(buf, sizeof(buf), p)) {
		char* dev = strstr(buf, "/dev_");
		if (dev) {
			dev += 5; // skip "/dev_"
			if (strlen(dev) >= 17) {
				for (int i = 0; i < 17 && i < (int)out_size - 1; i++) {
					out[i] = (dev[i] == '_') ? ':' : dev[i];
				}
				out[17] = '\0';
				break;
			}
		}
	}
	pclose(p);
	return out[0] != '\0';
}

static void* watchBT(void* arg) {
	char active[64] = "";
	char mac[64];

	for (;;) {
		// bluetoothd runs only when the user enabled Bluetooth; skip the
		// poll otherwise to avoid querying D-Bus pointlessly.
		if (access(BLUETOOTHD_PID, F_OK) == 0) {
			if (find_bt_sink(mac, sizeof(mac))) {
				if (strcmp(mac, active) != 0) {
					char cmd[512];

					snprintf(cmd, sizeof(cmd), "%s start-interface bluetooth %s >/dev/null 2>&1",
							 AUDIO_SH, mac);
					system(cmd);
					strncpy(active, mac, sizeof(active) - 1);
					SetBT(1);
				}
			} else if (active[0]) {
				char cmd[512];

				snprintf(cmd, sizeof(cmd), "%s start-interface %s >/dev/null 2>&1", AUDIO_SH,
						 GetJack() ? "headphones" : "speakers");
				system(cmd);
				active[0] = '\0';
				SetBT(0);
			}
		}
		sleep(2);
	}
	return NULL;
}

//////////////////////////////////////
// Input Device Management

static void initInputDevices(void) {
	const char* dev_names[] = {
		input_gamepad,
		input_stick,
		input_power,
		input_volume,
		input_menu,
		audio_jack_device_name,
	};
	input_count = 0;
	for (size_t i = 0; i < sizeof(dev_names) / sizeof(dev_names[0]) &&
					   (size_t)input_count < (sizeof(input_fds) / sizeof(input_fds[0]));
		 i++) {
		int fd = MINIME_inputOpenByName(dev_names[i]);
		if (fd >= 0)
			input_fds[input_count++] = fd;
	}
}

//////////////////////////////////////
// Event Polling & Key Repeat Handling

typedef struct {
	uint32_t menu_pressed;
	uint32_t up_pressed;
	uint32_t up_just_pressed;
	uint32_t up_repeat_at;
	uint32_t down_pressed;
	uint32_t down_just_pressed;
	uint32_t down_repeat_at;
} KeymonState;

static void resetState(KeymonState* state) {
	state->menu_pressed = 0;
	state->up_pressed = state->up_just_pressed = 0;
	state->down_pressed = state->down_just_pressed = 0;
	state->up_repeat_at = state->down_repeat_at = 0;
}

static void handleKeyEvent(int code, int val, int menu_code, KeymonState* state, uint32_t now) {
	if (code == menu_code) {
		state->menu_pressed = val;
	} else if (code == button_keycodes[BTN_ID_PLUS]) {
		state->up_pressed = state->up_just_pressed = val;
		if (val)
			state->up_repeat_at = now + 300;
	} else if (code == button_keycodes[BTN_ID_MINUS]) {
		state->down_pressed = state->down_just_pressed = val;
		if (val)
			state->down_repeat_at = now + 300;
	}
}

static void pollInputFd(int fd, int menu_code, KeymonState* state, uint32_t now) {
	struct input_event ev;
	while (read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
		if (ev.type == EV_SW && ev.code == SW_HEADPHONE_INSERT) {
			SetJack(ev.value);
		} else if (ev.type == EV_KEY && ev.value <= REPEAT) {
			handleKeyEvent(ev.code, ev.value, menu_code, state, now);
		}
	}
}

static void pollInputs(int menu_code, KeymonState* state, uint32_t now) {
	for (int i = 0; i < input_count; i++) {
		if (input_fds[i] > 0)
			pollInputFd(input_fds[i], menu_code, state, now);
	}
}

static void adjustVolumeOrBrightness(int is_up, int menu_pressed) {
	if (menu_pressed) {
		int val = GetBrightness() + (is_up ? 1 : -1);
		if (val >= BRIGHTNESS_MIN && val <= BRIGHTNESS_MAX)
			SetBrightness(val);
	} else {
		int val = GetVolume() + (is_up ? 1 : -1);
		if (val >= VOLUME_MIN && val <= VOLUME_MAX)
			SetVolume(val);
	}
}

static void handleButtonRepeats(KeymonState* state, uint32_t now) {
	if (state->up_just_pressed || (state->up_pressed && now >= state->up_repeat_at)) {
		adjustVolumeOrBrightness(1, state->menu_pressed);
		if (state->up_just_pressed)
			state->up_just_pressed = 0;
		else
			state->up_repeat_at += 100;
	}

	if (state->down_just_pressed || (state->down_pressed && now >= state->down_repeat_at)) {
		adjustVolumeOrBrightness(0, state->menu_pressed);
		if (state->down_just_pressed)
			state->down_just_pressed = 0;
		else
			state->down_repeat_at += 100;
	}
}

//////////////////////////////////////
// Keymon Daemon Entry

int main(int argc, char* argv[]) {
	(void)argc;
	(void)argv;
	if (MINIME_traitsInit() != 0)
		return 1;
	InitSettings();
	pthread_create(&hdmi_pt, NULL, &watchHDMI, NULL);
	pthread_create(&power_pt, NULL, &watchPower, NULL);
	pthread_create(&bt_pt, NULL, &watchBT, NULL);

	initInputDevices();

	int menu_code = (button_keycodes[BTN_ID_MENU] >= 0 ? button_keycodes[BTN_ID_MENU]
													   : button_keycodes[BTN_ID_SELECT]);
	KeymonState state = {0};
	uint32_t then = now_ms();

	while (1) {
		uint32_t now = now_ms();
		if (now - then > 1000) {
			resetState(&state);
		} else {
			pollInputs(menu_code, &state, now);
			handleButtonRepeats(&state, now);
		}

		then = now;
		usleep(16666); // 60fps
	}
	return 0;
}