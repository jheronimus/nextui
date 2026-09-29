/*
 * bootsplash: lightweight DRM/KMS and framebuffer bootsplash for Minime
 *
 * Renders the lowercase 'minimē' ASCII wordmark and a looping left-to-right
 * gradient progress bar.
 *
 * Direct DRM/KMS mode:
 * - Applies primary plane rotation at frame 0 via DRM atomic commit.
 * - Allocates DRM dumb buffers and double-buffers via page flips with hardware vsync.
 * - Draws directly at 0° in native landscape geometry (640x480).
 *
 * Fallback mode:
 * - Falls back to /dev/fb0 if DRM is unavailable.
 *
 * Listens to evdev volume keys to toggle between bootsplash (KD_GRAPHICS)
 * and console log (KD_TEXT). Watches OpenRC UI lifecycle to perform clean handoff.
 * Clears the screen to black on exit.
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

#ifndef DRM_MODE_CONNECTED
#define DRM_MODE_CONNECTED 1
#endif
#ifndef DRM_PLANE_TYPE_PRIMARY
#define DRM_PLANE_TYPE_PRIMARY 1
#endif

#define MAX_LOGO_ROWS 4
#define MAX_LOGO_COLS 40
#define MAX_INPUTS 16
#define TIMEOUT_SECS 60

static int g_logo_rows = 4;
static int g_logo_cols = 36;
static int g_is_blast16 = 0;

static const char *nextui_logo_lines[MAX_LOGO_ROWS] = {
	"█   █  █▀▀▀▀  █   █  ▀▀█▀▀  █   █  █",
	"██  █  █▀▀     ▀▄▀     █    █   █  █",
	"█ █ █  █▄▄▄▄   ▄▀▄     █    █▄▄▄█  █",
	"█  ██         █   █    █            "
};

static const char *blast16_logo_lines[MAX_LOGO_ROWS] = {
	"█▀▀█ █    █▀▀█ █▀▀▀ ▀█▀   ▄█  █▀▀█",
	"█▀▀▄ █    █▀▀█ ▀▀▀█  █     █  █▄▄▄",
	"█  █ █    █  █    █  █     █  █  █",
	"█▄▄█ █▄▄▄ █  █ ▄▄▄█  ▀    ▄█▄ █▄▄█"
};

enum glyph_type {
	GLYPH_SPACE = 0,
	GLYPH_FULL,
	GLYPH_UPPER,
	GLYPH_LOWER
};

static enum glyph_type logo_grid[MAX_LOGO_ROWS][MAX_LOGO_COLS];


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
	uint32_t plane_id;
	struct drm_mode_modeinfo mode;
	struct {
		uint32_t handle;
		uint32_t fb_id;
		uint8_t *map;
		size_t size;
		uint32_t pitch;
	} bufs[2];
	int cur_buf;
	uint32_t width;
	uint32_t height;
};

struct fb_ctx {
	int fd;
	uint8_t *mem;
	size_t mem_size;
	uint32_t xres;
	uint32_t yres;
	uint32_t bpp;
	uint32_t line_length;
};

static volatile sig_atomic_t g_running = 1;

static void sig_handler(int sig)
{
	(void)sig;
	g_running = 0;
}

static int check_ui_blast16(void)
{
	const char *ui_files[] = {
		"/mnt/sdcard/.minime/ui.env",
		"/usr/share/minime/ui.env",
		"/etc/minime/ui.env",
		NULL
	};
	for (int i = 0; ui_files[i]; i++) {
		FILE *f = fopen(ui_files[i], "r");
		if (!f)
			continue;
		char line[128];
		while (fgets(line, sizeof(line), f)) {
			if (strstr(line, "Blast16") || strstr(line, "blast16")) {
				fclose(f);
				return 1;
			}
		}
		fclose(f);
	}
	return 0;
}

static void parse_logo_grid(void)
{
	g_is_blast16 = check_ui_blast16();
	const char * const *lines = g_is_blast16 ? blast16_logo_lines : nextui_logo_lines;
	g_logo_rows = 4;
	g_logo_cols = g_is_blast16 ? 34 : 36;

	memset(logo_grid, 0, sizeof(logo_grid));
	for (int r = 0; r < g_logo_rows; r++) {
		const unsigned char *p = (const unsigned char *)lines[r];
		int c = 0;
		while (*p && c < g_logo_cols) {
			if (*p == ' ') {
				logo_grid[r][c++] = GLYPH_SPACE;
				p++;
			} else if (p[0] == 0xe2 && p[1] == 0x96) {
				if (p[2] == 0x88) {
					logo_grid[r][c++] = GLYPH_FULL;
				} else if (p[2] == 0x80) {
					logo_grid[r][c++] = GLYPH_UPPER;
				} else if (p[2] == 0x84) {
					logo_grid[r][c++] = GLYPH_LOWER;
				} else {
					logo_grid[r][c++] = GLYPH_SPACE;
				}
				p += 3;
			} else {
				p++;
			}
		}
	}
}


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

static void draw_rect(const struct render_surface *surf, uint32_t x, uint32_t y,
		      uint32_t w, uint32_t h, uint32_t color)
{
	for (uint32_t row = 0; row < h; row++) {
		for (uint32_t col = 0; col < w; col++) {
			put_pixel(surf, x + col, y + row, color);
		}
	}
}

static void draw_glyph(const struct render_surface *surf, uint32_t x, uint32_t y,
		       uint32_t w, uint32_t h, enum glyph_type type, uint32_t color)
{
	switch (type) {
	case GLYPH_FULL:
		draw_rect(surf, x, y, w, h, color);
		break;
	case GLYPH_UPPER:
		draw_rect(surf, x, y, w, (h + 1) / 2, color);
		break;
	case GLYPH_LOWER:
		draw_rect(surf, x, y + h / 2, w, h - h / 2, color);
		break;
	case GLYPH_SPACE:
	default:
		break;
	}
}

static void draw_wordmark(const struct render_surface *surf, uint32_t start_x, uint32_t start_y,
			  uint32_t cell_w, uint32_t cell_h, uint32_t color)
{
	for (int r = 0; r < g_logo_rows; r++) {
		for (int c = 0; c < g_logo_cols; c++) {
			enum glyph_type g = logo_grid[r][c];
			if (g != GLYPH_SPACE) {
				uint32_t gx = start_x + (uint32_t)c * cell_w;
				uint32_t gy = start_y + (uint32_t)r * cell_h;
				draw_glyph(surf, gx, gy, cell_w, cell_h, g, color);
			}
		}
	}
}

static void draw_gradient_bar(const struct render_surface *surf, uint32_t track_x, uint32_t track_y,
			      uint32_t track_w, uint32_t bar_h, int32_t beam_pos,
			      uint32_t beam_w, uint32_t *scratch_buf)
{
	if (track_w > 4096)
		track_w = 4096;

	for (uint32_t x = 0; x < track_w; x++) {
		int32_t d = abs((int32_t)x - beam_pos);
		int32_t d_wrap = (int32_t)track_w - d;
		if (d_wrap < d)
			d = d_wrap;

		if ((uint32_t)d < beam_w) {
			float t = 1.0f - ((float)d / (float)beam_w);
			float intensity = t * t * (3.0f - 2.0f * t);

			uint8_t r, g, b;
			if (g_is_blast16) {
				/* Sega blue beam */
				r = (uint8_t)(intensity * 20.0f);
				g = (uint8_t)(90.0f * (1.0f - intensity) + intensity * 200.0f);
				b = (uint8_t)(160.0f * (1.0f - intensity) + intensity * 255.0f);
			} else {
				r = (uint8_t)(intensity * 180.0f);
				g = (uint8_t)(30.0f * (1.0f - intensity) + intensity * 240.0f);
				b = (uint8_t)(80.0f * (1.0f - intensity) + intensity * 255.0f);
			}
			scratch_buf[x] = (surf->bpp == 16) ? pack_rgb16(r, g, b) : pack_rgb32(r, g, b);
		} else {
			scratch_buf[x] = 0;
		}
	}

	for (uint32_t y = 0; y < bar_h; y++) {
		for (uint32_t x = 0; x < track_w; x++) {
			put_pixel(surf, track_x + x, track_y + y, scratch_buf[x]);
		}
	}
}


