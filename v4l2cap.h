#ifndef V4L2CAP_H
#define V4L2CAP_H

#include <stdint.h>

typedef struct {
	int fd;
	int width, height;
	uint32_t fourcc;       /* negotiated format, e.g. V4L2_PIX_FMT_YUYV */
	void **bufs;           /* mmap'd buffers */
	unsigned *buflens;
	unsigned nbufs;
} Cam;

/* opens device, requests width/height, mmaps buffers, starts streaming.
 * returns 0 on success. */
int cam_open(Cam *cam, const char *device, int width, int height);

/* blocks until a frame is ready, returns pointer to RGB24 buffer of
 * cam->width * cam->height * 3 bytes (converted internally), valid until
 * the next call to cam_read_frame. returns NULL on error. */
uint8_t *cam_read_frame(Cam *cam);

void cam_close(Cam *cam);

#endif
