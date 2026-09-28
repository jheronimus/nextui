// NextUI Alpine platform libmsettings
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <tinyalsa/mixer.h>
#include <unistd.h>

#include "displaycal.h"
#include "msettings.h"
#include "traits.h"
#include "utils.h"

///////////////////////////////////////

// Legacy MinUI settings
typedef struct SettingsV3 {
	int version;
	int brightness;
	int headphones;
	int speaker;
	int mute;
	int unused[2];
	int jack;
} SettingsV3;

typedef struct SettingsV4 {
	int version;
	int brightness;
	int colortemperature;
	int headphones;
	int speaker;
	int mute;
	int unused[2];
	int jack;
} SettingsV4;

typedef struct SettingsV5 {
	int version;
	int brightness;
	int colortemperature;
	int headphones;
	int speaker;
	int mute;
	int unused[2];
	int jack;
} SettingsV5;

typedef struct SettingsV6 {
	int version;
	int brightness;
	int colortemperature;
	int headphones;
	int speaker;
	int mute;
	int contrast;
	int saturation;
	int exposure;
	int unused[2];
	int jack;
} SettingsV6;

typedef struct SettingsV7 {
	int version;
	int brightness;
	int colortemperature;
	int headphones;
	int speaker;
	int mute;
	int contrast;
	int saturation;
	int exposure;
	int mutedbrightness;
	int mutedcolortemperature;
	int mutedcontrast;
	int mutedsaturation;
	int mutedexposure;
	int unused[2];
	int jack;
} SettingsV7;

typedef struct SettingsV8 {
	int version;
	int brightness;
	int colortemperature;
	int headphones;
	int speaker;
	int mute;
	int contrast;
	int saturation;
	int exposure;
	int toggled_brightness;
	int toggled_colortemperature;
	int toggled_contrast;
	int toggled_saturation;
	int toggled_exposure;
	int toggled_volume;
	int unused[2];
	int jack;
} SettingsV8;

typedef struct SettingsV9 {
	int version;
	int brightness;
	int colortemperature;
	int headphones;
	int speaker;
	int mute;
	int contrast;
	int saturation;
	int exposure;
	int toggled_brightness;
	int toggled_colortemperature;
	int toggled_contrast;
	int toggled_saturation;
	int toggled_exposure;
	int toggled_volume;
	int disable_dpad_on_mute;
	int emulate_joystick_on_mute;
	int turbo_a;
	int turbo_b;
	int turbo_x;
	int turbo_y;
	int turbo_l1;
	int turbo_l2;
	int turbo_r1;
	int turbo_r2;
	int unused[2];
	int jack;
} SettingsV9;

typedef struct SettingsV10 {
	int version;
	int brightness;
	int colortemperature;
	int headphones;
	int speaker;
	int mute;
	int contrast;
	int saturation;
	int exposure;
	int toggled_brightness;
	int toggled_colortemperature;
	int toggled_contrast;
	int toggled_saturation;
	int toggled_exposure;
	int toggled_volume;
	int disable_dpad_on_mute;
	int emulate_joystick_on_mute;
	int turbo_a;
	int turbo_b;
	int turbo_x;
	int turbo_y;
	int turbo_l1;
	int turbo_l2;
	int turbo_r1;
	int turbo_r2;
	int unused[2];
	int jack;
	int audiosink;
} SettingsV10;

typedef struct SettingsV11 {
	int version;
	int brightness;
	int colortemperature;
	int headphones;
	int speaker;
	int mute;
	int contrast;
	int saturation;
	int exposure;
	int toggled_brightness;
	int toggled_colortemperature;
	int toggled_contrast;
	int toggled_saturation;
	int toggled_exposure;
	int toggled_volume;
	int disable_dpad_on_mute;
	int emulate_joystick_on_mute;
	int turbo_a;
	int turbo_b;
	int turbo_x;
	int turbo_y;
	int turbo_l1;
	int turbo_l2;
	int turbo_r1;
	int turbo_r2;
	int unused[2];
	int jack;
	int audiosink;
	int displaycal_enabled;
	int displaycal_red_gain;
	int displaycal_green_gain;
	int displaycal_blue_gain;
} SettingsV11;