/* ──────────────── DRM Helpers ──────────────── */

static uint32_t rot_for_angle(int angle)
{
	switch (angle) {
	case 90:  return DRM_MODE_ROTATE_90;
	case 180: return DRM_MODE_ROTATE_180;
	case 270: return DRM_MODE_ROTATE_270;
	default:  return DRM_MODE_ROTATE_0;
	}
}

static int is_internal_conn(uint32_t type)
{
	switch (type) {
	case DRM_MODE_CONNECTOR_DSI:
	case DRM_MODE_CONNECTOR_eDP:
	case DRM_MODE_CONNECTOR_LVDS:
	case DRM_MODE_CONNECTOR_DPI:
		return 1;
	default:
		return 0;
	}
}

static int drm_find_prop(int fd, uint32_t obj_id, uint32_t obj_type,
			 const char *wanted, uint32_t *prop_id_out)
{
	struct drm_mode_obj_get_properties req = {0};
	req.obj_id = obj_id;
	req.obj_type = obj_type;
	if (ioctl(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &req) < 0 || req.count_props == 0)
		return -1;

	uint32_t *ids = calloc(req.count_props, sizeof(uint32_t));
	uint64_t *vals = calloc(req.count_props, sizeof(uint64_t));
	if (!ids || !vals) {
		free(ids); free(vals);
		return -1;
	}

	req.props_ptr = (uint64_t)(uintptr_t)ids;
	req.prop_values_ptr = (uint64_t)(uintptr_t)vals;
	if (ioctl(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &req) < 0) {
		free(ids); free(vals);
		return -1;
	}

	int found = -1;
	for (uint32_t i = 0; i < req.count_props; i++) {
		struct drm_mode_get_property prop = {0};
		prop.prop_id = ids[i];
		if (ioctl(fd, DRM_IOCTL_MODE_GETPROPERTY, &prop) == 0) {
			if (strcmp(prop.name, wanted) == 0) {
				*prop_id_out = ids[i];
				found = 0;
				break;
			}
		}
	}
	free(ids);
	free(vals);
	return found;
}

