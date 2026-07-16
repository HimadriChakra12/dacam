/* X11 backend for dacam — implements the same win.h interface as win.c.
 * Uses MIT-SHM (XShm) for zero-copy pixel delivery when available,
 * falls back to XPutImage otherwise.  No Xft, no xcb, no xkbcommon —
 * just raw Xlib the way st/dwm/sxiv do it. */

#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/XShm.h>

/* linux/input-event-codes.h defines KEY_SPACE etc. — we need those so
 * on_key() in main.c can compare against config.h's key_shoot/key_timer/
 * key_quit without any change.  The X11 backend maps XK_* → KEY_*. */
#include <linux/input-event-codes.h>

#include "win.h"

struct Win {
	Display     *dpy;
	int          scr;
	Window       win;
	GC           gc;
	Atom         wm_delete;

	int          width, height;
	ClickHandler on_click;
	KeyHandler   on_key;
	int          shift_down;

	/* MIT-SHM double buffer */
	int              shm_ok;
	XShmSegmentInfo  shm[2];
	XImage          *img[2];
	int              cur;   /* which buffer we're drawing into */

	/* fallback (no SHM): single malloc'd buffer + XImage wrapper */
	uint32_t        *fb_pixels;
	XImage          *fb_img;

	int closed;
};

/* ---- XShm helpers --------------------------------------------------- */

static int shm_alloc(Win *w, int idx) {
	int depth = DefaultDepth(w->dpy, w->scr);
	Visual *vis = DefaultVisual(w->dpy, w->scr);
	w->img[idx] = XShmCreateImage(w->dpy, vis, depth, ZPixmap,
	                               NULL, &w->shm[idx],
	                               w->width, w->height);
	if (!w->img[idx]) return 0;
	int sz = w->img[idx]->bytes_per_line * w->height;
	w->shm[idx].shmid = shmget(IPC_PRIVATE, sz, IPC_CREAT | 0600);
	if (w->shm[idx].shmid < 0) { XDestroyImage(w->img[idx]); return 0; }
	w->shm[idx].shmaddr = shmat(w->shm[idx].shmid, NULL, 0);
	w->img[idx]->data  = w->shm[idx].shmaddr;
	w->shm[idx].readOnly = False;
	if (!XShmAttach(w->dpy, &w->shm[idx])) {
		shmdt(w->shm[idx].shmaddr);
		shmctl(w->shm[idx].shmid, IPC_RMID, NULL);
		XDestroyImage(w->img[idx]);
		return 0;
	}
	/* mark for auto-removal: segment stays until last detach */
	shmctl(w->shm[idx].shmid, IPC_RMID, NULL);
	return 1;
}

static void shm_free(Win *w, int idx) {
	if (!w->img[idx]) return;
	XShmDetach(w->dpy, &w->shm[idx]);
	shmdt(w->shm[idx].shmaddr);
	XDestroyImage(w->img[idx]);
	w->img[idx] = NULL;
}

/* ---- KeySym → Linux evdev keycode ----------------------------------- */
/* We only need to handle the keys config.h actually exposes.
 * The mapping is: XK_* (X keysym) → KEY_* (linux/input-event-codes.h).
 * main.c compares on_key's argument against key_shoot/key_timer/key_quit
 * which are defined as KEY_SPACE/KEY_ENTER/KEY_Q — same values on both
 * backends, so config.h works without modification. */
static uint32_t keysym_to_evdev(KeySym ks) {
	switch (ks) {
	case XK_space:        return KEY_SPACE;
	case XK_Return:
	case XK_KP_Enter:     return KEY_ENTER;
	case XK_q:
	case XK_Q:            return KEY_Q;
	case XK_o:
	case XK_O:            return KEY_O;
	/* extend here for any future config.h bindings */
	default:              return 0; /* unmapped — on_key ignores 0 */
	}
}

/* ---- public API ----------------------------------------------------- */