#define SETTINGS_VERSION 11
typedef SettingsV11 Settings;
static Settings DefaultSettings = {
	.version = SETTINGS_VERSION,
	.brightness = SETTINGS_DEFAULT_BRIGHTNESS,
	.colortemperature = SETTINGS_DEFAULT_COLORTEMP,
	.headphones = SETTINGS_DEFAULT_HEADPHONE_VOLUME,
	.speaker = SETTINGS_DEFAULT_VOLUME,
	.mute = 0,
	.contrast = SETTINGS_DEFAULT_CONTRAST,
	.saturation = SETTINGS_DEFAULT_SATURATION,
	.exposure = SETTINGS_DEFAULT_EXPOSURE,
	.toggled_brightness = SETTINGS_DEFAULT_MUTE_NO_CHANGE,
	.toggled_colortemperature = SETTINGS_DEFAULT_MUTE_NO_CHANGE,
	.toggled_contrast = SETTINGS_DEFAULT_MUTE_NO_CHANGE,
	.toggled_saturation = SETTINGS_DEFAULT_MUTE_NO_CHANGE,
	.toggled_exposure = SETTINGS_DEFAULT_MUTE_NO_CHANGE,
	.toggled_volume = 0,
	.disable_dpad_on_mute = 0,
	.emulate_joystick_on_mute = 0,
	.turbo_a = 0,
	.turbo_b = 0,
	.turbo_x = 0,
	.turbo_y = 0,
	.turbo_l1 = 0,
	.turbo_l2 = 0,
	.turbo_r1 = 0,
	.turbo_r2 = 0,
	.jack = 0,
	.audiosink = AUDIO_SINK_DEFAULT,
	.displaycal_enabled = DISPLAYCAL_DEFAULT_ENABLED,
	.displaycal_red_gain = DISPLAYCAL_DEFAULT_RED_GAIN,
	.displaycal_green_gain = DISPLAYCAL_DEFAULT_GREEN_GAIN,
	.displaycal_blue_gain = DISPLAYCAL_DEFAULT_BLUE_GAIN,
};
static Settings *settings;

#define SHM_KEY "/SharedSettings"
static char SettingsPath[256];
static int shm_fd = -1;
static int is_host = 0;
static int shm_size = sizeof(Settings);

int scaleBrightness(int);
int scaleColortemp(int);
int scaleContrast(int);
int scaleSaturation(int);
int scaleExposure(int);
int scaleVolume(int);

void disableDpad(int);
void emulateJoystick(int);
void turboA(int);
void turboB(int);
void turboX(int);
void turboY(int);
void turboL1(int);
void turboL2(int);
void turboR1(int);
void turboR2(int);

static int peekVersion(const char *filename) {
	int version = 0;
	FILE *file = fopen(filename, "r");
	if (file) {
		if (fread(&version, sizeof(int), 1, file) != 1) version = 0;
		fclose(file);
	}
	return version;
}

static void applyDisplayCalDefaultsForDevice(Settings *target) {
	DisplayCalDefaults defaults = DisplayCal_getDefaultSettings(DISPLAYCAL_PRESET_DEFAULT);
	target->displaycal_enabled = defaults.enabled;
	target->displaycal_red_gain = defaults.red_gain;
	target->displaycal_green_gain = defaults.green_gain;
	target->displaycal_blue_gain = defaults.blue_gain;
}

