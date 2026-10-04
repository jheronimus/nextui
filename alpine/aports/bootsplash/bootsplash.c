/*
 * bootsplash: Direct DRM/KMS and framebuffer bootsplash daemon for NextUI Alpine
 *
 * Displays /mnt/sdcard/.minime/bootsplash.png centered on screen.
 *
 * Direct DRM/KMS mode:
 * - Uses primary plane and DRM dumb buffers with hardware page flip.
 * - Handles display rotation based on traits / hardware orientation.
 *
 * Fallback mode:
 * - Falls back to /dev/fb0 if DRM is unavailable.
 *
 * Listens to evdev volume keys to toggle between bootsplash (KD_GRAPHICS)
 * and console log (KD_TEXT). Watches OpenRC UI lifecycle to perform clean handoff.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>
#include <time.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <sys/klog.h>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#ifndef DRM_MODE_CONNECTED
#define DRM_MODE_CONNECTED 1
#endif
#ifndef DRM_PLANE_TYPE_PRIMARY
#define DRM_PLANE_TYPE_PRIMARY 1
#endif

#define MAX_INPUTS 16
#define TIMEOUT_SECS 60
#define BOOTSPLASH_PATH "/mnt/sdcard/.minime/bootsplash.png"
#define TRAITS_PATH "/mnt/sdcard/.minime/traits"

static volatile bool g_running = true;

static void handle_signal(int sig)
{
	(void)sig;
	g_running = false;
}

struct render_surface {
	uint8_t *mem;
	uint32_t width;
	uint32_t height;
	uint32_t log_width;
	uint32_t log_height;
	uint32_t pitch;
	uint32_t bpp;
	int rotation;
};

struct drm_state {
	int fd;
	uint32_t conn_id;
	uint32_t crtc_id;
	struct drm_mode_modeinfo mode;
	struct {
		uint32_t fb_id;
		uint32_t handle;
		uint32_t pitch;
		uint32_t size;
		uint8_t *map;
	} bufs[2];
	int cur_buf;
	uint32_t width;
	uint32_t height;
};

struct fb_state {
	int fd;
	uint8_t *mem;
	size_t mem_size;
	uint32_t width;
	uint32_t height;
	uint32_t pitch;
	uint32_t bpp;
	struct fb_var_screeninfo orig_var;
};

static inline uint32_t pack_rgb32(uint8_t r, uint8_t g, uint8_t b)
{
	return (0xFFU << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static inline uint32_t pack_rgb16(uint8_t r, uint8_t g, uint8_t b)
{
	return (uint32_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static inline void put_pixel(const struct render_surface *surf, uint32_t x, uint32_t y, uint32_t color)
{
	if (x >= surf->log_width || y >= surf->log_height)
		return;

	uint32_t px = x;
	uint32_t py = y;

	if (surf->rotation == 90) {
		px = (surf->log_height - 1) - y;
		py = x;
	} else if (surf->rotation == 180) {
		px = (surf->log_width - 1) - x;
		py = (surf->log_height - 1) - y;
	} else if (surf->rotation == 270) {
		px = y;
		py = (surf->log_width - 1) - x;
	}

	if (px >= surf->width || py >= surf->height)
		return;

	if (surf->bpp == 16) {
		uint16_t *p = (uint16_t *)(surf->mem + (py * surf->pitch) + (px * 2));
		*p = (uint16_t)color;
	} else {
		uint32_t *p = (uint32_t *)(surf->mem + (py * surf->pitch) + (px * 4));
		*p = color;
	}
}

static void clear_surface(const struct render_surface *surf)
{
	if (!surf->mem)
		return;
	memset(surf->mem, 0, (size_t)surf->pitch * surf->height);
}

static bool load_and_draw_splash(const struct render_surface *surf)
{
	int img_w = 0, img_h = 0, channels = 0;
	stbi_uc *pixels = stbi_load(BOOTSPLASH_PATH, &img_w, &img_h, &channels, 4);
	if (!pixels)
		return false;

	clear_surface(surf);

	int dst_x = ((int)surf->log_width - img_w) / 2;
	int dst_y = ((int)surf->log_height - img_h) / 2;

	for (int y = 0; y < img_h; y++) {
		int py = dst_y + y;
		if (py < 0 || py >= (int)surf->log_height)
			continue;

		for (int x = 0; x < img_w; x++) {
			int px = dst_x + x;
			if (px < 0 || px >= (int)surf->log_width)
				continue;

			uint8_t *src = &pixels[(y * img_w + x) * 4];
			uint8_t a = src[3];
			if (a == 0)
				continue;

			uint8_t r = (uint8_t)(((uint16_t)src[0] * a) / 255);
			uint8_t g = (uint8_t)(((uint16_t)src[1] * a) / 255);
			uint8_t b = (uint8_t)(((uint16_t)src[2] * a) / 255);
			uint32_t col = (surf->bpp == 16) ? pack_rgb16(r, g, b) : pack_rgb32(r, g, b);
			put_pixel(surf, (uint32_t)px, (uint32_t)py, col);
		}
	}

	stbi_image_free(pixels);
	return true;
}

/* ──────────────── DRM/KMS Subsystem ──────────────── */

