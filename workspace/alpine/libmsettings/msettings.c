/*
 * Minime libmsettings.
 * Shared-memory settings bridge (same contract as upstream).
 * Hardware access is traits-driven and lives in platform.c/traits.c.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include "utils.h"

#include "msettings.h"
#include "settings.h"
#include "traits.h"

#define AUDIO_SH "/usr/share/minime/scripts/audio.sh"

//////////////////////////////////////
// Shared Memory Lifecycle

typedef struct {
	int brightness;
	int volume;
	int jack;
	int hdmi;
	int mute;
	int charging;
	int battery;
	int bt;
	int size;
} SharedSettings;

static SharedSettings* shared = NULL;
static int shm_fd = -1;

static int isAudioJackConnected(void) {
	int fd = MINIME_inputOpenByName(audio_jack_device_name);
	if (fd < 0)
		return 0;
	unsigned char switches[1] = {0};
	int connected = 0;
	if (ioctl(fd, EVIOCGSW(sizeof(switches)), switches) >= 0) {
		connected = (switches[0] & (1 << SW_HEADPHONE_INSERT)) ? 1 : 0;
	}
	close(fd);
	return connected;
}

void InitSettings(void) {
	shm_fd = shm_open("/SharedSettings", O_RDWR | O_CREAT, 0666);
	if (shm_fd < 0)
		return;

	SharedSettings proto = {.size = sizeof(SharedSettings)};
	ftruncate(shm_fd, sizeof(SharedSettings));
	shared = mmap(NULL, sizeof(SharedSettings), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
	if (shared == MAP_FAILED) {
		shared = NULL;
		return;
	}

	if (shared->size == 0) {
		memcpy(shared, &proto, sizeof(SharedSettings));
		SetBrightness(5);
		SetVolume(10);
	}
	SetJack(isAudioJackConnected());
}

void QuitSettings(void) {
	if (shared) {
		munmap(shared, sizeof(SharedSettings));
		shared = NULL;
	}
	if (shm_fd >= 0) {
		close(shm_fd);
		shm_fd = -1;
	}
}

//////////////////////////////////////
// Display & Audio Hardware Controls

void SetRawBrightness(int value) {
	if (MINIME_traitAvailable(screen_backlight_path))
		putInt(screen_backlight_path, value);
}

void SetBrightness(int value) {
	if (!shared)
		return;
	if (value < BRIGHTNESS_MIN)
		value = BRIGHTNESS_MIN;
	if (value > BRIGHTNESS_MAX)
		value = BRIGHTNESS_MAX;
	shared->brightness = value;

	int max = (screen_backlight_max > 0) ? screen_backlight_max : 255;
	int raw = (value * max) / BRIGHTNESS_MAX;
	if (value > 0 && raw == 0)
		raw = 1;
	SetRawBrightness(raw);
}

void SetRawVolume(int value) {
	char card_flag[32] = "";
	if (strcmp(audio_card, "default") != 0) {
		snprintf(card_flag, sizeof(card_flag), "-c '%s' ", audio_card);
	}
	char command[256];
	snprintf(command, sizeof(command), "amixer -q %ssset '%s' %d%% unmute >/dev/null 2>&1",
			 card_flag, audio_mixer, value);
	(void)system(command);
}

void SetVolume(int value) {
	if (!shared)
		return;
	if (value < VOLUME_MIN)
		value = VOLUME_MIN;
	if (value > VOLUME_MAX)
		value = VOLUME_MAX;
	shared->volume = value;

	int raw = (value == 0) ? 0 : 60 + ((value - 1) * 40) / (VOLUME_MAX - 1);
	SetRawVolume(raw);
}

//////////////////////////////////////
// State Getters & Setters

int GetBrightness(void) {
	return shared ? shared->brightness : 5;
}

int GetVolume(void) {
	return shared ? shared->volume : 10;
}

int GetJack(void) {
	return shared ? shared->jack : 0;
}

void SetJack(int value) {
	if (shared)
		shared->jack = value;
	char cmd[256];
	snprintf(cmd, sizeof(cmd), "%s start-interface %s >/dev/null 2>&1", AUDIO_SH,
			 value ? "headphones" : "speakers");
	(void)system(cmd);
}

int GetHDMI(void) {
	return shared ? shared->hdmi : 0;
}

void SetHDMI(int value) {
	if (shared)
		shared->hdmi = value;
	char cmd[256];
	if (value) {
		snprintf(cmd, sizeof(cmd), "%s start-interface hdmi >/dev/null 2>&1", AUDIO_SH);
	} else {
		snprintf(cmd, sizeof(cmd), "%s start-interface %s >/dev/null 2>&1", AUDIO_SH,
				 (shared && shared->jack) ? "headphones" : "speakers");
	}
	(void)system(cmd);
}

int GetMute(void) {
	return shared ? shared->mute : 0;
}

void SetMute(int value) {
	if (shared)
		shared->mute = value;
}

int GetCharging(void) {
	return shared ? shared->charging : 0;
}

void SetCharging(int value) {
	if (shared)
		shared->charging = value;
}

int GetBattery(void) {
	return shared ? shared->battery : 0;
}

void SetBattery(int value) {
	if (shared)
		shared->battery = value;
}

int GetBT(void) {
	return shared ? shared->bt : 0;
}

void SetBT(int value) {
	if (shared)
		shared->bt = value;
}
