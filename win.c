/* Bare-metal Wayland client: wl_shm double buffering + xdg-shell.
 * This is the entire "toolkit" — deliberately, in the spirit of dwm's
 * direct Xlib use instead of a GUI framework. */

#define _DEFAULT_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include "win.h"
#include "xdg-shell-client-protocol.h"

struct Win {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct xdg_wm_base *wm_base;
	struct wl_seat *seat;
	struct wl_pointer *pointer;

	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;

	int width, height, stride;
	struct wl_buffer *buf[2];
	uint32_t *pixels[2];
	int cur; /* which of buf[]/pixels[] we're drawing into */

	int pointer_x, pointer_y;
	ClickHandler on_click;
	int closed;
};

/* ---- shm buffer pool (2 buffers, no reuse tracking needed at our
 * frame rate — good enough for a suckless tool) ---------------------- */

static struct wl_buffer *make_buffer(Win *w, uint32_t **out_pixels) {
	int size = w->stride * w->height;
	char name[] = "/suckcam-XXXXXX";
	int fd = -1;
	for (int i = 0; i < 100 && fd < 0; i++) {
		name[9] = 'A' + (rand() % 26);
		fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
	}
	if (fd < 0) { perror("shm_open"); exit(1); }
	shm_unlink(name);
	ftruncate(fd, size);
	void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	struct wl_shm_pool *pool = wl_shm_create_pool(w->shm, fd, size);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(
		pool, 0, w->width, w->height, w->stride, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	*out_pixels = data;
	return buffer;
}

/* ---- pointer ---- */

static void ptr_motion(void *d, struct wl_pointer *p, uint32_t t,
                        wl_fixed_t x, wl_fixed_t y) {
	(void)p;(void)t;
	Win *w = d;
	w->pointer_x = wl_fixed_to_int(x);
	w->pointer_y = wl_fixed_to_int(y);
}
static void ptr_button(void *d, struct wl_pointer *p, uint32_t s, uint32_t t,
                        uint32_t button, uint32_t state) {
	(void)p;(void)s;(void)t;(void)button;
	Win *w = d;
	if (state == WL_POINTER_BUTTON_STATE_PRESSED && w->on_click)
		w->on_click(w->pointer_x, w->pointer_y);
}
static void ptr_noop1(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *sf, wl_fixed_t x, wl_fixed_t y)
	{ (void)d;(void)p;(void)s;(void)sf;(void)x;(void)y; }
static void ptr_noop2(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *sf)
	{ (void)d;(void)p;(void)s;(void)sf; }
static void ptr_noop3(void *d, struct wl_pointer *p, uint32_t t, uint32_t axis, wl_fixed_t v)
	{ (void)d;(void)p;(void)t;(void)axis;(void)v; }
static void ptr_noop4(void *d, struct wl_pointer *p)
	{ (void)d;(void)p; }
static const struct wl_pointer_listener pointer_listener = {
	.enter = ptr_noop1, .leave = ptr_noop2, .motion = ptr_motion,
	.button = ptr_button, .axis = ptr_noop3, .frame = ptr_noop4,
};

static void seat_caps(void *d, struct wl_seat *seat, uint32_t caps) {
	Win *w = d;
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !w->pointer) {
		w->pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(w->pointer, &pointer_listener, w);
	}
}
static void seat_name(void *d, struct wl_seat *s, const char *n) { (void)d;(void)s;(void)n; }
static const struct wl_seat_listener seat_listener = { seat_caps, seat_name };

/* ---- xdg-shell plumbing ---- */

static void xdg_wm_ping(void *d, struct xdg_wm_base *b, uint32_t serial) {
	(void)d;
	xdg_wm_base_pong(b, serial);
}
static const struct xdg_wm_base_listener wm_base_listener = { xdg_wm_ping };

static void xdg_surf_configure(void *d, struct xdg_surface *s, uint32_t serial) {
	(void)d;
	xdg_surface_ack_configure(s, serial);
}
static const struct xdg_surface_listener xdg_surface_listener = { xdg_surf_configure };

static void toplevel_configure(void *d, struct xdg_toplevel *t, int32_t w,
                                int32_t h, struct wl_array *states)
	{ (void)d;(void)t;(void)w;(void)h;(void)states; }
