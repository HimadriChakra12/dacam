#ifndef DRAW_H
#define DRAW_H

#include <stdint.h>

void draw_fill(uint32_t *px, int stride_px, int x, int y, int w, int h, uint32_t argb);
void draw_rgb24(uint32_t *px, int dst_stride_px, const uint8_t *rgb, int w, int h);
/* text scale=1 -> 5x7px glyphs, 1px gap. scale=2 -> 10x14px, etc. */
void draw_text(uint32_t *px, int stride_px, int x, int y, int scale,
                const char *s, uint32_t argb);
int  text_width(const char *s, int scale);

#endif
