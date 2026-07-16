/* Minimal V4L2 mmap-streaming capture. No libv4l, no gstreamer, no opencv.
 * Requests YUYV (near-universal for UVC webcams) and converts to RGB24
 * ourselves so we don't need a JPEG decoder dependency. */

#define _DEFAULT_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>
#include <linux/videodev2.h>

#include "v4l2cap.h"

#define NBUFS 4

static int xioctl(int fd, unsigned long req, void *arg) {
	int r;
	do { r = ioctl(fd, req, arg); } while (r == -1 && errno == EINTR);
	return r;
}

static inline uint8_t clamp8(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

/* YUYV (YUV 4:2:2, 2 bytes/px) -> RGB24 (3 bytes/px), BT.601 */
static void yuyv_to_rgb24(const uint8_t *src, uint8_t *dst, int w, int h) {
	for (int i = 0; i < w * h / 2; i++) {
		int y0 = src[0], u = src[1] - 128, y1 = src[2], v = src[3] - 128;
		src += 4;
		int r = (351 * v) >> 8, g = -((179 * v + 86 * u) >> 8), b = (443 * u) >> 8;
		*dst++ = clamp8(y0 + r); *dst++ = clamp8(y0 + g); *dst++ = clamp8(y0 + b);
		*dst++ = clamp8(y1 + r); *dst++ = clamp8(y1 + g); *dst++ = clamp8(y1 + b);
	}
}

static uint8_t *rgbbuf = NULL;

int cam_open(Cam *cam, const char *device, int width, int height) {
	memset(cam, 0, sizeof(*cam));
	cam->fd = open(device, O_RDWR | O_NONBLOCK);
	if (cam->fd < 0) { perror("open video device"); return -1; }

	struct v4l2_capability cap;
	if (xioctl(cam->fd, VIDIOC_QUERYCAP, &cap) < 0) { perror("QUERYCAP"); goto fail; }
	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		fprintf(stderr, "%s: not a capture device\n", device);
		goto fail;
	}

	struct v4l2_format fmt = {0};
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = width;
	fmt.fmt.pix.height = height;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
	fmt.fmt.pix.field = V4L2_FIELD_NONE;
	if (xioctl(cam->fd, VIDIOC_S_FMT, &fmt) < 0) { perror("S_FMT"); goto fail; }
	/* driver may adjust size/format; trust what it reports back */
	cam->width = fmt.fmt.pix.width;
	cam->height = fmt.fmt.pix.height;
	cam->fourcc = fmt.fmt.pix.pixelformat;
	if (cam->fourcc != V4L2_PIX_FMT_YUYV) {
		fprintf(stderr, "device refused YUYV (got fourcc 0x%x); "
		        "this minimal build only handles YUYV\n", cam->fourcc);
		goto fail;
	}

	struct v4l2_requestbuffers req = {0};
	req.count = NBUFS;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;
	if (xioctl(cam->fd, VIDIOC_REQBUFS, &req) < 0) { perror("REQBUFS"); goto fail; }

	cam->nbufs = req.count;
	cam->bufs = calloc(cam->nbufs, sizeof(void *));
	cam->buflens = calloc(cam->nbufs, sizeof(unsigned));

	for (unsigned i = 0; i < cam->nbufs; i++) {
		struct v4l2_buffer buf = {0};
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;
		if (xioctl(cam->fd, VIDIOC_QUERYBUF, &buf) < 0) { perror("QUERYBUF"); goto fail; }
		cam->bufs[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
		                     MAP_SHARED, cam->fd, buf.m.offset);
		if (cam->bufs[i] == MAP_FAILED) { perror("mmap"); goto fail; }
		cam->buflens[i] = buf.length;
		if (xioctl(cam->fd, VIDIOC_QBUF, &buf) < 0) { perror("QBUF init"); goto fail; }
	}

	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (xioctl(cam->fd, VIDIOC_STREAMON, &type) < 0) { perror("STREAMON"); goto fail; }

	rgbbuf = malloc((size_t)cam->width * cam->height * 3);
	return 0;

fail:
	if (cam->fd >= 0) close(cam->fd);
	return -1;
}

uint8_t *cam_read_frame(Cam *cam) {
	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(cam->fd, &fds);
	struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
	int r = select(cam->fd + 1, &fds, NULL, NULL, &tv);
	if (r <= 0) return NULL; /* timeout or error */

	struct v4l2_buffer buf = {0};
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	if (xioctl(cam->fd, VIDIOC_DQBUF, &buf) < 0) {
		if (errno == EAGAIN) return NULL;
		perror("DQBUF");
		return NULL;
	}

	yuyv_to_rgb24(cam->bufs[buf.index], rgbbuf, cam->width, cam->height);

	if (xioctl(cam->fd, VIDIOC_QBUF, &buf) < 0) perror("QBUF");
	return rgbbuf;
}

void cam_close(Cam *cam) {
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	xioctl(cam->fd, VIDIOC_STREAMOFF, &type);
	for (unsigned i = 0; i < cam->nbufs; i++)
		if (cam->bufs[i]) munmap(cam->bufs[i], cam->buflens[i]);
	free(cam->bufs);
	free(cam->buflens);
	free(rgbbuf);
	rgbbuf = NULL;
	close(cam->fd);
}