static int drm_set_plane_rotation(int fd, uint32_t plane_id, int angle)
{
	uint32_t rot_prop = 0;
	if (drm_find_prop(fd, plane_id, DRM_MODE_OBJECT_PLANE, "rotation", &rot_prop) < 0)
		return -1;

	uint64_t want = rot_for_angle(angle);
	struct drm_mode_atomic atom = {0};
	uint32_t objs[1] = { plane_id };
	uint32_t props[1] = { rot_prop };
	uint64_t vals[1] = { want };

	atom.count_objs = 1;
	atom.objs_ptr = (uint64_t)(uintptr_t)objs;
	atom.props_ptr = (uint64_t)(uintptr_t)props;
	atom.prop_values_ptr = (uint64_t)(uintptr_t)vals;

	if (ioctl(fd, DRM_IOCTL_MODE_ATOMIC, &atom) < 0 && errno == EINVAL) {
		atom.flags = DRM_MODE_ATOMIC_ALLOW_MODESET;
		if (ioctl(fd, DRM_IOCTL_MODE_ATOMIC, &atom) < 0)
			return -1;
	}
	return 0;
}

static int drm_init(struct drm_state *drm, int angle, uint32_t trait_w, uint32_t trait_h)
{
	memset(drm, 0, sizeof(*drm));
	drm->fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	if (drm->fd < 0)
		return -1;

	struct drm_set_client_cap cap_univ = { .capability = DRM_CLIENT_CAP_UNIVERSAL_PLANES, .value = 1 };
	ioctl(drm->fd, DRM_IOCTL_SET_CLIENT_CAP, &cap_univ);
	struct drm_set_client_cap cap_atomic = { .capability = DRM_CLIENT_CAP_ATOMIC, .value = 1 };
	ioctl(drm->fd, DRM_IOCTL_SET_CLIENT_CAP, &cap_atomic);

	struct drm_mode_card_res res = {0};
	if (ioctl(drm->fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0 || res.count_connectors == 0) {
		close(drm->fd);
		return -1;
	}

	uint32_t *crtcs = calloc(res.count_crtcs, sizeof(uint32_t));
	uint32_t *conns = calloc(res.count_connectors, sizeof(uint32_t));
	uint32_t *encs = calloc(res.count_encoders, sizeof(uint32_t));
	uint32_t *fbs = calloc(res.count_fbs, sizeof(uint32_t));
	if (!crtcs || !conns || !encs || !fbs) {
		free(crtcs); free(conns); free(encs); free(fbs);
		close(drm->fd);
		return -1;
	}

	res.crtc_id_ptr = (uint64_t)(uintptr_t)crtcs;
	res.connector_id_ptr = (uint64_t)(uintptr_t)conns;
	res.encoder_id_ptr = (uint64_t)(uintptr_t)encs;
	res.fb_id_ptr = (uint64_t)(uintptr_t)fbs;
	if (ioctl(drm->fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) {
		free(crtcs); free(conns); free(encs); free(fbs);
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

	/* Allocate double-buffered DRM dumb buffers */
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

	/* Attach first buffer to CRTC */
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
		/* Fallback to setcrtc if page flip fails */
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
			if (!persist) {
				memset(drm->bufs[b].map, 0, drm->bufs[b].size);
			}
			munmap(drm->bufs[b].map, drm->bufs[b].size);
		}
		if (!persist) {
			if (drm->bufs[b].fb_id) {
				ioctl(drm->fd, DRM_IOCTL_MODE_RMFB, drm->bufs[b].fb_id);
			}
			if (drm->bufs[b].handle) {
				struct drm_mode_destroy_dumb dd = { .handle = drm->bufs[b].handle };
				ioctl(drm->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dd);
			}
		}
	}
	if (drm->fd >= 0)
		close(drm->fd);
}

/* ──────────────── Traits & LifeCycle Helpers ──────────────── */

static int open_tty(void)
{
	static const char *tty_paths[] = {
		"/dev/tty0",
		"/dev/tty1",
		"/dev/console",
		NULL
	};

	for (int i = 0; tty_paths[i]; i++) {
		int fd = open(tty_paths[i], O_RDWR | O_NOCTTY | O_CLOEXEC);
		if (fd >= 0)
			return fd;
	}
	return -1;
}

static void trim_str(char *s)
{
	size_t len = strlen(s);
	while (len > 0 && (s[len - 1] == '\r' || s[len - 1] == '\n' || s[len - 1] == ' ' || s[len - 1] == '\t')) {
		s[--len] = '\0';
	}
}

static int read_dt_str(const char *path, char *buf, size_t max_len)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	ssize_t n = read(fd, buf, max_len - 1);
	close(fd);
	if (n <= 0)
		return -1;
	buf[n] = '\0';
	return 0;
}

