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

#define BUTTON_UP BUTTON_NA
#define BUTTON_DOWN BUTTON_NA
#define BUTTON_LEFT BUTTON_NA
#define BUTTON_RIGHT BUTTON_NA

#define BUTTON_SELECT BUTTON_NA
#define BUTTON_START BUTTON_NA

#define BUTTON_A BUTTON_NA
#define BUTTON_B BUTTON_NA
#define BUTTON_X BUTTON_NA
#define BUTTON_Y BUTTON_NA

#define BUTTON_L1 BUTTON_NA
#define BUTTON_R1 BUTTON_NA
#define BUTTON_L2 BUTTON_NA
#define BUTTON_R2 BUTTON_NA
#define BUTTON_L3 BUTTON_NA
#define BUTTON_R3 BUTTON_NA
#define BUTTON_L4 BUTTON_NA
#define BUTTON_R4 BUTTON_NA

#define BUTTON_MENU BUTTON_NA
#define BUTTON_MENU_ALT BUTTON_NA
#define BUTTON_POWER BUTTON_NA
#define BUTTON_PLUS BUTTON_NA
#define BUTTON_MINUS BUTTON_NA

#define CODE_UP CODE_NA
#define CODE_DOWN CODE_NA
#define CODE_LEFT CODE_NA
#define CODE_RIGHT CODE_NA

#define CODE_SELECT CODE_NA
#define CODE_START CODE_NA

#define CODE_A CODE_NA
#define CODE_B CODE_NA
#define CODE_X CODE_NA
#define CODE_Y CODE_NA

#define CODE_L1 CODE_NA
#define CODE_R1 CODE_NA
#define CODE_L2 CODE_NA
#define CODE_R2 CODE_NA
#define CODE_L3 CODE_NA
#define CODE_R3 CODE_NA
#define CODE_L4 CODE_NA
#define CODE_R4 CODE_NA

#define CODE_MENU 1
#define CODE_POWER 116
#define CODE_PLUS CODE_NA
#define CODE_MINUS CODE_NA

#define JOY_UP JOY_NA
#define JOY_DOWN JOY_NA
#define JOY_LEFT JOY_NA
#define JOY_RIGHT JOY_NA

#define JOY_SELECT JOY_NA
#define JOY_START JOY_NA

#define JOY_A JOY_NA
#define JOY_B JOY_NA
#define JOY_X JOY_NA
#define JOY_Y JOY_NA

#define JOY_L1 JOY_NA
#define JOY_R1 JOY_NA
#define JOY_L2 JOY_NA
#define JOY_R2 JOY_NA
#define JOY_L3 JOY_NA
#define JOY_R3 JOY_NA
#define JOY_L4 JOY_NA
#define JOY_R4 JOY_NA

#define JOY_MENU JOY_NA
#define JOY_POWER JOY_NA
#define JOY_PLUS JOY_NA
#define JOY_MINUS JOY_NA

#define AXIS_LX 0
#define AXIS_LY 1
#define AXIS_RX 2
#define AXIS_RY 3
#define AXIS_L2 AXIS_NA
#define AXIS_R2 AXIS_NA

#define MAX_LIGHTS 0

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
