#ifndef VORTEX_TTX_RENDER_H
#define VORTEX_TTX_RENDER_H

#include "teletext.h"
#include <stdint.h>
#include <stdbool.h>

/* Cell pixel dimensions in the base framebuffer (40 * 16 = 640, 25 * 20 = 500) */
#define TTX_CELL_W 16
#define TTX_CELL_H 20
#define TTX_FB_W   (TTX_COLS * TTX_CELL_W)  /* 640 */
#define TTX_FB_H   (TTX_ROWS * TTX_CELL_H)  /* 500 */

typedef struct {
    uint32_t pixels[TTX_FB_W * TTX_FB_H]; /* 0xAARRGGBB */
    bool     flash_on;
    bool     reveal;
    int      hover_row;
    int      hover_col;
    uint16_t hover_link;
} TtxRenderer;

void ttx_render_init(TtxRenderer *rnd);
void ttx_render_page(TtxRenderer *rnd, const TtxPage *pg);

/* Convert screen coordinates inside canvas (0..canvas_w, 0..canvas_h) to grid cell */
bool ttx_render_hit_test(int x, int y, int canvas_w, int canvas_h, int *out_row, int *out_col);

/* Export rendered ARGB framebuffer as 24-bit Windows/Amiga BMP */
bool ttx_export_bmp(const TtxRenderer *rnd, const char *filepath);

#endif /* VORTEX_TTX_RENDER_H */