void InitSettings(void) {
	MINIME_traitsInit();
	applyDisplayCalDefaultsForDevice(&DefaultSettings);

	const char *userdata = getenv("USERDATA_PATH");
	if (userdata) {
		snprintf(SettingsPath, sizeof(SettingsPath), "%s/msettings.bin", userdata);
	} else {
		snprintf(SettingsPath, sizeof(SettingsPath), "/mnt/sdcard/.userdata/alpine/msettings.bin");
	}

	shm_fd = shm_open(SHM_KEY, O_RDWR | O_CREAT | O_EXCL, 0644);
	if (shm_fd == -1 && errno == EEXIST) {
		shm_fd = shm_open(SHM_KEY, O_RDWR, 0644);
		if (shm_fd >= 0) {
			settings = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
		}
	} else if (shm_fd >= 0) {
		is_host = 1;
		ftruncate(shm_fd, shm_size);
		settings = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
		if (settings) {
			int version = peekVersion(SettingsPath);
			if (version == SETTINGS_VERSION) {
				int fd = open(SettingsPath, O_RDONLY);
				if (fd >= 0) {
					read(fd, settings, shm_size);
					close(fd);
				}
			} else {
				memcpy(settings, &DefaultSettings, shm_size);
			}
			settings->mute = 0;
		}
	}

	if (settings) {
		SetMute(settings->mute);
	}
}

int InitializedSettings(void) { return (settings != NULL); }

void QuitSettings(void) {
	if (settings) {
		if (is_host) {
			int fd = open(SettingsPath, O_CREAT | O_WRONLY | O_TRUNC, 0644);
			if (fd >= 0) {
				write(fd, settings, shm_size);
				close(fd);
				sync();
			}
		}
		munmap(settings, shm_size);
		settings = NULL;
	}
	if (shm_fd >= 0) {
		close(shm_fd);
		if (is_host) shm_unlink(SHM_KEY);
		shm_fd = -1;
	}
}

static inline void SaveSettings(void) {
	if (!settings) return;
	int fd = open(SettingsPath, O_CREAT | O_WRONLY, 0644);
	if (fd >= 0) {
		write(fd, settings, shm_size);
		close(fd);
		sync();
	}
}

static inline void applyDisplayCalSettings(void) {
	if (settings && settings->displaycal_enabled) {
		SetRawDisplayCal(1, settings->displaycal_red_gain, settings->displaycal_green_gain,
						 settings->displaycal_blue_gain);
	}
}

// Getters
int GetBrightness(void) {
	if (!settings) return SETTINGS_DEFAULT_BRIGHTNESS;
	if (settings->mute && GetMutedBrightness() != SETTINGS_DEFAULT_MUTE_NO_CHANGE) return GetMutedBrightness();
	return settings->brightness;
}

int GetColortemp(void) {
	if (!settings) return SETTINGS_DEFAULT_COLORTEMP;
	if (settings->mute && GetMutedColortemp() != SETTINGS_DEFAULT_MUTE_NO_CHANGE) return GetMutedColortemp();
	return settings->colortemperature;
}

int GetVolume(void) {
	if (!settings) return SETTINGS_DEFAULT_VOLUME;
	if (settings->mute && GetMutedVolume() != SETTINGS_DEFAULT_MUTE_NO_CHANGE) return GetMutedVolume();
	if (settings->jack || settings->audiosink != AUDIO_SINK_DEFAULT) return settings->headphones;
	return settings->speaker;
}

int GetContrast(void) {
	if (!settings) return SETTINGS_DEFAULT_CONTRAST;
	if (settings->mute && GetMutedContrast() != SETTINGS_DEFAULT_MUTE_NO_CHANGE) return GetMutedContrast();
	return settings->contrast;
}

int GetSaturation(void) {
	if (!settings) return SETTINGS_DEFAULT_SATURATION;
	if (settings->mute && GetMutedSaturation() != SETTINGS_DEFAULT_MUTE_NO_CHANGE) return GetMutedSaturation();
	return settings->saturation;
}

int GetExposure(void) {
	if (!settings) return SETTINGS_DEFAULT_EXPOSURE;
	if (settings->mute && GetMutedExposure() != SETTINGS_DEFAULT_MUTE_NO_CHANGE) return GetMutedExposure();
	return settings->exposure;
}