static int read_dt_rotation(void)
{
	static const char *dt_paths[] = {
		"/sys/firmware/devicetree/base/dsi@fe060000/panel@0/rotation",
		"/proc/device-tree/dsi@fe060000/panel@0/rotation",
		NULL
	};

	for (int i = 0; dt_paths[i]; i++) {
		int fd = open(dt_paths[i], O_RDONLY | O_CLOEXEC);
		if (fd >= 0) {
			uint8_t buf[4];
			if (read(fd, buf, 4) == 4) {
				uint32_t rot = ((uint32_t)buf[0] << 24) |
				               ((uint32_t)buf[1] << 16) |
				               ((uint32_t)buf[2] << 8) |
				               (uint32_t)buf[3];
				close(fd);
				if (rot == 90 || rot == 180 || rot == 270)
					return (int)rot;
			}
			close(fd);
		}
	}
	return 0;
}

static int parse_device_ini(const char *path, int *key_up, int *key_down, int *screen_rot,
			    uint32_t *width, uint32_t *height)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return 0;

	char line[256];
	char parent[64] = {0};

	while (fgets(line, sizeof(line), f)) {
		int val = 0;
		if (sscanf(line, "key_vol_up=%d", &val) == 1 && val > 0)
			*key_up = val;
		else if (sscanf(line, "key_vol_down=%d", &val) == 1 && val > 0)
			*key_down = val;
		else if (sscanf(line, "screen_rotation=%d", &val) == 1 && val >= 0)
			*screen_rot = val;
		else if (sscanf(line, "screen_width=%d", &val) == 1 && val > 0 && width)
			*width = (uint32_t)val;
		else if (sscanf(line, "screen_height=%d", &val) == 1 && val > 0 && height)
			*height = (uint32_t)val;
		else if (sscanf(line, "parent=%63s", parent) == 1) {
			trim_str(parent);
		}
	}
	fclose(f);

	if (parent[0]) {
		char ppath[512];
		snprintf(ppath, sizeof(ppath), "/usr/share/minime/traits/devices/%s.ini", parent);
		parse_device_ini(ppath, key_up, key_down, screen_rot, width, height);
	}
	return 1;
}