static int is_internal_conn(uint32_t type)
{
	return (type == 14 || type == 15 || type == 16 || type == 17);
}

static int drm_init(struct drm_state *drm)
{
	memset(drm, 0, sizeof(*drm));
	drm->fd = -1;

	for (int i = 0; i < 4; i++) {
		char path[32];
		snprintf(path, sizeof(path), "/dev/dri/card%d", i);
		int fd = open(path, O_RDWR | O_CLOEXEC);
		if (fd >= 0) {
			drm->fd = fd;
			break;
		}
	}
	if (drm->fd < 0)
		return -1;

	ioctl(drm->fd, DRM_IOCTL_SET_MASTER, 0);

	struct drm_mode_card_res res = {0};
	if (ioctl(drm->fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0 || res.count_connectors == 0) {
		close(drm->fd);
		return -1;
	}

	uint32_t *conns = calloc(res.count_connectors, sizeof(uint32_t));
	uint32_t *crtcs = calloc(res.count_crtcs, sizeof(uint32_t));
	uint32_t *encs  = calloc(res.count_encoders, sizeof(uint32_t));
	uint32_t *fbs   = calloc(res.count_fbs, sizeof(uint32_t));

	res.connector_id_ptr = (uint64_t)(uintptr_t)conns;
	res.crtc_id_ptr = (uint64_t)(uintptr_t)crtcs;
	res.encoder_id_ptr = (uint64_t)(uintptr_t)encs;
	res.fb_id_ptr = (uint64_t)(uintptr_t)fbs;

	if (ioctl(drm->fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) {
		free(conns); free(crtcs); free(encs); free(fbs);
		close(drm->fd);
		return -1;
	}

	int found = 0;
	for (uint32_t i = 0; i < res.count_connectors; i++) {
		struct drm_mode_get_connector conn = {0};
		conn.connector_id = conns[i];
		if (ioctl(drm->fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0)
			continue;
		if (conn.connection != DRM_MODE_CONNECTED || conn.count_modes == 0)
			continue;

		struct drm_mode_modeinfo *modes = calloc(conn.count_modes, sizeof(struct drm_mode_modeinfo));
		uint32_t *enc_ids = calloc(conn.count_encoders, sizeof(uint32_t));
		conn.modes_ptr = (uint64_t)(uintptr_t)modes;
		conn.encoders_ptr = (uint64_t)(uintptr_t)enc_ids;
		if (ioctl(drm->fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) == 0) {
			drm->conn_id = conns[i];
			drm->mode = modes[0];

			if (conn.encoder_id != 0) {
				struct drm_mode_get_encoder enc = {0};
				enc.encoder_id = conn.encoder_id;
				if (ioctl(drm->fd, DRM_IOCTL_MODE_GETENCODER, &enc) == 0)
					drm->crtc_id = enc.crtc_id;
			}
			if (drm->crtc_id == 0 && res.count_crtcs > 0)
				drm->crtc_id = crtcs[0];

			found = 1;
			free(modes);
			free(enc_ids);
			if (is_internal_conn(conn.connector_type))
				break;
		} else {
			free(modes);
			free(enc_ids);
		}
	}
	free(crtcs); free(conns); free(encs); free(fbs);

	if (!found || drm->crtc_id == 0) {
		close(drm->fd);
		return -1;
	}

	drm->width = drm->mode.hdisplay;
	drm->height = drm->mode.vdisplay;

	for (int b = 0; b < 2; b++) {
		struct drm_mode_create_dumb cd = {0};
		cd.width = drm->width;
		cd.height = drm->height;
		cd.bpp = 32;
		if (ioctl(drm->fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd) < 0) {
			close(drm->fd);
			return -1;
		}
		drm->bufs[b].handle = cd.handle;
		drm->bufs[b].pitch = cd.pitch;
		drm->bufs[b].size = cd.size;

		struct drm_mode_fb_cmd fb_cmd = {0};
		fb_cmd.width = drm->width;
		fb_cmd.height = drm->height;
		fb_cmd.pitch = cd.pitch;
		fb_cmd.bpp = 32;
		fb_cmd.depth = 24;
		fb_cmd.handle = cd.handle;
		if (ioctl(drm->fd, DRM_IOCTL_MODE_ADDFB, &fb_cmd) < 0) {
			close(drm->fd);
			return -1;
		}
		drm->bufs[b].fb_id = fb_cmd.fb_id;

		struct drm_mode_map_dumb md = {0};
		md.handle = cd.handle;
		if (ioctl(drm->fd, DRM_IOCTL_MODE_MAP_DUMB, &md) < 0) {
			close(drm->fd);
			return -1;
		}

		drm->bufs[b].map = mmap(NULL, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, drm->fd, md.offset);
		if (drm->bufs[b].map == MAP_FAILED) {
			close(drm->fd);
			return -1;
		}
		memset(drm->bufs[b].map, 0, cd.size);
	}

	uint32_t conn_ids[1] = { drm->conn_id };
	struct drm_mode_crtc crtc = {0};
	crtc.crtc_id = drm->crtc_id;
	crtc.fb_id = drm->bufs[0].fb_id;
	crtc.set_connectors_ptr = (uint64_t)(uintptr_t)conn_ids;
	crtc.count_connectors = 1;
	crtc.mode = drm->mode;
	crtc.mode_valid = 1;
	ioctl(drm->fd, DRM_IOCTL_MODE_SETCRTC, &crtc);

	return 0;
}

static void drm_flip(struct drm_state *drm)
{
	struct drm_mode_crtc_page_flip pf = {0};
	pf.crtc_id = drm->crtc_id;
	pf.fb_id = drm->bufs[drm->cur_buf].fb_id;
	pf.flags = 0;
	if (ioctl(drm->fd, DRM_IOCTL_MODE_PAGE_FLIP, &pf) < 0) {
		uint32_t conn_ids[1] = { drm->conn_id };
		struct drm_mode_crtc crtc = {0};
		crtc.crtc_id = drm->crtc_id;
		crtc.fb_id = drm->bufs[drm->cur_buf].fb_id;
		crtc.set_connectors_ptr = (uint64_t)(uintptr_t)conn_ids;
		crtc.count_connectors = 1;
		crtc.mode = drm->mode;
		crtc.mode_valid = 1;
		ioctl(drm->fd, DRM_IOCTL_MODE_SETCRTC, &crtc);
	}
	drm->cur_buf = 1 - drm->cur_buf;
}

static void drm_cleanup(struct drm_state *drm, bool persist)
{
	for (int b = 0; b < 2; b++) {
		if (drm->bufs[b].map && drm->bufs[b].map != MAP_FAILED) {
			if (!persist)
				memset(drm->bufs[b].map, 0, drm->bufs[b].size);
			munmap(drm->bufs[b].map, drm->bufs[b].size);
		}
		if (!persist) {
			if (drm->bufs[b].fb_id)
				ioctl(drm->fd, DRM_IOCTL_MODE_RMFB, drm->bufs[b].fb_id);
			if (drm->bufs[b].handle) {
				struct drm_mode_destroy_dumb dd = { .handle = drm->bufs[b].handle };
				ioctl(drm->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dd);
			}
		}
	}
	if (drm->fd >= 0)
		close(drm->fd);
}

/* ──────────────── Framebuffer Fallback ──────────────── */

static int fb_init(struct fb_state *fb)
{
	memset(fb, 0, sizeof(*fb));
	fb->fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
	if (fb->fd < 0)
		return -1;

	struct fb_fix_screeninfo finfo;
	if (ioctl(fb->fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
		close(fb->fd);
		return -1;
	}

	struct fb_var_screeninfo vinfo;
	if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
		close(fb->fd);
		return -1;
	}
	fb->orig_var = vinfo;

	fb->width = vinfo.xres;
	fb->height = vinfo.yres;
	fb->bpp = vinfo.bits_per_pixel;
	fb->pitch = finfo.line_length ? finfo.line_length : fb->width * (fb->bpp / 8);
	fb->mem_size = (size_t)fb->pitch * fb->height;

	fb->mem = mmap(NULL, fb->mem_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0);
	if (fb->mem == MAP_FAILED) {
		close(fb->fd);
		return -1;
	}

	return 0;
}

/* ──────────────── Input & Lifecycle ──────────────── */

static int open_tty(void)
{
	static const char *tty_paths[] = { "/dev/tty0", "/dev/tty1", "/dev/console", NULL };
	for (int i = 0; tty_paths[i]; i++) {
		int fd = open(tty_paths[i], O_RDWR | O_CLOEXEC);
		if (fd >= 0)
			return fd;
	}
	return -1;
}

static int scan_input_devices(int *fds, int max_fds)
{
	int count = 0;
	DIR *dir = opendir("/dev/input");
	if (!dir)
		return 0;

	struct dirent *ent;
	while ((ent = readdir(dir)) && count < max_fds) {
		if (strncmp(ent->d_name, "event", 5) != 0)
			continue;
		char path[64];
		snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);
		int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd >= 0)
			fds[count++] = fd;
	}
	closedir(dir);
	return count;
}

static void close_input_devices(int *fds, int count)
{
	for (int i = 0; i < count; i++) {
		if (fds[i] >= 0) {
			close(fds[i]);
			fds[i] = -1;
		}
	}
}

static void read_traits(int *key_up, int *key_down, int *screen_rot)
{
	*key_up = KEY_VOLUMEUP;
	*key_down = KEY_VOLUMEDOWN;
	*screen_rot = 0;

	FILE *f = fopen(TRAITS_PATH, "r");
	if (!f)
		return;

	char line[128];
	while (fgets(line, sizeof(line), f)) {
		int val = 0;
		if (sscanf(line, "key_vol_up=%d", &val) == 1 && val > 0)
			*key_up = val;
		else if (sscanf(line, "key_vol_down=%d", &val) == 1 && val > 0)
			*key_down = val;
		else if (sscanf(line, "screen_rotation=%d", &val) == 1 && val >= 0)
			*screen_rot = val;
	}
	fclose(f);
}

/* ──────────────── Main ──────────────── */

int main(int argc, char **argv)
{
	bool persist = true;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--no-persist") == 0 || strcmp(argv[i], "--clear") == 0)
			persist = false;
	}

	struct sigaction sa = {
		.sa_handler = handle_signal,
		.sa_flags = 0,
	};
	sigemptyset(&sa.sa_mask);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);

	int key_vol_up = KEY_VOLUMEUP;
	int key_vol_down = KEY_VOLUMEDOWN;
	int screen_rot = 0;
	read_traits(&key_vol_up, &key_vol_down, &screen_rot);

	int tty_fd = open_tty();
	if (tty_fd >= 0)
		ioctl(tty_fd, KDSETMODE, KD_GRAPHICS);

	struct drm_state drm;
	struct fb_state fb;
	bool use_drm = (drm_init(&drm) == 0);
	if (!use_drm && fb_init(&fb) != 0) {
		if (tty_fd >= 0)
			close(tty_fd);
		return 1;
	}

	struct render_surface surf = {0};
	if (use_drm) {
		surf.mem = drm.bufs[0].map;
		surf.width = drm.width;
		surf.height = drm.height;
		surf.pitch = drm.bufs[0].pitch;
		surf.bpp = 32;
	} else {
		surf.mem = fb.mem;
		surf.width = fb.width;
		surf.height = fb.height;
		surf.pitch = fb.pitch;
		surf.bpp = fb.bpp;
	}

	surf.rotation = screen_rot;
	if (surf.rotation == 90 || surf.rotation == 270) {
		surf.log_width = surf.height;
		surf.log_height = surf.width;
	} else {
		surf.log_width = surf.width;
		surf.log_height = surf.height;
	}

	load_and_draw_splash(&surf);
	if (use_drm) {
		surf.mem = drm.bufs[1].map;
		load_and_draw_splash(&surf);
		surf.mem = drm.bufs[0].map;
		drm_flip(&drm);
	}

	int input_fds[MAX_INPUTS];
	int input_count = scan_input_devices(input_fds, MAX_INPUTS);
	bool in_graphics_mode = true;
	time_t start_time = time(NULL);

	while (g_running) {
		if (access("/tmp/nextui_exec", F_OK) == 0)
			break;
		if (access("/run/openrc/failed/ui", F_OK) == 0) {
			if (tty_fd >= 0)
				ioctl(tty_fd, KDSETMODE, KD_TEXT);
			klogctl(8, NULL, 7);
			break;
		}
		if (time(NULL) - start_time > TIMEOUT_SECS)
			break;

		struct pollfd pfds[MAX_INPUTS];
		for (int i = 0; i < input_count; i++) {
			pfds[i].fd = input_fds[i];
			pfds[i].events = POLLIN;
			pfds[i].revents = 0;
		}

		int ret = poll(pfds, input_count, 100);
		if (ret > 0) {
			for (int i = 0; i < input_count; i++) {
				if (!(pfds[i].revents & POLLIN))
					continue;

				struct input_event ev;
				while (read(pfds[i].fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
					if (ev.type != EV_KEY || ev.value != 1)
						continue;

					if (ev.code == key_vol_up && in_graphics_mode) {
						if (tty_fd >= 0)
							ioctl(tty_fd, KDSETMODE, KD_TEXT);
						klogctl(8, NULL, 7);
						in_graphics_mode = false;
					} else if (ev.code == key_vol_down && !in_graphics_mode) {
						if (tty_fd >= 0)
							ioctl(tty_fd, KDSETMODE, KD_GRAPHICS);
						klogctl(8, NULL, 1);
						in_graphics_mode = true;
						surf.mem = use_drm ? drm.bufs[drm.cur_buf].map : fb.mem;
						load_and_draw_splash(&surf);
						if (use_drm)
							drm_flip(&drm);
					}
				}
			}
		}
	}

	close_input_devices(input_fds, input_count);

	if (use_drm) {
		drm_cleanup(&drm, persist);
	} else {
		if (!persist)
			clear_surface(&surf);
		munmap(fb.mem, fb.mem_size);
		close(fb.fd);
	}

	if (tty_fd >= 0)
		close(tty_fd);

	return 0;
}