int GetDisplayCalEnabled(void) { return settings ? settings->displaycal_enabled : 0; }
int GetDisplayCalRedGain(void) { return settings ? settings->displaycal_red_gain : 100; }
int GetDisplayCalGreenGain(void) { return settings ? settings->displaycal_green_gain : 100; }
int GetDisplayCalBlueGain(void) { return settings ? settings->displaycal_blue_gain : 100; }

int GetJack(void) { return settings ? settings->jack : 0; }

int GetAudioSink(void) { return settings ? settings->audiosink : AUDIO_SINK_DEFAULT; }

int GetHDMI(void) {
	if (gpu_hdmi_state_path[0]) {
		char buf[16] = {0};
		FILE *f = fopen(gpu_hdmi_state_path, "r");
		if (f) {
			if (fgets(buf, sizeof(buf), f)) {
				fclose(f);
				return (strncmp(buf, "connected", 9) == 0);
			}
			fclose(f);
		}
	}
	return MINIME_isHDMIConnected();
}

int GetMute(void) { return settings ? settings->mute : 0; }

int GetMutedBrightness(void) { return settings ? settings->toggled_brightness : 0; }
int GetMutedColortemp(void) { return settings ? settings->toggled_colortemperature : 0; }
int GetMutedContrast(void) { return settings ? settings->toggled_contrast : 0; }
int GetMutedSaturation(void) { return settings ? settings->toggled_saturation : 0; }
int GetMutedExposure(void) { return settings ? settings->toggled_exposure : 0; }
int GetMutedVolume(void) { return settings ? settings->toggled_volume : 0; }
int GetMuteDisablesDpad(void) { return settings ? settings->disable_dpad_on_mute : 0; }
int GetMuteEmulatesJoystick(void) { return settings ? settings->emulate_joystick_on_mute : 0; }
int GetMuteTurboA(void) { return settings ? settings->turbo_a : 0; }
int GetMuteTurboB(void) { return settings ? settings->turbo_b : 0; }
int GetMuteTurboX(void) { return settings ? settings->turbo_x : 0; }
int GetMuteTurboY(void) { return settings ? settings->turbo_y : 0; }
int GetMuteTurboL1(void) { return settings ? settings->turbo_l1 : 0; }
int GetMuteTurboL2(void) { return settings ? settings->turbo_l2 : 0; }
int GetMuteTurboR1(void) { return settings ? settings->turbo_r1 : 0; }
int GetMuteTurboR2(void) { return settings ? settings->turbo_r2 : 0; }

// Setters
void SetBrightness(int value) {
	if (!settings) return;
	if (settings->mute && GetMutedBrightness() != SETTINGS_DEFAULT_MUTE_NO_CHANGE)
		return SetRawBrightness(scaleBrightness(GetMutedBrightness()));
	SetRawBrightness(scaleBrightness(value));
	settings->brightness = value;
	SaveSettings();
}

void SetColortemp(int value) {
	if (!settings) return;
	if (settings->mute && GetMutedColortemp() != SETTINGS_DEFAULT_MUTE_NO_CHANGE)
		return SetRawColortemp(scaleColortemp(GetMutedColortemp()));
	SetRawColortemp(scaleColortemp(value));
	settings->colortemperature = value;
	SaveSettings();
}

void SetContrast(int value) {
	if (!settings) return;
	if (settings->mute && GetMutedContrast() != SETTINGS_DEFAULT_MUTE_NO_CHANGE)
		return SetRawContrast(scaleContrast(GetMutedContrast()));
	SetRawContrast(scaleContrast(value));
	settings->contrast = value;
	SaveSettings();
}

void SetSaturation(int value) {
	if (!settings) return;
	if (settings->mute && GetMutedSaturation() != SETTINGS_DEFAULT_MUTE_NO_CHANGE)
		return SetRawSaturation(scaleSaturation(GetMutedSaturation()));
	SetRawSaturation(scaleSaturation(value));
	settings->saturation = value;
	SaveSettings();
}

