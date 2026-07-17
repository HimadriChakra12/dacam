/* Everything the GUI needs, in ~100 lines: rect fill, RGB24->ARGB8888
 * blit for the camera preview, and a hand-rolled 5x7 bitmap font so we
 * don't need freetype/pango for six button labels and a countdown digit. */

#include <string.h>
#include "draw.h"

void draw_fill(uint32_t *px, int stride_px, int x, int y, int w, int h, uint32_t argb) {
	for (int j = 0; j < h; j++)
		for (int i = 0; i < w; i++)
			px[(y + j) * stride_px + (x + i)] = argb;
}

void draw_rgb24(uint32_t *px, int dst_stride_px, const uint8_t *rgb, int w, int h) {
	for (int y = 0; y < h; y++) {
		const uint8_t *row = rgb + (size_t)y * w * 3;
		uint32_t *drow = px + (size_t)y * dst_stride_px;
		for (int x = 0; x < w; x++) {
			uint8_t r = row[x * 3], g = row[x * 3 + 1], b = row[x * 3 + 2];
			drow[x] = 0xFF000000u | (r << 16) | (g << 8) | b;
		}
	}
}

/* 5-wide x 7-tall glyphs, '#'=on '.'=off. Only the characters suckcam's
 * UI actually uses (labels + digits + a few punctuation). Add more here
 * if you add buttons. */
typedef struct { char c; const char *rows[7]; } Glyph;
static const Glyph font[] = {
	{'S', {".###.","#....","#....",".###.","....#","....#","###.."}},
	{'H', {"#...#","#...#","#...#","#####","#...#","#...#","#...#"}},
	{'O', {".###.","#...#","#...#","#...#","#...#","#...#",".###."}},
	{'T', {"#####","..#..","..#..","..#..","..#..","..#..","..#.."}},
	{'I', {".###.","..#..","..#..","..#..","..#..","..#..",".###."}},
	{'M', {"#...#","##.##","#.#.#","#...#","#...#","#...#","#...#"}},
	{'E', {"#####","#....","#....","####.","#....","#....","#####"}},
	{'R', {"####.","#...#","#...#","####.","#.#..","#..#.","#...#"}},
	{'Q', {".###.","#...#","#...#","#...#","#.#.#","#..#.",".##.#"}},
	{'U', {"#...#","#...#","#...#","#...#","#...#","#...#",".###."}},
	{'0', {".###.","#...#","#..##","#.#.#","##..#","#...#",".###."}},
	{'1', {"..#..",".##..","..#..","..#..","..#..","..#..",".###."}},
	{'2', {".###.","#...#","....#","...#.","..#..",".#...","#####"}},
	{'3', {".###.","#...#","....#","..##.","....#","#...#",".###."}},
	{'4', {"...#.","..##.",".#.#.","#..#.","#####","...#.","...#."}},
	{'5', {"#####","#....","####.","....#","....#","#...#",".###."}},
	{'6', {"..##.",".#...","#....","####.","#...#","#...#",".###."}},
	{'7', {"#####","....#","...#.","..#..",".#...",".#...",".#..."}},
	{'8', {".###.","#...#","#...#",".###.","#...#","#...#",".###."}},
	{'9', {".###.","#...#","#...#",".####","....#","...#.",".##.."}},
	{' ', {".....",".....",".....",".....",".....",".....","....."}},
};
#define NFONT (int)(sizeof(font) / sizeof(font[0]))

static const Glyph *glyph_for(char c) {
	for (int i = 0; i < NFONT; i++) if (font[i].c == c) return &font[i];
	return &font[NFONT - 1]; /* space */
}

int text_width(const char *s, int scale) {
	int n = strlen(s);
	return n ? n * 6 * scale - scale : 0; /* 5px glyph + 1px gap, no trailing gap */
}

void draw_text(uint32_t *px, int stride_px, int x, int y, int scale,
               const char *s, uint32_t argb) {
	for (; *s; s++, x += 6 * scale) {
		const Glyph *g = glyph_for(*s);
		for (int gy = 0; gy < 7; gy++)
			for (int gx = 0; gx < 5; gx++)
				if (g->rows[gy][gx] == '#')
					draw_fill(px, stride_px, x + gx * scale, y + gy * scale, scale, scale, argb);
	}
}