static int match_initramfs_traits(int *key_up, int *key_down, int *screen_rot,
				 uint32_t *width, uint32_t *height)
{
	parse_device_ini("/usr/share/minime/traits/platform.ini", key_up, key_down, screen_rot, width, height);

	char model[128] = {0};
	char compat[256] = {0};

	if (read_dt_str("/proc/device-tree/model", model, sizeof(model)) < 0) {
		read_dt_str("/sys/firmware/devicetree/base/model", model, sizeof(model));
	}
	if (read_dt_str("/proc/device-tree/compatible", compat, sizeof(compat)) < 0) {
		read_dt_str("/sys/firmware/devicetree/base/compatible", compat, sizeof(compat));
	}

	trim_str(model);
	trim_str(compat);

	DIR *dir = opendir("/usr/share/minime/traits/devices");
	if (!dir)
		return 0;

	struct dirent *de;
	while ((de = readdir(dir)) != NULL) {
		if (strstr(de->d_name, ".ini") == NULL)
			continue;

		char path[512];
		snprintf(path, sizeof(path), "/usr/share/minime/traits/devices/%s", de->d_name);

		FILE *f = fopen(path, "r");
		if (!f)
			continue;

		char line[256];
		bool in_match = false;
		char m_model[256] = {0};
		char m_compat[256] = {0};

		while (fgets(line, sizeof(line), f)) {
			trim_str(line);
			if (line[0] == '[') {
				in_match = (strcmp(line, "[match]") == 0);
				continue;
			}
			if (in_match) {
				if (strncmp(line, "model=", 6) == 0) {
					snprintf(m_model, sizeof(m_model), "%s", line + 6);
					trim_str(m_model);
				} else if (strncmp(line, "compatible=", 11) == 0) {
					snprintf(m_compat, sizeof(m_compat), "%s", line + 11);
					trim_str(m_compat);
				}
			}
		}
		fclose(f);

		bool matched = false;
		if (m_model[0] && model[0] && strcmp(m_model, model) == 0) {
			if (!m_compat[0] || (compat[0] && strstr(compat, m_compat) != NULL))
				matched = true;
		} else if (m_compat[0] && compat[0] && strstr(compat, m_compat) != NULL) {
			matched = true;
		}

		if (matched) {
			parse_device_ini(path, key_up, key_down, screen_rot, width, height);
			closedir(dir);
			return 1;
		}
	}
	closedir(dir);
	return 0;
}

static void read_traits(int *key_up, int *key_down, int *screen_rot, uint32_t *width, uint32_t *height)
{
	static const char *trait_files[] = {
		"/mnt/sdcard/.minime/traits",
		"/mnt/card/.minime/traits",
		"/etc/traits",
		NULL
	};

	*key_up = KEY_VOLUMEUP;
	*key_down = KEY_VOLUMEDOWN;

	int dt_rot = read_dt_rotation();
	if (dt_rot > 0)
		*screen_rot = dt_rot;

	match_initramfs_traits(key_up, key_down, screen_rot, width, height);

	for (int i = 0; trait_files[i]; i++) {
		FILE *f = fopen(trait_files[i], "r");
		if (!f)
			continue;

		char line[128];
		while (fgets(line, sizeof(line), f)) {
			int val = 0;
			if (sscanf(line, "key_vol_up=%d", &val) == 1 && val > 0)
				*key_up = val;
			else if (sscanf(line, "key_vol_down=%d", &val) == 1 && val > 0)
				*key_down = val;
			else if (sscanf(line, "screen_rotation=%d", &val) == 1 && val >= 0)
				*screen_rot = val;
			else if (sscanf(line, "screen_width=%d", &val) == 1 && val > 0 && width)
				*width = (uint32_t)val;
			else if (sscanf(line, "screen_height=%d", &val) == 1 && val > 0 && height)
				*height = (uint32_t)val;
		}
		fclose(f);
		break;
	}
}

static int scan_input_devices(int *fds, int max_fds)
{
	int count = 0;
	DIR *dir = opendir("/dev/input");
	if (!dir)
		return 0;

	struct dirent *de;
	while ((de = readdir(dir)) != NULL && count < max_fds) {
		if (strncmp(de->d_name, "event", 5) != 0)
			continue;

		char path[512];
		snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);

		int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd >= 0) {
			fds[count++] = fd;
		}
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