void SetExposure(int value) {
	if (!settings) return;
	if (settings->mute && GetMutedExposure() != SETTINGS_DEFAULT_MUTE_NO_CHANGE)
		return SetRawExposure(scaleExposure(GetMutedExposure()));
	SetRawExposure(scaleExposure(value));
	settings->exposure = value;
	SaveSettings();
}

void SetDisplayCalEnabled(int is_enabled) {
	if (!settings) return;
	int was_enabled = settings->displaycal_enabled;
	is_enabled = (is_enabled != 0);
	settings->displaycal_enabled = is_enabled;
	if (is_enabled)
		applyDisplayCalSettings();
	else if (was_enabled)
		SetRawDisplayCal(0, settings->displaycal_red_gain, settings->displaycal_green_gain,
						 settings->displaycal_blue_gain);
	SaveSettings();
}

void SetDisplayCalRedGain(int value) {
	if (!settings) return;
	value = DisplayCal_clampGainValue(value);
	settings->displaycal_red_gain = value;
	applyDisplayCalSettings();
	SaveSettings();
}

void SetDisplayCalGreenGain(int value) {
	if (!settings) return;
	value = DisplayCal_clampGainValue(value);
	settings->displaycal_green_gain = value;
	applyDisplayCalSettings();
	SaveSettings();
}

void SetDisplayCalBlueGain(int value) {
	if (!settings) return;
	value = DisplayCal_clampGainValue(value);
	settings->displaycal_blue_gain = value;
	applyDisplayCalSettings();
	SaveSettings();
}

void SetVolume(int value) {
	if (!settings) return;
	if (settings->mute && GetMutedVolume() != SETTINGS_DEFAULT_MUTE_NO_CHANGE)
		return SetRawVolume(scaleVolume(GetMutedVolume()));
	SetRawVolume(scaleVolume(value));
	if (settings->jack || settings->audiosink != AUDIO_SINK_DEFAULT)
		settings->headphones = value;
	else
		settings->speaker = value;
	SaveSettings();
}

void SetJack(int value) {
	if (!settings) return;
	settings->jack = value;
	SetVolume(GetVolume());
}

void SetAudioSink(int value) {
	if (!settings) return;
	settings->audiosink = value;
	SetVolume(GetVolume());
}

void SetHDMI(int value) { (void)value; }

void SetMute(int value) {
	if (!settings) return;
	settings->mute = value;

	SetVolume(GetVolume());
	SetBrightness(GetBrightness());
	SetColortemp(GetColortemp());
	SetContrast(GetContrast());
	SetSaturation(GetSaturation());
	SetExposure(GetExposure());
	applyDisplayCalSettings();

	disableDpad(settings->mute);
	emulateJoystick(settings->mute);
	turboA(settings->mute);
	turboB(settings->mute);
	turboX(settings->mute);
	turboY(settings->mute);
	turboL1(settings->mute);
	turboL2(settings->mute);
	turboR1(settings->mute);
	turboR2(settings->mute);
}

