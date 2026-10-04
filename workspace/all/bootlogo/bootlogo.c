#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <dirent.h>
#include <signal.h>
#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "utils.h"

static bool quit = false;

static void sigHandler(int sig)
{
    switch (sig)
    {
    case SIGINT:
    case SIGTERM:
        quit = true;
        break;
    default:
        break;
    }
}

static SDL_Surface *screen;
static char basepath[MAX_PATH];

SDL_Surface** images;
char **image_paths;
static int selected = 0;
static int count = 0;

static SDL_Surface *rotatePreviewCW(SDL_Surface *src)
{
    if (!src) return NULL;
    SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormat(0, src->h, src->w, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!dst) return src;
    SDL_Surface *conv = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_ARGB8888, 0);
    if (!conv) {
        SDL_FreeSurface(dst);
        return src;
    }
    uint32_t *src_pixels = (uint32_t *)conv->pixels;
    uint32_t *dst_pixels = (uint32_t *)dst->pixels;
    int src_stride = conv->pitch / 4;
    int dst_stride = dst->pitch / 4;
    for (int y = 0; y < conv->h; y++) {
        for (int x = 0; x < conv->w; x++) {
            dst_pixels[x * dst_stride + (conv->h - 1 - y)] = src_pixels[y * src_stride + x];
        }
    }
    SDL_FreeSurface(conv);
    SDL_FreeSurface(src);
    return dst;
}

int loadImages(void)
{
    // NEXTUI-ALPINE: check resolution folder first (e.g. 640x480), then fallback to logos
    snprintf(basepath, sizeof(basepath), "%s/Bootlogo.pak/%ix%i/", TOOLS_PATH, screen->w, screen->h);
    if (access(basepath, R_OK) != 0) {
        snprintf(basepath, sizeof(basepath), "%s/Bootlogo.pak/logos/", TOOLS_PATH);
    }
    if (access(basepath, R_OK) != 0) {
        char *device = getenv("DEVICE");
        if (exactMatch("brick", device) || exactMatch("brickpro", device)) {
            snprintf(basepath, sizeof(basepath), "%s/Bootlogo.pak/brick/", TOOLS_PATH);
        } else {
            snprintf(basepath, sizeof(basepath), "%s/Bootlogo.pak/smartpro/", TOOLS_PATH);
        }
    }

    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir(basepath)) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            if (strstr(ent->d_name, ".png") != NULL) {
                char path[MAX_PATH];
                snprintf(path, sizeof(path), "%s%s", basepath, ent->d_name);
                SDL_Surface *img = IMG_Load(path);
                if (img) {
                    if (PLAT_getScreenRotation() != 0)
                        img = rotatePreviewCW(img);
                    count++;
                    images = realloc(images, sizeof(SDL_Surface *) * count);
                    images[count - 1] = img;
                    image_paths = realloc(image_paths, sizeof(char *) * count);
                    image_paths[count - 1] = strdup(path);
                }
            }
        }
        closedir(dir);
    } else {
        LOG_error("could not open directory: %s\n", basepath);
        if (CFG_getHaptics()) {
            VIB_triplePulse(5, 150, 200);
        }
        return 0;
    }
    return count;
}

void unloadImages(void)
{
    for (int i = 0; i < count; i++) {
        SDL_FreeSurface(images[i]);
    }
    free(images);
}

int main(int argc, char *argv[])
{
    InitSettings();

    PWR_setCPUSpeed(CPU_SPEED_AUTO);

    screen = GFX_init(MODE_MENU);
    PAD_init();
    PWR_init();

    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);

    loadImages();

    int dirty = 1;
    int was_online = PWR_isOnline();
    int had_bt = PLAT_btIsConnected();
    while (!quit) {
        GFX_startFrame();
        PAD_poll();

        if (PAD_justPressed(BTN_MENU)) {
            // ignore menu combos
        } else {
            if (PAD_justRepeated(BTN_LEFT) && count > 0) {
                selected -= 1;
                if (selected < 0)
                    selected = count - 1;
                dirty = 1;
            } else if (PAD_justRepeated(BTN_RIGHT) && count > 0) {
                selected += 1;
                if (selected >= count)
                    selected = 0;
                dirty = 1;
            } else if (PAD_justPressed(BTN_A) && count > 0) {
                // NEXTUI-ALPINE: write bootsplash directly to /mnt/sdcard/.minime/bootsplash.png on Alpine
                if (exactMatch("alpine", PLATFORM)) {
                    char *logo_path = image_paths[selected];
                    char cmd[512];
                    snprintf(cmd, sizeof(cmd), "cp \"%s\" /mnt/sdcard/.minime/bootsplash.png && sync", logo_path);
                    system(cmd);
                    quit = 1;
                } else {
                    char *boot_path = "/mnt/boot/";
                    char *logo_path = image_paths[selected];
                    char cmd[512];
                    snprintf(cmd, sizeof(cmd), "mkdir -p %s && mount -t vfat /dev/mmcblk0p1 %s && cp \"%s\" %s/bootlogo.bmp && sync && umount %s && reboot", boot_path, boot_path, logo_path, boot_path, boot_path);
                    system(cmd);
                }
            } else if (PAD_justPressed(BTN_B)) {
                quit = 1;
            }
        }

        PWR_update(&dirty, NULL, NULL, NULL);

        int is_online = PWR_isOnline();
        if (was_online != is_online)
            dirty = 1;
        was_online = is_online;

        int has_bt = PLAT_btIsConnected();
        if (had_bt != has_bt)
            dirty = 1;
        had_bt = has_bt;

        if (dirty) {
            GFX_clear(screen);

            if (count > 0) {
                SDL_Surface *image = images[selected];
                if (image->w > screen->w || image->h > screen->h) {
                    int fit_w = screen->w;
                    int fit_h = image->h * screen->w / image->w;
                    if (fit_h > screen->h) {
                        fit_h = screen->h;
                        fit_w = image->w * screen->h / image->h;
                    }
                    SDL_Rect image_rect = {
                        screen->w / 2 - fit_w / 2,
                        screen->h / 2 - fit_h / 2,
                        fit_w,
                        fit_h
                    };
                    SDL_BlitScaled(image, NULL, screen, &image_rect);
                } else {
                    SDL_Rect image_rect = {
                        screen->w / 2 - image->w / 2,
                        screen->h / 2 - image->h / 2,
                        image->w,
                        image->h
                    };
                    SDL_BlitSurface(image, NULL, screen, &image_rect);
                }
            } else {
                char msg[MAX_PATH + 32];
                snprintf(msg, sizeof(msg), "No presets found in\n%s", basepath);
                GFX_blitMessage(font.small, msg, screen, NULL);
            }

            GFX_blitButtonGroup((char *[]){"L/R", "SCROLL", NULL}, 0, screen, 0);
            GFX_blitButtonGroup((char *[]){"A", "SET", "B", "BACK", NULL}, 1, screen, 1);

            GFX_flip(screen);
            dirty = 0;
        } else {
            GFX_sync();
        }
    }

    unloadImages();

    QuitSettings();
    PWR_quit();
    PAD_quit();
    GFX_quit();

    return EXIT_SUCCESS;
}