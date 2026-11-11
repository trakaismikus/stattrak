#ifndef GFX_CONFIG_H
#define GFX_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "gfx.h"


// #define GFX_WIDTH             (128)
// #define GFX_HEIGHT            (32)
#define GFX_FRAME_BUF_SIZE(_width, _height)    (((_width) * (_height)) / 8)

#define GFX_PIXEL(ctx, x, y)  (*(ctx->frame_buf + ((y)/8)*ctx->width + (x)))


static inline void Gfx_setPixelColor(gfx_ctx_t* p_ctx, int16_t x, int16_t y, gfx_color_t color) {
  if(color)
    GFX_PIXEL(p_ctx, x, y) |= (1<<(y%8));
  else
    GFX_PIXEL(p_ctx, x, y) &= ~(1<<(y%8));
}

static inline gfx_color_t Gfx_getPixelColor(gfx_ctx_t* p_ctx, int16_t x, int16_t y) {
  return (GFX_PIXEL(p_ctx, x, y) & (1<<(y%8))) != 0;
}

static inline void Gfx_invertPixelColor(gfx_ctx_t* p_ctx, int16_t x, int16_t y) {
  GFX_PIXEL(p_ctx, x, y) ^= (1<<(y%8));
}


#endif /* GFX_CONFIG_H */