void SetMutedBrightness(int value) {
	if (settings) {
		settings->toggled_brightness = value;
		SaveSettings();
	}
}
void SetMutedColortemp(int value) {
	if (settings) {
		settings->toggled_colortemperature = value;
		SaveSettings();
	}
}
void SetMutedContrast(int value) {
	if (settings) {
		settings->toggled_contrast = value;
		SaveSettings();
	}
}
void SetMutedSaturation(int value) {
	if (settings) {
		settings->toggled_saturation = value;
		SaveSettings();
	}
}
void SetMutedExposure(int value) {
	if (settings) {
		settings->toggled_exposure = value;
		SaveSettings();
	}
}
void SetMutedVolume(int value) {
	if (settings) {
		settings->toggled_volume = value;
		SaveSettings();
	}
}
void SetMuteDisablesDpad(int value) {
	if (settings) {
		settings->disable_dpad_on_mute = value;
		SaveSettings();
	}
}
void SetMuteEmulatesJoystick(int value) {
	if (settings) {
		settings->emulate_joystick_on_mute = value;
		SaveSettings();
	}
}
void SetMuteTurboA(int value) {
	if (settings) {
		settings->turbo_a = value;
		SaveSettings();
	}
}
void SetMuteTurboB(int value) {
	if (settings) {
		settings->turbo_b = value;
		SaveSettings();
	}
}
void SetMuteTurboX(int value) {
	if (settings) {
		settings->turbo_x = value;
		SaveSettings();
	}
}
void SetMuteTurboY(int value) {
	if (settings) {
		settings->turbo_y = value;
		SaveSettings();
	}
}
void SetMuteTurboL1(int value) {
	if (settings) {
		settings->turbo_l1 = value;
		SaveSettings();
	}
}
void SetMuteTurboL2(int value) {
	if (settings) {
		settings->turbo_l2 = value;
		SaveSettings();
	}
}
void SetMuteTurboR1(int value) {
	if (settings) {
		settings->turbo_r1 = value;
		SaveSettings();
	}
}
void SetMuteTurboR2(int value) {
	if (settings) {
		settings->turbo_r2 = value;
		SaveSettings();
	}
}

#define INPUTD_PATH "/tmp/trimui_inputd"
#define INPUTD_DPAD_PATH "/tmp/trimui_inputd/input_no_dpad"
#define INPUTD_JOYSTICK_PATH "/tmp/trimui_inputd/input_dpad_to_joystick"
#define INPUTD_TURBO_A_PATH "/tmp/trimui_inputd/turbo_a"
#define INPUTD_TURBO_B_PATH "/tmp/trimui_inputd/turbo_b"
#define INPUTD_TURBO_X_PATH "/tmp/trimui_inputd/turbo_x"
#define INPUTD_TURBO_Y_PATH "/tmp/trimui_inputd/turbo_y"
#define INPUTD_TURBO_L1_PATH "/tmp/trimui_inputd/turbo_l1"
#define INPUTD_TURBO_L2_PATH "/tmp/trimui_inputd/turbo_l2"
#define INPUTD_TURBO_R1_PATH "/tmp/trimui_inputd/turbo_r1"
#define INPUTD_TURBO_R2_PATH "/tmp/trimui_inputd/turbo_r2"

void disableDpad(int is_muted) {
	if (is_muted && GetMuteDisablesDpad()) {
		mkdir(INPUTD_PATH, 0777);
		close(open(INPUTD_DPAD_PATH, O_RDWR | O_CREAT, 0777));
	} else {
		unlink(INPUTD_DPAD_PATH);
	}
}

void emulateJoystick(int is_muted) {
	if (is_muted && GetMuteEmulatesJoystick()) {
		mkdir(INPUTD_PATH, 0777);
		close(open(INPUTD_JOYSTICK_PATH, O_RDWR | O_CREAT, 0777));
	} else {
		unlink(INPUTD_JOYSTICK_PATH);
	}
}