Win *win_open(int width, int height, ClickHandler on_click, KeyHandler on_key) {
	Win *w = calloc(1, sizeof(Win));
	w->width    = width;
	w->height   = height;
	w->on_click = on_click;
	w->on_key   = on_key;

	w->dpy = XOpenDisplay(NULL);
	if (!w->dpy) {
		fprintf(stderr, "dacam: cannot open X display ($DISPLAY not set?)\n");
		exit(1);
	}
	w->scr = DefaultScreen(w->dpy);

	/* create window */
	XSetWindowAttributes swa = {
		.background_pixel = BlackPixel(w->dpy, w->scr),
		.event_mask = ExposureMask | ButtonPressMask
		            | KeyPressMask | KeyReleaseMask
		            | StructureNotifyMask,
	};
	w->win = XCreateWindow(
		w->dpy, RootWindow(w->dpy, w->scr),
		0, 0, width, height, 0,
		DefaultDepth(w->dpy, w->scr),
		InputOutput,
		DefaultVisual(w->dpy, w->scr),
		CWBackPixel | CWEventMask, &swa);

	XStoreName(w->dpy, w->win, "dacam");

	/* WM_DELETE_WINDOW so clicking × calls our handler */
	w->wm_delete = XInternAtom(w->dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(w->dpy, w->win, &w->wm_delete, 1);

	/* fixed size hint — same as the Wayland version (no resize) */
	XSizeHints *sh = XAllocSizeHints();
	sh->flags = PMinSize | PMaxSize;
	sh->min_width = sh->max_width = width;
	sh->min_height = sh->max_height = height;
	XSetWMNormalHints(w->dpy, w->win, sh);
	XFree(sh);

	w->gc = XCreateGC(w->dpy, w->win, 0, NULL);
	XMapWindow(w->dpy, w->win);

	/* try MIT-SHM double-buffering */
	int shm_event_base, shm_error_base;
	if (XShmQueryExtension(w->dpy) &&
	    XShmQueryExtension(w->dpy) &&
	    XQueryExtension(w->dpy, "MIT-SHM",
	                    &shm_event_base, &shm_event_base, &shm_error_base)) {
		if (shm_alloc(w, 0) && shm_alloc(w, 1)) {
			w->shm_ok = 1;
		} else {
			shm_free(w, 0);
			shm_free(w, 1);
		}
	}

	if (!w->shm_ok) {
		/* fallback: malloc buffer, wrap in XImage */
		fprintf(stderr, "dacam: MIT-SHM unavailable, using XPutImage fallback\n");
		w->fb_pixels = calloc((size_t)width * height, 4);
		int depth = DefaultDepth(w->dpy, w->scr);
		w->fb_img = XCreateImage(
			w->dpy, DefaultVisual(w->dpy, w->scr), depth,
			ZPixmap, 0, (char *)w->fb_pixels,
			width, height, 32, width * 4);
	}

	XSync(w->dpy, False);
	return w;
}

uint32_t *win_buffer(Win *w) {
	if (w->shm_ok)
		return (uint32_t *)w->img[w->cur]->data;
	return w->fb_pixels;
}

void win_commit(Win *w) {
	if (w->shm_ok) {
		XShmPutImage(w->dpy, w->win, w->gc, w->img[w->cur],
		             0, 0, 0, 0, w->width, w->height, False);
		w->cur ^= 1;
	} else {
		/* XPutImage doesn't free data, safe to reuse */
		XPutImage(w->dpy, w->win, w->gc, w->fb_img,
		          0, 0, 0, 0, w->width, w->height);
	}
	XFlush(w->dpy);
}

int win_dispatch(Win *w, int timeout_ms) {
	(void)timeout_ms;
	if (w->closed) return -1;

	while (XPending(w->dpy)) {
		XEvent ev;
		XNextEvent(w->dpy, &ev);

		switch (ev.type) {
		case ButtonPress:
			if (w->on_click)
				w->on_click(ev.xbutton.x, ev.xbutton.y);
			break;

		case KeyPress: {
			KeySym ks = XLookupKeysym(&ev.xkey, 0);
			if (ks == XK_Shift_L || ks == XK_Shift_R) {
				w->shift_down = 1;
				break;
			}
			uint32_t evdev = keysym_to_evdev(ks);
			if (evdev && w->on_key)
				w->on_key(evdev, w->shift_down ? MOD_SHIFT : 0);
			break;
		}

		case KeyRelease: {
			KeySym ks = XLookupKeysym(&ev.xkey, 0);
			if (ks == XK_Shift_L || ks == XK_Shift_R)
				w->shift_down = 0;
			break;
		}

		case ClientMessage:
			if ((Atom)ev.xclient.data.l[0] == w->wm_delete)
				w->closed = 1;
			break;

		case Expose:
			/* re-blit current buffer on expose */
			win_commit(w);
			break;

		default:
			break;
		}
	}

	if (w->closed) return -1;
	return 1;
}

int win_fd(Win *w) {
	return ConnectionNumber(w->dpy);  /* Xlib socket fd — polls like Wayland */
}

void win_close(Win *w) {
	if (w->shm_ok) {
		shm_free(w, 0);
		shm_free(w, 1);
	} else {
		/* XDestroyImage would free data too — prevent double-free */
		w->fb_img->data = NULL;
		XDestroyImage(w->fb_img);
		free(w->fb_pixels);
	}
	XFreeGC(w->dpy, w->gc);
	XDestroyWindow(w->dpy, w->win);
	XCloseDisplay(w->dpy);
	free(w);
}
