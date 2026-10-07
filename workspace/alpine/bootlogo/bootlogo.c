#include <dirent.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <msettings.h>

#include "api.h"
#include "defines.h"
#include "utils.h"

static bool quit = false;

static void sigHandler(int sig) {
	switch (sig) {
	case SIGINT:
	case SIGTERM:
		quit = true;
		break;
	default:
		break;
	}
}

static SDL_Surface *screen;
static SDL_Surface **images;
static char **image_paths;
static int selected = 0;
static int count = 0;
static char basepath[MAX_PATH];

static SDL_Surface *rotatePreviewCW(SDL_Surface *src) {
	SDL_Surface *conv = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_ARGB8888, 0);
	if (!conv) {
		return src;
	}

	SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormat(0, conv->h, conv->w, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!dst) {
		SDL_FreeSurface(conv);
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

static int append_image(const char *path) {
	SDL_Surface *img = IMG_Load(path);
	if (!img) {
		return 0;
	}

	if (PLAT_getScreenRotation() != 0) {
		img = rotatePreviewCW(img);
	}

	SDL_Surface **new_imgs = realloc(images, sizeof(SDL_Surface *) * (count + 1));
	char **new_paths = realloc(image_paths, sizeof(char *) * (count + 1));
	if (!new_imgs || !new_paths) {
		SDL_FreeSurface(img);
		if (new_imgs) {
			images = new_imgs;
		}
		if (new_paths) {
			image_paths = new_paths;
		}
		return 0;
	}

	images = new_imgs;
	image_paths = new_paths;
	images[count] = img;
	image_paths[count] = strdup(path);
	count++;
	return 1;
}

static int load_images(void) {
	snprintf(basepath, sizeof(basepath), "%s/Bootlogo.pak/", TOOLS_PATH);
	DIR *dir = opendir(basepath);
	if (!dir) {
		LOG_error("could not open directory: %s\n", basepath);
		if (CFG_getHaptics()) {
			VIB_triplePulse(5, 150, 200);
		}
		return 0;
	}

	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL) {
		if (!strstr(ent->d_name, ".png")) {
			continue;
		}

		char path[MAX_PATH];
		snprintf(path, sizeof(path), "%s%s", basepath, ent->d_name);
		append_image(path);
	}
	closedir(dir);
	return count;
}

static void unload_images(void) {
	for (int i = 0; i < count; i++) {
		SDL_FreeSurface(images[i]);
		free(image_paths[i]);
	}
	free(images);
	free(image_paths);
}

static void apply_selected_logo(void) {
	if (selected < 0 || selected >= count) {
		return;
	}

	char cmd[MAX_PATH + 64];
	snprintf(cmd, sizeof(cmd), "cp \"%s\" /mnt/sdcard/.minime/bootsplash.png && sync", image_paths[selected]);
	system(cmd);
	quit = true;
}

static int handle_nav_input(void) {
	if (count <= 0) {
		return 0;
	}
	if (PAD_justRepeated(BTN_LEFT)) {
		selected = (selected > 0) ? selected - 1 : count - 1;
		return 1;
	}
	if (PAD_justRepeated(BTN_RIGHT)) {
		selected = (selected + 1 < count) ? selected + 1 : 0;
		return 1;
	}
	return 0;
}

static void handle_action_input(void) {
	if (PAD_justPressed(BTN_A) && count > 0) {
		apply_selected_logo();
	} else if (PAD_justPressed(BTN_B)) {
		quit = true;
	}
}

static void render_image(SDL_Surface *image) {
	if (image->w > screen->w || image->h > screen->h) {
		int fit_w = screen->w;
		int fit_h = image->h * screen->w / image->w;
		if (fit_h > screen->h) {
			fit_h = screen->h;
			fit_w = image->w * screen->h / image->h;
		}
		SDL_Rect rect = {
			screen->w / 2 - fit_w / 2,
			screen->h / 2 - fit_h / 2,
			fit_w,
			fit_h,
		};
		SDL_BlitScaled(image, NULL, screen, &rect);
	} else {
		SDL_Rect rect = {
			screen->w / 2 - image->w / 2,
			screen->h / 2 - image->h / 2,
			image->w,
			image->h,
		};
		SDL_BlitSurface(image, NULL, screen, &rect);
	}
}

static void render_screen(void) {
	GFX_clear(screen);

	if (count > 0) {
		render_image(images[selected]);
	} else {
		char msg[MAX_PATH + 32];
		snprintf(msg, sizeof(msg), "No presets found in\n%s", basepath);
		GFX_blitMessage(font.small, msg, screen, NULL);
	}

	GFX_blitButtonGroup((char *[]){"L/R", "SCROLL", NULL}, 0, screen, 0);
	GFX_blitButtonGroup((char *[]){"A", "SET", "B", "BACK", NULL}, 1, screen, 1);

	GFX_flip(screen);
}

int main(int argc, char *argv[]) {
	InitSettings();
	PWR_setCPUSpeed(CPU_SPEED_AUTO);

	screen = GFX_init(MODE_MENU);
	PAD_init();
	PWR_init();

	signal(SIGINT, sigHandler);
	signal(SIGTERM, sigHandler);

	load_images();

	int dirty = 1;
	while (!quit) {
		GFX_startFrame();
		PAD_poll();

		if (!PAD_justPressed(BTN_MENU)) {
			if (handle_nav_input()) {
				dirty = 1;
			}
			handle_action_input();
		}

		PWR_update(&dirty, NULL, NULL, NULL);

		if (dirty) {
			render_screen();
			dirty = 0;
		} else {
			GFX_sync();
		}
	}

	unload_images();

	QuitSettings();
	PWR_quit();
	PAD_quit();
	GFX_quit();

	return EXIT_SUCCESS;
}