void turboA(int is_muted) {
	if (is_muted && GetMuteTurboA()) {
		mkdir(INPUTD_PATH, 0777);
		close(open(INPUTD_TURBO_A_PATH, O_RDWR | O_CREAT, 0777));
	} else {
		unlink(INPUTD_TURBO_A_PATH);
	}
}
void turboB(int is_muted) {
	if (is_muted && GetMuteTurboB()) {
		mkdir(INPUTD_PATH, 0777);
		close(open(INPUTD_TURBO_B_PATH, O_RDWR | O_CREAT, 0777));
	} else {
		unlink(INPUTD_TURBO_B_PATH);
	}
}
void turboX(int is_muted) {
	if (is_muted && GetMuteTurboX()) {
		mkdir(INPUTD_PATH, 0777);
		close(open(INPUTD_TURBO_X_PATH, O_RDWR | O_CREAT, 0777));
	} else {
		unlink(INPUTD_TURBO_X_PATH);
	}
}
void turboY(int is_muted) {
	if (is_muted && GetMuteTurboY()) {
		mkdir(INPUTD_PATH, 0777);
		close(open(INPUTD_TURBO_Y_PATH, O_RDWR | O_CREAT, 0777));
	} else {
		unlink(INPUTD_TURBO_Y_PATH);
	}
}
void turboL1(int is_muted) {
	if (is_muted && GetMuteTurboL1()) {
		mkdir(INPUTD_PATH, 0777);
		close(open(INPUTD_TURBO_L1_PATH, O_RDWR | O_CREAT, 0777));
	} else {
		unlink(INPUTD_TURBO_L1_PATH);
	}
}
void turboL2(int is_muted) {
	if (is_muted && GetMuteTurboL2()) {
		mkdir(INPUTD_PATH, 0777);
		close(open(INPUTD_TURBO_L2_PATH, O_RDWR | O_CREAT, 0777));
	} else {
		unlink(INPUTD_TURBO_L2_PATH);
	}
}
void turboR1(int is_muted) {
	if (is_muted && GetMuteTurboR1()) {
		mkdir(INPUTD_PATH, 0777);
		close(open(INPUTD_TURBO_R1_PATH, O_RDWR | O_CREAT, 0777));
	} else {
		unlink(INPUTD_TURBO_R1_PATH);
	}
}
void turboR2(int is_muted) {
	if (is_muted && GetMuteTurboR2()) {
		mkdir(INPUTD_PATH, 0777);
		close(open(INPUTD_TURBO_R2_PATH, O_RDWR | O_CREAT, 0777));
	} else {
		unlink(INPUTD_TURBO_R2_PATH);
	}
}

static const int brightness_lut[] = {0, 2, 4, 8, 16, 32, 64, 96, 128, 192, 255};
int scaleBrightness(int value) {
	if (value <= 0) return 0;
	if (value >= 10) return 255;
	return brightness_lut[value];
}

static const int volume_lut[] = {0, 2, 4, 6, 8, 10, 12, 14, 17, 20, 24, 28, 33, 40, 50, 60, 70, 80, 90, 95, 100};
int scaleVolume(int value) {
	if (value <= 0) return 0;
	if (value >= 20) return 100;
	return volume_lut[value];
}

int scaleColortemp(int value) {
	if (value < 0) return -200;
	if (value > 40) return 200;
	return (value - 20) * 10;
}

int scaleContrast(int value) {
	if (value < -4) return 10;
	if (value > 5) return 100;
	return (value + 5) * 10;
}

int scaleSaturation(int value) {
	if (value < -5) return 0;
	if (value > 5) return 100;
	return (value + 5) * 10;
}

int scaleExposure(int value) {
	if (value < -4) return 10;
	if (value > 5) return 100;
	return (value + 5) * 10;
}

// Hardware setters
void SetRawBrightness(int val) {
	int max = screen_backlight_max > 0 ? screen_backlight_max : 255;
	int raw = (val * max) / 255;
	const char *path = screen_backlight_path[0] ? screen_backlight_path : "/sys/class/backlight/backlight/brightness";
	FILE *f = fopen(path, "w");
	if (f) {
		fprintf(f, "%d\n", raw);
		fclose(f);
	}
}

void SetRawColortemp(int val) {
	FILE *fd = fopen("/sys/class/disp/disp/attr/color_temperature", "w");
	if (fd) {
		fprintf(fd, "%i", val);
		fclose(fd);
	}
}

void SetRawContrast(int val) {
	FILE *fd = fopen("/sys/class/disp/disp/attr/enhance_contrast", "w");
	if (fd) {
		fprintf(fd, "%i", val);
		fclose(fd);
	}
}

