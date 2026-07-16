#ifndef WIN_H
#define WIN_H

#include <stdint.h>

typedef void (*ClickHandler)(int x, int y);

typedef struct Win Win;

/* Opens a Wayland window of given size, ARGB8888 software surface.
 * on_click fires on pointer button-down with surface-local coords. */
Win *win_open(int width, int height, ClickHandler on_click);

/* Returns the pixel buffer for the frame currently being drawn into.
 * Caller writes ARGB8888, row-major, stride == width*4. */
uint32_t *win_buffer(Win *w);

/* Pushes the buffer to the compositor. */
void win_commit(Win *w);

/* Pumps Wayland + returns >0 if events were processed, 0 on timeout,
 * <0 on disconnect. timeout_ms bounds the wait (use for animating the
 * countdown/live-preview redraw loop). */
int win_dispatch(Win *w, int timeout_ms);

/* fd suitable for select()/poll() alongside e.g. a v4l2 fd */
int win_fd(Win *w);

void win_close(Win *w);

#endif