static bool check_file_exists(const char *path)
{
	return access(path, F_OK) == 0;
}

static const char *custom_bmp_paths[] = {
	"/mnt/sdcard/.minime/bootsplash.bmp",
	"/mnt/card/.minime/bootsplash.bmp",
	"/usr/share/minime/bootsplash.bmp",
	NULL
};

static const char *find_custom_bmp(void)
{
	for (int i = 0; custom_bmp_paths[i]; i++) {
		if (access(custom_bmp_paths[i], R_OK) == 0)
			return custom_bmp_paths[i];
	}
	return NULL;
}

static int load_and_draw_bmp(const struct render_surface *surf, const char *path,
			     uint32_t *out_w, uint32_t *out_h, uint32_t *out_y)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;

	uint8_t header[54];
	if (read(fd, header, 54) != 54) {
		close(fd);
		return -1;
	}

	if (header[0] != 'B' || header[1] != 'M') {
		close(fd);
		return -1;
	}

	uint32_t data_offset = *(uint32_t *)&header[10];
	int32_t w = *(int32_t *)&header[18];
	int32_t h = *(int32_t *)&header[22];
	uint16_t bpp = *(uint16_t *)&header[28];

	if (w <= 0 || h == 0 || (bpp != 24 && bpp != 32)) {
		close(fd);
		return -1;
	}

	int abs_h = h > 0 ? h : -h;
	bool flip_y = (h > 0);

	size_t row_bytes = (size_t)w * (bpp / 8);
	size_t pad_row = (row_bytes + 3) & ~3;
	uint8_t *row_buf = malloc(pad_row);
	if (!row_buf) {
		close(fd);
		return -1;
	}

	if (lseek(fd, data_offset, SEEK_SET) == (off_t)-1) {
		free(row_buf);
		close(fd);
		return -1;
	}

	uint32_t start_x = (surf->log_width > (uint32_t)w) ? (surf->log_width - (uint32_t)w) / 2 : 0;
	uint32_t start_y = (surf->log_height > (uint32_t)abs_h) ? (surf->log_height - (uint32_t)abs_h) / 2 : 0;

	for (int y = 0; y < abs_h; y++) {
		if (read(fd, row_buf, pad_row) != (ssize_t)pad_row)
			break;

		int dst_y = flip_y ? (abs_h - 1 - y) : y;
		for (int x = 0; x < w; x++) {
			uint8_t *p = row_buf + (x * (bpp / 8));
			uint32_t col = (surf->bpp == 16)
				? pack_rgb16(p[2], p[1], p[0])
				: pack_rgb32(p[2], p[1], p[0]);
			put_pixel(surf, start_x + (uint32_t)x, start_y + (uint32_t)dst_y, col);
		}
	}

	free(row_buf);
	close(fd);

	if (out_w) *out_w = (uint32_t)w;
	if (out_h) *out_h = (uint32_t)abs_h;
	if (out_y) *out_y = start_y;
	return 0;
}