void SetRawSaturation(int val) {
	FILE *fd = fopen("/sys/class/disp/disp/attr/enhance_saturation", "w");
	if (fd) {
		fprintf(fd, "%i", val);
		fclose(fd);
	}
}

void SetRawExposure(int val) {
	FILE *fd = fopen("/sys/class/disp/disp/attr/enhance_bright", "w");
	if (fd) {
		fprintf(fd, "%i", val);
		fclose(fd);
	}
}

void SetRawDisplayCal(int enabled, int red_gain, int green_gain, int blue_gain) {
	if (enabled) {
		DisplayCal_enableWithValues(red_gain, green_gain, blue_gain);
	} else {
		DisplayCal_disable();
	}
}

static int find_card_in_asound(const char *target) {
	FILE *fp = fopen("/proc/asound/cards", "r");
	if (!fp) return -1;
	char line[256];
	int card = -1;
	while (fgets(line, sizeof(line), fp)) {
		if (target && !strstr(line, target)) continue;
		if (!target && (strstr(line, "HDMI") || strstr(line, "hdmi"))) continue;
		if (sscanf(line, " %d ", &card) == 1) break;
	}
	fclose(fp);
	return card;
}

static int get_audio_card_num(void) {
	if (audio_card[0] && strcmp(audio_card, "default") != 0) {
		char *end;
		long num = strtol(audio_card, &end, 10);
		if (*end == '\0') return (int)num;
		int card = find_card_in_asound(audio_card);
		if (card >= 0) return card;
	}
	int card = find_card_in_asound(NULL);
	return (card >= 0) ? card : 0;
}

static int is_volume_ctl(struct mixer_ctl *ctl) {
	if (mixer_ctl_get_type(ctl) != MIXER_CTL_TYPE_INT) return 0;
	const char *name = mixer_ctl_get_name(ctl);
	if (!name || strstr(name, "Switch")) return 0;
	static const char *names[] = {"Playback", "Master", "Line Out", "PCM"};
	for (int i = 0; i < 4; i++) {
		if (strstr(name, names[i])) return 1;
	}
	return 0;
}

static struct mixer_ctl *find_mixer_ctl(struct mixer *mixer) {
	if (audio_mixer[0]) {
		struct mixer_ctl *ctl = mixer_get_ctl_by_name(mixer, audio_mixer);
		if (ctl) return ctl;
	}
	const unsigned int count = mixer_get_num_ctls(mixer);
	for (unsigned int i = 0; i < count; i++) {
		struct mixer_ctl *ctl = mixer_get_ctl(mixer, i);
		if (ctl && is_volume_ctl(ctl)) return ctl;
	}
	return NULL;
}

void SetRawVolume(int val) {
	if (settings && settings->mute && GetMutedVolume() != SETTINGS_DEFAULT_MUTE_NO_CHANGE)
		val = scaleVolume(GetMutedVolume());

	if (GetAudioSink() == AUDIO_SINK_BLUETOOTH) {
		char cmd[256];
		snprintf(cmd, sizeof(cmd), "amixer -D bluealsa sset 'Playback' %d%% >/dev/null 2>&1", val);
		system(cmd);
		return;
	}

	int card_num = get_audio_card_num();
	struct mixer *mixer = mixer_open(card_num);
	if (!mixer) mixer = mixer_open(0);
	if (!mixer) return;

	struct mixer_ctl *ctl = find_mixer_ctl(mixer);
	if (ctl && mixer_ctl_get_type(ctl) == MIXER_CTL_TYPE_INT) {
		int min = mixer_ctl_get_range_min(ctl);
		int max = mixer_ctl_get_range_max(ctl);
		int volume = min + (val * (max - min)) / 100;
		unsigned int count = mixer_ctl_get_num_values(ctl);
		for (unsigned int i = 0; i < count; i++) {
			mixer_ctl_set_value(ctl, i, volume);
		}
	}
	mixer_close(mixer);
}