static void toplevel_close(void *d, struct xdg_toplevel *t) {
	(void)t;
	((Win *)d)->closed = 1;
}
static const struct xdg_toplevel_listener toplevel_listener = {
	.configure = toplevel_configure,
	.close     = toplevel_close,
};

/* ---- registry ---- */

static void reg_global(void *d, struct wl_registry *r, uint32_t name,
                        const char *iface, uint32_t ver) {
	(void)ver;
	Win *w = d;
	if (!strcmp(iface, wl_compositor_interface.name))
		w->compositor = wl_registry_bind(r, name, &wl_compositor_interface, 4);
	else if (!strcmp(iface, wl_shm_interface.name))
		w->shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
	else if (!strcmp(iface, xdg_wm_base_interface.name))
		w->wm_base = wl_registry_bind(r, name, &xdg_wm_base_interface, 1);
	else if (!strcmp(iface, wl_seat_interface.name))
		w->seat = wl_registry_bind(r, name, &wl_seat_interface, 5);
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t name) { (void)d;(void)r;(void)name; }
static const struct wl_registry_listener registry_listener = { reg_global, reg_remove };

Win *win_open(int width, int height, ClickHandler on_click) {
	Win *w = calloc(1, sizeof(Win));
	w->width = width; w->height = height; w->stride = width * 4;
	w->on_click = on_click;

	w->display = wl_display_connect(NULL);
	if (!w->display) { fprintf(stderr, "no Wayland display (is a compositor running?)\n"); exit(1); }

	w->registry = wl_display_get_registry(w->display);
	wl_registry_add_listener(w->registry, &registry_listener, w);
	wl_display_roundtrip(w->display); /* collect globals */

	if (!w->compositor || !w->shm || !w->wm_base) {
		fprintf(stderr, "compositor missing wl_compositor/wl_shm/xdg_wm_base\n");
		exit(1);
	}
	xdg_wm_base_add_listener(w->wm_base, &wm_base_listener, w);
	if (w->seat) wl_seat_add_listener(w->seat, &seat_listener, w);

	w->surface = wl_compositor_create_surface(w->compositor);
	w->xdg_surface = xdg_wm_base_get_xdg_surface(w->wm_base, w->surface);
	xdg_surface_add_listener(w->xdg_surface, &xdg_surface_listener, w);
	w->xdg_toplevel = xdg_surface_get_toplevel(w->xdg_surface);
	xdg_toplevel_add_listener(w->xdg_toplevel, &toplevel_listener, w);
	xdg_toplevel_set_title(w->xdg_toplevel, "suckcam");
	xdg_toplevel_set_app_id(w->xdg_toplevel, "suckcam");
	wl_surface_commit(w->surface);
	wl_display_roundtrip(w->display); /* get initial configure */

	w->buf[0] = make_buffer(w, &w->pixels[0]);
	w->buf[1] = make_buffer(w, &w->pixels[1]);
	return w;
}

uint32_t *win_buffer(Win *w) { return w->pixels[w->cur]; }

void win_commit(Win *w) {
	struct wl_buffer *b = w->buf[w->cur];
	wl_surface_attach(w->surface, b, 0, 0);
	wl_surface_damage_buffer(w->surface, 0, 0, w->width, w->height);
	wl_surface_commit(w->surface);
	wl_display_flush(w->display); /* push commit out to compositor immediately */
	w->cur ^= 1;
}

int win_dispatch(Win *w, int timeout_ms) {
	(void)timeout_ms;
	if (w->closed) return -1;
	/* wl_display_dispatch_pending only drains the already-parsed queue;
	 * we must also read new bytes off the socket (main.c's poll already
	 * told us POLLIN is set).  Use the prepare/read/dispatch sequence so
	 * we don't race with a concurrent flush from win_commit. */
	if (wl_display_prepare_read(w->display) == 0) {
		wl_display_read_events(w->display);
	}
	if (wl_display_dispatch_pending(w->display) < 0) return -1;
	if (w->closed) return -1;
	return 1;
}

int win_fd(Win *w) { return wl_display_get_fd(w->display); }

void win_close(Win *w) {
	if (w->pointer) wl_pointer_destroy(w->pointer);
	if (w->seat) wl_seat_destroy(w->seat);
	xdg_toplevel_destroy(w->xdg_toplevel);
	xdg_surface_destroy(w->xdg_surface);
	wl_surface_destroy(w->surface);
	wl_display_disconnect(w->display);
	free(w);
}