int main(int argc, char **argv)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sig_handler;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);

	int key_vol_up = KEY_VOLUMEUP;
	int key_vol_down = KEY_VOLUMEDOWN;
	int screen_rot = 0;
	uint32_t trait_w = 0, trait_h = 0;
	read_traits(&key_vol_up, &key_vol_down, &screen_rot, &trait_w, &trait_h);

	parse_logo_grid();


	bool persist = false;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--persist") == 0 || strcmp(argv[i], "--no-clear") == 0) {
			persist = true;
		} else if (strcmp(argv[i], "--rotate") == 0 && i + 1 < argc) {
			screen_rot = atoi(argv[++i]);
		} else if (strcmp(argv[i], "90") == 0 || strcmp(argv[i], "180") == 0 ||
			   strcmp(argv[i], "270") == 0 || strcmp(argv[i], "0") == 0) {
			screen_rot = atoi(argv[i]);
		}
	}

	int tty_fd = open_tty();
	if (tty_fd >= 0) {
		ioctl(tty_fd, KDSETMODE, KD_GRAPHICS);
	}

	struct drm_state drm;
	bool use_drm = (drm_init(&drm, screen_rot, trait_w, trait_h) == 0);

	struct fb_ctx fb;
	memset(&fb, 0, sizeof(fb));

	struct render_surface surf;
	memset(&surf, 0, sizeof(surf));

	if (use_drm) {
		surf.width = drm.width;
		surf.height = drm.height;
		surf.pitch = drm.bufs[0].pitch;
		surf.bpp = 32;
		surf.mem = drm.bufs[0].map;
	} else {
		/* Fallback to fbdev */
		fb.fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
		if (fb.fd < 0) {
			if (tty_fd >= 0) {
				ioctl(tty_fd, KDSETMODE, KD_TEXT);
				close(tty_fd);
			}
			return 1;
		}

		struct fb_var_screeninfo vinfo;
		struct fb_fix_screeninfo finfo;
		if (ioctl(fb.fd, FBIOGET_VSCREENINFO, &vinfo) < 0 ||
		    ioctl(fb.fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
			close(fb.fd);
			if (tty_fd >= 0) {
				ioctl(tty_fd, KDSETMODE, KD_TEXT);
				close(tty_fd);
			}
			return 1;
		}

		fb.xres = vinfo.xres;
		fb.yres = vinfo.yres;
		fb.bpp = vinfo.bits_per_pixel;
		fb.line_length = finfo.line_length ? finfo.line_length : (fb.xres * (fb.bpp / 8));
		fb.mem_size = finfo.smem_len ? finfo.smem_len : (fb.line_length * fb.yres);

		fb.mem = mmap(NULL, fb.mem_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb.fd, 0);
		if (fb.mem == MAP_FAILED) {
			close(fb.fd);
			if (tty_fd >= 0) {
				ioctl(tty_fd, KDSETMODE, KD_TEXT);
				close(tty_fd);
			}
			return 1;
		}

		surf.width = fb.xres;
		surf.height = fb.yres;
		surf.pitch = fb.line_length;
		surf.bpp = fb.bpp;
		surf.mem = fb.mem;
	}

	surf.rotation = screen_rot;
	if (surf.rotation == 90 || surf.rotation == 270) {
		surf.log_width = surf.height;
		surf.log_height = surf.width;
	} else {
		surf.log_width = surf.width;
		surf.log_height = surf.height;
	}

	/* Geometry calculations for landscape layout */
	uint32_t log_w = surf.log_width;
	uint32_t log_h = surf.log_height;

	uint32_t cell_w = log_w / (g_logo_cols + 10);
	if (cell_w < 4) cell_w = 4;
	if (cell_w > 18) cell_w = 18;
	uint32_t cell_h = cell_w;

	uint32_t word_w = g_logo_cols * cell_w;
	uint32_t word_h = g_logo_rows * cell_h;

	uint32_t gap_h = (cell_h * 3) / 2;
	uint32_t bar_h = (cell_h * 2) / 3;
	if (bar_h < 6) bar_h = 6;
	if (bar_h > 16) bar_h = 16;
	uint32_t total_h = word_h + gap_h + bar_h;

	uint32_t start_x = (log_w > word_w) ? (log_w - word_w) / 2 : 0;
	uint32_t start_y = (log_h > total_h) ? (log_h - total_h) / 2 : 0;
	const char *bmp_file = find_custom_bmp();
	uint32_t bmp_w = 0, bmp_h = 0, bmp_y = 0;
	bool has_bmp = (bmp_file && load_and_draw_bmp(&surf, bmp_file, &bmp_w, &bmp_h, &bmp_y) == 0);

	uint32_t fg_color = (surf.bpp == 16) ? pack_rgb16(240, 240, 240) : pack_rgb32(240, 240, 240);

	uint32_t track_x = start_x;
	uint32_t track_y = start_y + word_h + gap_h;
	uint32_t track_w = word_w;

	if (has_bmp) {
		track_x = (log_w > bmp_w) ? (log_w - bmp_w) / 2 : start_x;
		track_y = bmp_y + bmp_h + 16;
		track_w = (bmp_w > 80) ? bmp_w : 80;
		if (track_y + bar_h > log_h)
			track_y = log_h - bar_h - 8;
	} else {
		clear_surface(&surf);
		draw_wordmark(&surf, start_x, start_y, cell_w, cell_h, fg_color);
	}

	uint32_t beam_w = track_w / 3;
	if (beam_w < 20) beam_w = 20;

	uint32_t scratch_buf[4096];
	if (track_w > 4096)
		track_w = 4096;

	if (use_drm) {
		/* Initialize second buffer identically for seamless double-buffering */
		surf.mem = drm.bufs[1].map;
		clear_surface(&surf);
		if (has_bmp) {
			load_and_draw_bmp(&surf, bmp_file, NULL, NULL, NULL);
		} else {
			draw_wordmark(&surf, start_x, start_y, cell_w, cell_h, fg_color);
		}
		surf.mem = drm.bufs[0].map;
	}

	int input_fds[MAX_INPUTS];
	int input_count = scan_input_devices(input_fds, MAX_INPUTS);

	bool in_graphics_mode = true;
	int32_t beam_pos = 0;
	int step = (int)(track_w / 35);
	if (step < 2) step = 2;

	time_t start_time = time(NULL);

	while (g_running) {
		if (check_file_exists("/run/openrc/started/ui")) {
			if (use_drm) {
				surf.mem = drm.bufs[drm.cur_buf].map;
				clear_surface(&surf);
				drm_flip(&drm);
			} else {
				clear_surface(&surf);
			}
			break;
		}
		if (check_file_exists("/run/openrc/failed/ui")) {
			if (tty_fd >= 0)
				ioctl(tty_fd, KDSETMODE, KD_TEXT);
			break;
		}
		if (time(NULL) - start_time > TIMEOUT_SECS) {
			if (tty_fd >= 0)
				ioctl(tty_fd, KDSETMODE, KD_TEXT);
			break;
		}

		/* Poll input devices */
		struct pollfd pfds[MAX_INPUTS];
		for (int i = 0; i < input_count; i++) {
			pfds[i].fd = input_fds[i];
			pfds[i].events = POLLIN;
			pfds[i].revents = 0;
		}

		int poll_ret = poll(pfds, input_count, 20);
		if (poll_ret > 0) {
			for (int i = 0; i < input_count; i++) {
				if (!(pfds[i].revents & POLLIN))
					continue;

				struct input_event ev;
				while (read(pfds[i].fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
					if (ev.type != EV_KEY || ev.value != 1)
						continue;

					if (ev.code == key_vol_up) {
						if (in_graphics_mode) {
							if (tty_fd >= 0)
								ioctl(tty_fd, KDSETMODE, KD_TEXT);
							in_graphics_mode = false;
						}
					} else if (ev.code == key_vol_down) {
						if (!in_graphics_mode) {
							if (tty_fd >= 0)
								ioctl(tty_fd, KDSETMODE, KD_GRAPHICS);
							in_graphics_mode = true;
							if (use_drm) {
								surf.mem = drm.bufs[drm.cur_buf].map;
								clear_surface(&surf);
								if (has_bmp) {
									load_and_draw_bmp(&surf, bmp_file, NULL, NULL, NULL);
								} else {
									draw_wordmark(&surf, start_x, start_y, cell_w, cell_h, fg_color);
								}
							} else {
								clear_surface(&surf);
								if (has_bmp) {
									load_and_draw_bmp(&surf, bmp_file, NULL, NULL, NULL);
								} else {
									draw_wordmark(&surf, start_x, start_y, cell_w, cell_h, fg_color);
								}
							}
						}
					}
				}
			}
		}

		/* Animate progress bar in KD_GRAPHICS mode */
		if (in_graphics_mode) {
			if (use_drm) {
				surf.mem = drm.bufs[drm.cur_buf].map;
			}
			draw_gradient_bar(&surf, track_x, track_y, track_w, bar_h,
					  beam_pos, beam_w, scratch_buf);

			if (use_drm) {
				drm_flip(&drm);
			}

			beam_pos += step;
			if (beam_pos >= (int32_t)track_w) {
				beam_pos -= (int32_t)track_w;
			}
		}
	}

	/* Clear screen on exit unless persist requested */
	if (!persist && (tty_fd < 0 || in_graphics_mode)) {
		if (use_drm) {
			surf.mem = drm.bufs[drm.cur_buf].map;
			clear_surface(&surf);
			drm_flip(&drm);
		} else {
			clear_surface(&surf);
		}
	}

	close_input_devices(input_fds, input_count);

	if (use_drm) {
		drm_cleanup(&drm, persist);
	} else {
		if (!persist) {
			clear_surface(&surf);
		}
		munmap(fb.mem, fb.mem_size);
		close(fb.fd);
	}

	if (tty_fd >= 0)
		close(tty_fd);

	return 0;
}
