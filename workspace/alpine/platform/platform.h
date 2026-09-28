#ifndef PLATFORM_H
#define PLATFORM_H

///////////////////////////////

#include <SDL2/SDL.h>
#include <stdbool.h>

//////////////////////////////////////
// Display (Native panel & layout)

extern int screen_width;
extern int screen_height;
extern int screen_padding;
extern int screen_row_count;

#define FIXED_SCALE 2
#define FIXED_BPP 2
#define FIXED_DEPTH (FIXED_BPP * 8)
#define FIXED_WIDTH screen_width
#define FIXED_HEIGHT screen_height
#define FIXED_PITCH (screen_width * FIXED_BPP)
#define FIXED_SIZE (FIXED_PITCH * screen_height)

#define MAIN_ROW_COUNT (screen_row_count + (on_hdmi ? 2 : 0))
#define PADDING (on_hdmi ? 40 : screen_padding)

int PLAT_getScreenRotation(void);
extern void (*plat_custom_flip)(SDL_Surface *surface);

//////////////////////////////////////
// HDMI Output

extern int on_hdmi;
extern int gpu_hdmi_width;
extern int gpu_hdmi_height;
extern char gpu_hdmi_state_path[];
int MINIME_traitAvailable(const char *value);

#define HAS_HDMI MINIME_traitAvailable(gpu_hdmi_state_path)
#define HDMI_WIDTH gpu_hdmi_width
#define HDMI_HEIGHT gpu_hdmi_height
#define HDMI_PITCH (gpu_hdmi_width * FIXED_BPP)
#define HDMI_SIZE (HDMI_PITCH * gpu_hdmi_height)

//////////////////////////////////////
// Gamepad & Button Mapping

int PLAT_is6Button(void);
int PLAT_hasMenuButton(void);
int PLAT_hasL3(void);
int PLAT_hasR3(void);
int PLAT_hasLeftStick(void);
int PLAT_hasRightStick(void);

#define BTN_RESUME BTN_X
#define BTN_SLEEP BTN_POWER
#define BTN_WAKE BTN_POWER
#define BTN_MOD_VOLUME BTN_NONE
#define BTN_MOD_BRIGHTNESS BTN_MENU
#define BTN_MOD_COLORTEMP BTN_SELECT
#define BTN_MOD_PLUS BTN_PLUS
#define BTN_MOD_MINUS BTN_MINUS

#define BTN_FN1 BTN_L3
#define BTN_FN2 BTN_R3
#define BTN_FN1_NAME "L3"
#define BTN_FN2_NAME "R3"
#define BTN_FN3 BTN_NONE
#define BTN_FN3_NAME ""

//////////////////////////////////////
// Hardware & Peripheral Capabilities

bool PLAT_hasBluetooth(void);
bool PLAT_hasWifi(void);
const char *PLAT_getWifiInterface(void);
int PLAT_hasLid(void);

//////////////////////////////////////
// Platform Constants

#define SDCARD_PATH "/mnt/sdcard"
#define MUTE_VOLUME_RAW 0
#define SAMPLES 400 // fix for (most) fceumm underruns

///////////////////////////////

#endif
