/* dacam: a camera app that does exactly one thing.
 * live preview + shutter + self-timer, ~500 lines total, zero GUI
 * toolkit, zero video framework. */
#define _POSIX_C_SOURCE 199309L
#define _DEFAULT_SOURCE

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "config.h"
#include "v4l2cap.h"
#include "win.h"
#include "draw.h"

static Cam cam;
static Win *win;
static int win_w, win_h;
static uint8_t *last_frame; /* rgb24 copy of the most recent preview frame */

static int timer_armed = 0;
static struct timespec timer_deadline;
static int pending_shot = 0;

typedef struct { int x, y, w, h; } Rect;
static Rect btn_rect[BTN_COUNT];

static double now_mono(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void layout_buttons(void) {
	int n = BTN_COUNT, gap = 8, bw = (win_w - gap * (n + 1)) / n, bh = ctrl_height - 16;
	int x = gap, y = cap_height + 8;
	for (int i = 0; i < n; i++) {
		btn_rect[i] = (Rect){ x, y, bw, bh };
		x += bw + gap;
	}
}

static const char *resolved_save_dir(void) {
	/* If config.h set an explicit directory, use it verbatim. */
	if (save_dir) return save_dir;
	/* Otherwise build $HOME/<save_subdir> once and cache it. */
	static char dir[512];
	if (dir[0]) return dir;
	const char *home = getenv("HOME");
	if (!home || !*home) {
		fprintf(stderr, "dacam: $HOME not set, saving to current directory\n");
		dir[0] = '.'; dir[1] = '\0';
	} else {
		snprintf(dir, sizeof(dir), "%s/%s", home, save_subdir);
	}
	return dir;
}

static void save_current_frame(void) {
	if (!last_frame) return;
	time_t t = time(NULL);
	struct tm tm; localtime_r(&t, &tm);
	char path[512];
	snprintf(path, sizeof(path), "%s/%s_%04d-%02d-%02d_%02d%02d%02d.png",
	         resolved_save_dir(), save_prefix,
	         tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
	         tm.tm_hour, tm.tm_min, tm.tm_sec);
	if (stbi_write_png(path, cam.width, cam.height, 3, last_frame, cam.width * 3))
		fprintf(stderr, "saved %s\n", path);
	else
		fprintf(stderr, "failed to write %s\n", path);
}

static void on_key(uint32_t key) {
	if (key == key_shoot) {
		pending_shot = 1;
	} else if (key == key_timer) {
		timer_armed = !timer_armed;
		if (timer_armed) {
			clock_gettime(CLOCK_MONOTONIC, &timer_deadline);
			timer_deadline.tv_sec += timer_seconds_default;
		}
	} else if (key == key_quit) {
		exit(0);
	}
}

static void on_click(int x, int y) {
	for (int i = 0; i < BTN_COUNT; i++) {
		Rect r = btn_rect[i];
		if (x < r.x || x >= r.x + r.w || y < r.y || y >= r.y + r.h) continue;
		switch (i) {
		case BTN_SHOOT:
			pending_shot = 1;
			break;
		case BTN_TIMER:
			timer_armed = !timer_armed;
			if (timer_armed) {
				timer_deadline = (struct timespec){0};
				clock_gettime(CLOCK_MONOTONIC, &timer_deadline);
				timer_deadline.tv_sec += timer_seconds_default;
			}
			break;
		case BTN_QUIT:
			exit(0);
		}
	}
}

static void draw_frame(void) {
	uint32_t *px = win_buffer(win);
	draw_fill(px, win_w, 0, 0, win_w, win_h, col_bg);

	if (last_frame)
		draw_rgb24(px, win_w, last_frame, cam.width, cam.height);

	/* control bar buttons */
	for (int i = 0; i < BTN_COUNT; i++) {
		Rect r = btn_rect[i];
		uint32_t c = col_btn;
		if (i == BTN_TIMER && timer_armed) c = col_btn_arm;
		draw_fill(px, win_w, r.x, r.y, r.w, r.h, c);
		int tw = text_width(btn_labels[i], 2);
		draw_text(px, win_w, r.x + (r.w - tw) / 2, r.y + (r.h - 14) / 2, 2,
		          btn_labels[i], col_text);
	}

	/* countdown overlay, big digit centered on the preview */
	if (timer_armed) {
		double remain = timer_deadline.tv_sec - now_mono() + (timer_deadline.tv_nsec / 1e9);
		int secs = (int)(remain + 0.999);
		if (secs > 0 && secs <= 9) {
			char d[2] = { '0' + secs, 0 };
			int scale = 12;
			int tw = text_width(d, scale);
			draw_text(px, win_w, (cap_width - tw) / 2, (cap_height - 7 * scale) / 2,
			          scale, d, col_timer);
		}
	}

	win_commit(win);
}

int main(void) {
	win_w = cap_width;
	win_h = cap_height + ctrl_height;

	if (cam_open(&cam, video_device, cap_width, cap_height) < 0) {
		fprintf(stderr, "failed to open %s\n", video_device);
		return 1;
	}
	/* driver may have picked a different size than requested */
	win_w = cam.width;
	win_h = cam.height + ctrl_height;

	win = win_open(win_w, win_h, on_click, on_key);
	layout_buttons();

	struct pollfd pfds[2] = {
		{ .fd = cam.fd, .events = POLLIN },
		{ .fd = win_fd(win), .events = POLLIN },
	};

	for (;;) {
		/* redraw at ~30fps while polling both fds; the countdown digit
		 * and the timer's expiry are both timing-sensitive enough that
		 * we don't want to block indefinitely on either fd. */
		int r = poll(pfds, 2, 33);
		if (r < 0) break;

		if (pfds[0].revents & POLLIN) {
			uint8_t *frame = cam_read_frame(&cam);
			if (frame) {
				if (!last_frame) last_frame = malloc((size_t)cam.width * cam.height * 3);
				memcpy(last_frame, frame, (size_t)cam.width * cam.height * 3);
			}
		}
		if (pfds[1].revents & POLLIN) {
			if (win_dispatch(win, 0) < 0) break; /* compositor closed us */
		}

		if (timer_armed && now_mono() >= timer_deadline.tv_sec + timer_deadline.tv_nsec / 1e9) {
			timer_armed = 0;
			pending_shot = 1;
		}
		if (pending_shot) {
			pending_shot = 0;
			save_current_frame();
		}

		draw_frame();
	}

	win_close(win);
	cam_close(&cam);
	free(last_frame);
	return 0;
}
