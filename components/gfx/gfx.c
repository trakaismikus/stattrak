/**************************************************************************************************
* @file   gfx.c
* @brief  Source file for graphics module.
* @author Janis Zaharans
* @date   n/a
***************************************************************************************************
*
* gfx_config.h
* ------------
* #define GFX_WIDTH
* #define GFX_HEIGHT
* #define GFX_BUFFER_SIZE
*
* static inline void        Gfx_setPixelColor(gfx_ctx_t* p_ctx, int16_t x, int16_t y, gfx_color_t color);
* static inline gfx_color_t Gfx_getPixelColor(gfx_ctx_t* p_ctx, int16_t x, int16_t y);
* static inline void        Gfx_invertPixelColor(gfx_ctx_t* p_ctx, int16_t x, int16_t y);
*
**************************************************************************************************/

/* > Includes ************************************************************************************/
#include "gfx.h"

#include <string.h>
#include <math.h>

#include "gfx_config.h"

/* > Defines *************************************************************************************/

/* > Enumerations ********************************************************************************/

/* > Type definitions ****************************************************************************/

/* > Local Variables *****************************************************************************/

static gfx_ctx_t* m_ctx_p = NULL;
static gfx_vec_t* m_win_p = NULL;
static gfx_vec_t  m_win;


/* > Local Function Declaration ******************************************************************/
static inline void _drawBitmap(int16_t x, int16_t y, uint16_t width, uint16_t height, const uint8_t* p_bitmap_data, gfx_color_t color, gfx_rotation_t rotation);
static inline void _drawBitmap0(int16_t x, int16_t y, uint16_t width, uint16_t height, const uint8_t* p_bitmap_data, gfx_color_t color);
static inline void _drawBitmap90(int16_t x, int16_t y, uint16_t width, uint16_t height, const uint8_t* p_bitmap_data, gfx_color_t color);
static inline void _drawBitmap180(int16_t x, int16_t y, uint16_t width, uint16_t height, const uint8_t* p_bitmap_data, gfx_color_t color);
static inline void _drawBitmap270(int16_t x, int16_t y, uint16_t width, uint16_t height, const uint8_t* p_bitmap_data, gfx_color_t color);
static inline void _drawBitmapMirrorH(int16_t x, int16_t y, uint16_t width, uint16_t height, const uint8_t* p_bitmap_data, gfx_color_t color);

static void _invertRect(int16_t x0, int16_t y0, int16_t x1, int16_t y1);

static int16_t _getAlignXOffset(const char* str, const gfx_font_t* font, gfx_align_t align, gfx_rotation_t rotation);
static int16_t _getAlignYOffset(const char* str, const gfx_font_t* font, gfx_align_t align, gfx_rotation_t rotation);

/* > Global Function Definition ******************************************************************/


// --- Context
// --- Set active Context
void Gfx_setContext(gfx_ctx_t* ctx) {
  m_ctx_p = ctx;
}

// --- Get active Context
gfx_ctx_t* Gfx_getContext(void) {
  return m_ctx_p;
}

// --- Update Context
void  Gfx_updateContext(void) {
  m_ctx_p->update(m_ctx_p->frame_buf);
}

uint16_t Gfx_getWidth(void) {
  return m_ctx_p->width;
}

uint16_t Gfx_getHeight(void) {
  return m_ctx_p->height;
}


// --- Set clipping window
/*
void Gfx_setWindow(int16_t x, int16_t y, uint16_t width, uint16_t height) {
  static gfx_vec_t win;

  win.x0 = x;
  win.y0 = y;
  win.x1 = x + width;
  win.y1 = y + height;

  m_win_p = &win;
}

void Gfx_setWindowRect(gfx_vec_t* win) {
  m_win_p = win;
}

void Gfx_unsetWindow(void) {
  m_win_p = NULL;
}*/

static uint8_t   s_winDepth = 0;
static gfx_vec_t s_winList[3];
static gfx_vec_t s_win;

void Gfx_setWindow(int16_t x, int16_t y, uint16_t width, uint16_t height) {


  if(s_winDepth == 0) {
    s_win.x0 = x;
    s_win.y0 = y;
    s_win.x1 = x + width;
    s_win.y1 = y + height;

    // s_winList[0] = s_win;
    memcpy(&s_winList[0], &s_win, sizeof(s_win));
    s_winDepth++;
  } else if(s_winDepth < 3) {
    s_winList[s_winDepth].x0 = x;
    s_winList[s_winDepth].y0 = y;
    s_winList[s_winDepth].x1 = x + width;
    s_winList[s_winDepth].y1 = y + height;

    s_win.x0 = (s_win.x0 > s_winList[s_winDepth].x0) ? s_win.x0 : s_winList[s_winDepth].x0;
    s_win.y0 = (s_win.y0 > s_winList[s_winDepth].y0) ? s_win.y0 : s_winList[s_winDepth].y0;
    s_win.x1 = (s_win.x1 < s_winList[s_winDepth].x1) ? s_win.x1 : s_winList[s_winDepth].x1;
    s_win.y1 = (s_win.y1 < s_winList[s_winDepth].y1) ? s_win.y1 : s_winList[s_winDepth].y1;

    s_winDepth++;
  }

  m_win_p = &s_win;
}

void Gfx_setWindowRect(gfx_vec_t* win) {
  m_win_p = win;
}

void Gfx_unsetWindow(void) {
  if(s_winDepth > 1) {
    s_winDepth--;

    // s_win = s_winList[0];
    memcpy(&s_win, &s_winList[0], sizeof(s_win));

    for(uint8_t i=1; i<s_winDepth; i++) {
      s_win.x0 = (s_win.x0 > s_winList[s_winDepth].x0) ? s_win.x0 : s_winList[s_winDepth].x0;
      s_win.y0 = (s_win.y0 > s_winList[s_winDepth].y0) ? s_win.y0 : s_winList[s_winDepth].y0;
      s_win.x1 = (s_win.x1 < s_winList[s_winDepth].x1) ? s_win.x1 : s_winList[s_winDepth].x1;
      s_win.y1 = (s_win.y1 < s_winList[s_winDepth].y1) ? s_win.y1 : s_winList[s_winDepth].y1;
    }
    
  } else {
    s_winDepth = 0;
    m_win_p = NULL;
  }
}



// --- Pixel
// --- Draw Pixel ---
void Gfx_drawPixel(int16_t x, int16_t y, gfx_color_t color) {

  // Apply offset
  x += m_ctx_p->offset.x;
  y += m_ctx_p->offset.y;

  // Check if within window
  if(m_win_p) {
    if(x < (m_win_p->x0 + m_ctx_p->offset.x) || y < (m_win_p->y0 + m_ctx_p->offset.y) || 
       x >= (m_win_p->x1 + m_ctx_p->offset.x) || y >= (m_win_p->y1 + m_ctx_p->offset.y))
      return;
  }

  // Check if within frame
  if(x < 0 || y < 0 || x >= m_ctx_p->width || y >= m_ctx_p->height)
    return;

  // Draw pixel
  Gfx_setPixelColor(m_ctx_p, x, y, color);
}


// --- Invert Pixel ---
void Gfx_invertPixel(int16_t x, int16_t y)
{
  // Apply offset
  x += m_ctx_p->offset.x;
  y += m_ctx_p->offset.y;

  // Check if within frame
  if(x < 0 || y < 0 || x >= m_ctx_p->width || y >= m_ctx_p->height)
    return;

  // Check if within window
  if(m_win_p) {
    if(x < (m_win_p->x0 + m_ctx_p->offset.x) || y < (m_win_p->y0 + m_ctx_p->offset.y) || 
       x >= (m_win_p->x1 + m_ctx_p->offset.x) || y >= (m_win_p->y1 + m_ctx_p->offset.y))
      return;
  }

  // Invert pixel
  Gfx_invertPixelColor(m_ctx_p, x, y);
}


uint8_t Gfx_getPixel(int16_t x, int16_t y)
{
  // Apply offset
  x += m_ctx_p->offset.x;
  y += m_ctx_p->offset.y;

  return Gfx_getPixelColor(m_ctx_p, x, y);
}



// --- Line
// Bresenham's algorithm
void Gfx_drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, gfx_color_t color) {
  int16_t steep = GFX_ABS((int16_t)(y1 - y0)) > GFX_ABS((int16_t)(x1 - x0));
  if (steep) {
    swap_int16_t(x0, y0);
    swap_int16_t(x1, y1);
  }

  if (x0 > x1) {
    swap_int16_t(x0, x1);
    swap_int16_t(y0, y1);
  }

  int16_t dx, dy;
  dx = x1 - x0;
  dy = GFX_ABS(y1 - y0);

  int16_t err = dx / 2;
  int16_t ystep;

  if (y0 < y1) {
    ystep = 1;
  } else {
    ystep = -1;
  }

  for (; x0<=x1; x0++) {
    if (steep) {
      Gfx_drawPixel(y0, x0, color);
    } else {
      Gfx_drawPixel(x0, y0, color);
    }
    err -= dy;
    if (err < 0) {
      y0 += ystep;
      err += dx;
    }
  }
}

// --- Draw Dashed Line ---
void Gfx_drawLineDashed(int16_t x0, int16_t y0, int16_t x1, int16_t y1, gfx_color_t color, uint8_t dash) {
  uint8_t cnt = 0;

  int16_t steep = GFX_ABS((int8_t)(y1 - y0)) > GFX_ABS((int8_t)(x1 - x0));
  if (steep) {
    swap_int16_t(x0, y0);
    swap_int16_t(x1, y1);
  }

  if (x0 > x1) {
    swap_int16_t(x0, x1);
    swap_int16_t(y0, y1);
  }

  int16_t dx, dy;
  dx = x1 - x0;
  dy = GFX_ABS(y1 - y0);

  int16_t err = dx / 2;
  int16_t ystep;

  if (y0 < y1) {
    ystep = 1;
  } else {
    ystep = -1;
  }

  for (; x0<=x1; x0++) {
    if(cnt < (dash/2)) {
      if (steep) {
        Gfx_drawPixel(y0, x0, color);
      } else {
        Gfx_drawPixel(x0, y0, color);
      }
    } else {
      if (steep) {
        Gfx_drawPixel(y0, x0, color ? 0 : color);
      } else {
        Gfx_drawPixel(x0, y0, color ? 0 : color);
      }
    }

    cnt++;
    if(cnt == dash)
      cnt = 0;

    err -= dy;
    if (err < 0) {
      y0 += ystep;
      err += dx;
    }
  }
}


// --- Draw Dotted Line ---
void Gfx_drawLineDotted(int16_t x0, int16_t y0, int16_t x1, int16_t y1, gfx_color_t color, uint8_t dist) {
  uint8_t cnt = 0;

  int16_t steep = GFX_ABS((int8_t)(y1 - y0)) > GFX_ABS((int8_t)(x1 - x0));
  if (steep) {
    swap_int16_t(x0, y0);
    swap_int16_t(x1, y1);
  }

  if (x0 > x1) {
    swap_int16_t(x0, x1);
    swap_int16_t(y0, y1);
  }

  int16_t dx, dy;
  dx = x1 - x0;
  dy = GFX_ABS(y1 - y0);

  int16_t err = dx / 2;
  int16_t ystep;

  if (y0 < y1) {
    ystep = 1;
  } else {
    ystep = -1;
  }

  for (; x0<=x1; x0++) {
    if(cnt == 0) {
      if (steep) {
        Gfx_drawPixel(y0, x0, color);
      } else {
        Gfx_drawPixel(x0, y0, color);
      }
    } else {
      if (steep) {
        Gfx_drawPixel(y0, x0, color ? 0 : color);
      } else {
        Gfx_drawPixel(x0, y0, color ? 0 : color);
      }
    }

    cnt++;
    if(cnt >= dist)
      cnt = 0;

    err -= dy;
    if (err < 0) {
      y0 += ystep;
      err += dx;
    }
  }
}


// --- Draw Horizontal Line ---
void Gfx_drawHLine(int16_t x, int16_t y, int16_t width, gfx_color_t color) {
  Gfx_drawLine(x, y, x+width-1, y, color);
}

// --- Draw Vertical Line ---
void Gfx_drawVLine(int16_t x, int16_t y, int16_t height, gfx_color_t color) {
  Gfx_drawLine(x, y, x, y+height-1, color);
}

// --- Rectangle
// --- Draw Rectangle ---
void Gfx_drawRect(int16_t x, int16_t y, int16_t width, int16_t height, gfx_color_t color) {

  // Width Pixel correction
  if(width < 0) width++;
  else if(width > 0) width--;

  // Height Pixel correction
  if(height < 0) height++;
  else if(height > 0) height--;

  Gfx_drawHLine(x, y, width, color);
  Gfx_drawHLine(x, y+height-1, width, color);
  Gfx_drawVLine(x, y, height, color);
  Gfx_drawVLine(x+width-1, y, height, color);
}

// --- Fill Rectangle ---
void Gfx_fillRect(int16_t x, int16_t y, int16_t width, int16_t height, gfx_color_t color) {
  int16_t ix, iy;
  int16_t xs, xe;
  int16_t ys, ye;

  if(width >= 0) {
    xs = x;
    xe = x + width;
  } else {
    xs = x + width;
    xe = x;
  }

  if(height >= 0) {
    ys = y;
    ye = y + height;
  } else {
    ys = y + height + 1;
    ye = y + 1;
  }

  for(ix = xs; ix < xe; ix++) {
    for(iy = ys; iy < ye; iy++)
      Gfx_drawPixel(ix, iy, color);
  }
}

// --- Invert Rectangle ---
void Gfx_invertRect(int16_t x, int16_t y, int16_t width, int16_t height) {
  int16_t ix, iy;
  int16_t xs, xe;
  int16_t ys, ye;

  if(width >= 0) {
    xs = x;
    xe = x + width;
  } else {
    xs = x + width;
    xe = x;
  }

  if(height >= 0) {
    ys = y;
    ye = y + height;
  } else {
    ys = y + height + 1;
    ye = y + 1;
  }

  for(ix = xs; ix < xe; ix++) {
    for(iy = ys; iy < ye; iy++)
      Gfx_invertPixel(ix, iy);
  }
}

// --- Dither Rectangle ---
void Gfx_ditherRect(int16_t x, int16_t y, int16_t width, int16_t height, gfx_color_t color) {
  int16_t ix, iy;
  int16_t xs, xe;
  int16_t ys, ye;

  if(width >= 0) {
    xs = x;
    xe = x + width;
  } else {
    xs = x + width;
    xe = x;
  }

  if(height >= 0) {
    ys = y;
    ye = y + height;
  } else {
    ys = y + height + 1;
    ye = y + 1;
  }

  for(ix=xs; ix<xe; ix++) {
    for(iy=ys; iy<ye; iy++) {
      Gfx_drawPixel(ix, iy, ((ix ^ iy) & 1) ? color : 0);
    }
  }
}

// --- Draw Rounded Rectangle ---
void Gfx_drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, gfx_color_t color) {
  Gfx_drawHLine(x+r  , y    , w-2*r, color); // Top
  Gfx_drawHLine(x+r  , y+h-1, w-2*r, color); // Bottom
  Gfx_drawVLine(x    , y+r  , h-2*r, color); // Left
  Gfx_drawVLine(x+w-1, y+r  , h-2*r, color); // Right

    // draw four corners
  Gfx_drawCircleHelper(x+r    , y+r    , r, 1, color);
  Gfx_drawCircleHelper(x+w-r-1, y+r    , r, 2, color);
  Gfx_drawCircleHelper(x+w-r-1, y+h-r-1, r, 4, color);
  Gfx_drawCircleHelper(x+r    , y+h-r-1, r, 8, color);
}

// --- Fill Rounded Rectangle ---
void Gfx_fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, gfx_color_t color) {
  Gfx_fillRect(x+r, y, w-2*r, h, color);

  Gfx_fillCircleHelper(x+w-r-1, y+r, r, 1, h-2*r-1, color);
  Gfx_fillCircleHelper(x+r    , y+r, r, 2, h-2*r-1, color);
}

// --- Circle
// --- Draw Circle ---
void Gfx_drawCircle(int16_t x0, int16_t y0, uint8_t radius, gfx_color_t color) {
  int16_t f = 1 - radius;

  int16_t ddF_x = 1;
  int16_t ddF_y = -2 * radius;
  int16_t x = 0;
  int16_t y = radius;

  Gfx_drawPixel(x0  , y0+radius, color);
  Gfx_drawPixel(x0  , y0-radius, color);
  Gfx_drawPixel(x0+radius, y0  , color);
  Gfx_drawPixel(x0-radius, y0  , color);

  while (x<y) {
    if (f >= 0) {
      y--;
      ddF_y += 2;
      f += ddF_y;
    }

    x++;
    ddF_x += 2;
    f += ddF_x;

    Gfx_drawPixel(x0 + x, y0 + y, color);
    Gfx_drawPixel(x0 - x, y0 + y, color);
    Gfx_drawPixel(x0 + x, y0 - y, color);
    Gfx_drawPixel(x0 - x, y0 - y, color);
    Gfx_drawPixel(x0 + y, y0 + x, color);
    Gfx_drawPixel(x0 - y, y0 + x, color);
    Gfx_drawPixel(x0 + y, y0 - x, color);
    Gfx_drawPixel(x0 - y, y0 - x, color);
  }
}

// --- Fill Circle ---
void Gfx_fillCircle(int16_t x0, int16_t y0, int16_t r, gfx_color_t color) {
  Gfx_drawVLine(x0, y0-r, 2*r+1, color);
  Gfx_fillCircleHelper(x0, y0, r, 3, 0, color);
}

// --- Helper functions to draw round corners ---
void Gfx_drawCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t cornername, gfx_color_t color) {
  int16_t f     = 1 - r;
  int16_t ddF_x = 1;
  int16_t ddF_y = -2 * r;
  int16_t x     = 0;
  int16_t y     = r;

  while (x<y) {
    if (f >= 0) {
      y--;
      ddF_y += 2;
      f     += ddF_y;
    }
    x++;
    ddF_x += 2;
    f     += ddF_x;
    if (cornername & 0x4) {
      Gfx_drawPixel(x0 + x, y0 + y, color);
      Gfx_drawPixel(x0 + y, y0 + x, color);
    }
    if (cornername & 0x2) {
      Gfx_drawPixel(x0 + x, y0 - y, color);
      Gfx_drawPixel(x0 + y, y0 - x, color);
    }
    if (cornername & 0x8) {
      Gfx_drawPixel(x0 - y, y0 + x, color);
      Gfx_drawPixel(x0 - x, y0 + y, color);
    }
    if (cornername & 0x1) {
      Gfx_drawPixel(x0 - y, y0 - x, color);
      Gfx_drawPixel(x0 - x, y0 - y, color);
    }
  }
}

// --- Helper functions to fill round corners ---
void Gfx_fillCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t cornername, int16_t delta, gfx_color_t color) {
  int16_t f     = 1 - r;
  int16_t ddF_x = 1;
  int16_t ddF_y = -2 * r;
  int16_t x     = 0;
  int16_t y     = r;

  while (x<y) {
    if (f >= 0) {
      y--;
      ddF_y += 2;
      f     += ddF_y;
    }
    x++;
    ddF_x += 2;
    f     += ddF_x;

    if (cornername & 0x1) {
      Gfx_drawVLine(x0+x, y0-y, 2*y+1+delta, color);
      Gfx_drawVLine(x0+y, y0-x, 2*x+1+delta, color);
    }
    if (cornername & 0x2) {
      Gfx_drawVLine(x0-x, y0-y, 2*y+1+delta, color);
      Gfx_drawVLine(x0-y, y0-x, 2*x+1+delta, color);
    }
  }
}


// --- Helper function to draw part of ellipse
void Gfx_drawEllipseHelper(int16_t x, int16_t y, uint16_t width, uint16_t height, uint8_t corner, gfx_color_t color) {
  // Draw ellipse using Bresenham's algorithm (code generated by CoPilot)
  int16_t a = width / 2;
  int16_t b = height / 2;
  int16_t wx = 0;
  int16_t wy = b;
  int32_t a2 = a * a;
  int32_t b2 = b * b;
  int32_t crit1 = -(a2 / 4 + a % 2 + b2);
  int32_t crit2 = -(b2 / 4 + b % 2 + a2);
  int32_t crit3 = -(b2 / 4 + b % 2);
  int32_t t = -a2 * wy;
  int32_t dxt = 2 * b2 * wx;
  int32_t dyt = -2 * a2 * wy;
  int32_t d2xt = 2 * b2;
  int32_t d2yt = 2 * a2;

  while (wy >= 0 && wx <= a) {
    if (corner & 0x1)
      Gfx_drawPixel(x + wx, y + wy, color);
    if (corner & 0x2)
      Gfx_drawPixel(x - wx, y + wy, color);
    if (corner & 0x4)
      Gfx_drawPixel(x + wx, y - wy, color);
    if (corner & 0x8)
      Gfx_drawPixel(x - wx, y - wy, color);
    
    if (t + b2 * wx <= crit1 || t + a2 * wy <= crit3) {
      wx++;
      dxt += d2xt;
      t += dxt;
    } else if (t - a2 * wy > crit2) {
      wy--;
      dyt += d2yt;
      t += dyt;
    } else {
      wx++;
      dxt += d2xt;
      t += dxt;
      wy--;
      dyt += d2yt;
      t += dyt;
    }
  }
}

// --- Draw Ellipse ---
void Gfx_drawEllipse(int16_t x, int16_t y, uint16_t width, uint16_t height, gfx_color_t color) {
  // Draw ellipse using Bresenham's algorithm (code generated by CoPilot)
  int16_t a = width / 2;
  int16_t b = height / 2;
  int16_t wx = 0;
  int16_t wy = b;
  int32_t a2 = a * a;
  int32_t b2 = b * b;
  int32_t crit1 = -(a2 / 4 + a % 2 + b2);
  int32_t crit2 = -(b2 / 4 + b % 2 + a2);
  int32_t crit3 = -(b2 / 4 + b % 2);
  int32_t t = -a2 * wy;
  int32_t dxt = 2 * b2 * wx;
  int32_t dyt = -2 * a2 * wy;
  int32_t d2xt = 2 * b2;
  int32_t d2yt = 2 * a2;

  while (wy >= 0 && wx <= a) {
    Gfx_drawPixel(x + wx, y + wy, color);
    if (wx != 0 || wy != 0) {
      Gfx_drawPixel(x - wx, y + wy, color);
      Gfx_drawPixel(x + wx, y - wy, color);
      Gfx_drawPixel(x - wx, y - wy, color);
    }
    if (t + b2 * wx <= crit1 || t + a2 * wy <= crit3) {
      wx++;
      dxt += d2xt;
      t += dxt;
    } else if (t - a2 * wy > crit2) {
      wy--;
      dyt += d2yt;
      t += dyt;
    } else {
      wx++;
      dxt += d2xt;
      t += dxt;
      wy--;
      dyt += d2yt;
      t += dyt;
    }
  }
}

// --- Fill Ellipse ---
void Gfx_fillEllipse(int16_t x, int16_t y, uint16_t width, uint16_t height, gfx_color_t color) {
  // Draw ellipse using Bresenham's algorithm (code generated by CoPilot)
  int16_t a = width / 2;
  int16_t b = height / 2;
  int16_t wx = 0;
  int16_t wy = b;
  int32_t a2 = a * a;
  int32_t b2 = b * b;
  int32_t crit1 = -(a2 / 4 + a % 2 + b2);
  int32_t crit2 = -(b2 / 4 + b % 2 + a2);
  int32_t crit3 = -(b2 / 4 + b % 2);
  int32_t t = -a2 * wy;
  int32_t dxt = 2 * b2 * wx;
  int32_t dyt = -2 * a2 * wy;
  int32_t d2xt = 2 * b2;
  int32_t d2yt = 2 * a2;

  while (wy >= 0 && wx <= a) {
    Gfx_drawHLine(x - wx, y - wy, 2 * wx, color);
    Gfx_drawHLine(x - wx, y + wy, 2 * wx, color);
    if (t + b2 * wx <= crit1 || t + a2 * wy <= crit3) {
      wx++;
      dxt += d2xt;
      t += dxt;
    } else if (t - a2 * wy > crit2) {
      wy--;
      dyt += d2yt;
      t += dyt;
    } else {
      wx++;
      dxt += d2xt;
      t += dxt;
      wy--;
      dyt += d2yt;
      t += dyt;
    }
  }
}


// --- Fill Triangle ---
void Gfx_fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, gfx_color_t color) {
  int16_t a, b, y, last;

  // Sort coordinates by Y order (y2 >= y1 >= y0)
  if (y0 > y1) {
    swap_int16_t(y0, y1); swap_int16_t(x0, x1);
  }
  if (y1 > y2) {
    swap_int16_t(y2, y1); swap_int16_t(x2, x1);
  }
  if (y0 > y1) {
    swap_int16_t(y0, y1); swap_int16_t(x0, x1);
  }

  if(y0 == y2) { // Handle awkward all-on-same-line case as its own thing
    a = b = x0;
    if(x1 < a)      a = x1;
    else if(x1 > b) b = x1;
    if(x2 < a)      a = x2;
    else if(x2 > b) b = x2;
    Gfx_drawHLine(a, y0, b-a+1, color);
    return;
  }

  int16_t
  dx01 = x1 - x0,
  dy01 = y1 - y0,
  dx02 = x2 - x0,
  dy02 = y2 - y0,
  dx12 = x2 - x1,
  dy12 = y2 - y1;
  int32_t
  sa   = 0,
  sb   = 0;

  // For upper part of triangle, find scanline crossings for segments
  // 0-1 and 0-2.  If y1=y2 (flat-bottomed triangle), the scanline y1
  // is included here (and second loop will be skipped, avoiding a /0
  // error there), otherwise scanline y1 is skipped here and handled
  // in the second loop...which also avoids a /0 error here if y0=y1
  // (flat-topped triangle).
  if(y1 == y2) last = y1;   // Include y1 scanline
  else         last = y1-1; // Skip it

  for(y=y0; y<=last; y++) {
    a   = x0 + sa / dy01;
    b   = x0 + sb / dy02;
    sa += dx01;
    sb += dx02;
    /* longhand:
    a = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
    b = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
    */
    if(a > b) swap_int16_t(a,b);
    Gfx_drawHLine(a, y, b-a+1, color);
  }

  // For lower part of triangle, find scanline crossings for segments
  // 0-2 and 1-2.  This loop is skipped if y1=y2.
  sa = dx12 * (y - y1);
  sb = dx02 * (y - y0);
  for(; y<=y2; y++) {
    a   = x1 + sa / dy12;
    b   = x0 + sb / dy02;
    sa += dx12;
    sb += dx02;
    /* longhand:
    a = x1 + (x2 - x1) * (y - y1) / (y2 - y1);
    b = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
    */
    if(a > b) swap_int16_t(a,b);
    Gfx_drawHLine(a, y, b-a+1, color);
  }
}


// --- Draw Arc ---
void Gfx_drawArc(float x, float y, float rx, float ry, int16_t w,
		int16_t startAngle, int16_t endAngle, gfx_color_t color)
{
	uint16_t seg = 3; // Segments are 3 degrees wide = 120 segments for 360 degrees
	uint16_t inc = 3; // Draw segments every 3 degrees, increase to 6 for segmented ring
  uint16_t seg_count = (endAngle - startAngle) / seg;

  const float kCoef = 2.0f * 3.1415f / 360.0f;

	// Calculate first pair of coordinates for segment start
	float sx = cosf((startAngle - 90) * kCoef);
	float sy = sinf((startAngle - 90) * kCoef);
	float x0 = sx * (rx - w) + x;
	float y0 = sy * (ry - w) + y;
	float x1 = sx * rx + x;
	float y1 = sy * ry + y;

	// Draw colour blocks every inc degrees
	for (int i = startAngle; i < startAngle + seg * seg_count; i += inc) {

		// Calculate pair of coordinates for segment end
		float sx2 = cosf((i + seg - 90) * kCoef);
		float sy2 = sinf((i + seg - 90) * kCoef);
		float x2 = sx2 * (rx - w) + x;
		float y2 = sy2 * (ry - w) + y;
		float x3 = sx2 * rx + x;
		float y3 = sy2 * ry + y;

		Gfx_fillTriangle(x0, y0, x1, y1, x2, y2, color);
		Gfx_fillTriangle(x1, y1, x2, y2, x3, y3, color);

		// Copy segment end to sgement start for next segment
		x0 = x2;
		y0 = y2;
		x1 = x3;
		y1 = y3;
	}
}

// --- Bitmap
void Gfx_drawBitmap(int16_t x, int16_t y, const gfx_bitmap_t* p_bitmap, gfx_color_t color, gfx_rotation_t rotation) {
  _drawBitmap(x, y, p_bitmap->width, p_bitmap->height, p_bitmap->bitmap, color, rotation);
}

void Gfx_drawBitmapRaw(int16_t x, int16_t y, const uint8_t* p_bitmap, uint8_t width, uint8_t height, gfx_color_t color, gfx_rotation_t rotation) {
  _drawBitmap(x, y, width, height, p_bitmap, color, rotation);
}


void Gfx_drawBitmapArray(int16_t x, int16_t y, const gfx_bitmap_t* p_bitmap, uint8_t idx, gfx_color_t color, gfx_rotation_t rotation) {
  if(idx >= p_bitmap->count)
    idx = p_bitmap->count - 1;

  const uint8_t* p_bitmap_data = p_bitmap->bitmap + (p_bitmap->size * idx);

  _drawBitmap(x, y, p_bitmap->width, p_bitmap->height, p_bitmap_data, color, rotation);
}

// --- Text
// --- Draw Character ---
void Gfx_drawChar(int16_t x, int16_t y, char c, const gfx_font_t *font, gfx_color_t color, uint8_t invert, gfx_rotation_t rotation) {
  uint8_t idx = font->char_to_idx(c);

#if defined(GFX_SUPPORT_GLYPH_FONT)
  const uint8_t* p_font_data;
  uint8_t width;

  if(font->glyphs == NULL) {
    p_font_data = font->data + (idx * font->size);
    width = font->width;
  } else {
    p_font_data = font->data + font->glyphs[idx].offset;
    width = font->glyphs[idx].width;
  }

  _drawBitmap(x, y, width, font->height, p_font_data, color, rotation);

  if(invert)
    Gfx_invertRect(x - 1, y - 1, width + 2, font->height + 2);
#else
  const uint8_t* p_font_data = font->data + (idx * font->size);

  _drawBitmap(x, y, font->width, font->height, p_font_data, color, rotation);

  if(invert)
    Gfx_invertRect(x - 1, y - 1, font->width + 2, font->height + 2);
#endif
}


// --- Draw Character Scaled ---
void Gfx_drawCharScaled(int16_t x, int16_t y, char c, const gfx_font_t *font, gfx_color_t color, uint8_t scale, uint8_t gap) {
  uint8_t idx = font->char_to_idx(c);
  const uint8_t* p_font_data;
  uint16_t width;
  int16_t px, py;

#if defined(GFX_SUPPORT_GLYPH_FONT)
  if(font->glyphs == NULL) {
    p_font_data = font->data + idx*font->size;
    width = font->width;
  } else {
    p_font_data = font->data + font->glyphs[idx].offset;
    width = font->glyphs[idx].width;
  }

#else
  p_font_data = font->data + idx*font->size;
  width = font->width;
#endif

  for(px=0; px<width; px++) {
    for(py=0; py<font->height; py++) {
      uint8_t iy = py / 8;
      uint8_t by = py % 8;

      if((*(p_font_data + iy*width + px)) & (1<<by)) {
        if(gap)
          Gfx_fillRect(x + px*scale, y + py*scale, scale-1, scale-1, color);
        else
          Gfx_fillRect(x + px*scale, y + py*scale, scale, scale, color);
      }
    }
  }  
}


// --- Draw String Scaled ---
void Gfx_drawStringScaled(int16_t x, int16_t y, const char* str, const gfx_font_t *font, gfx_color_t color, uint8_t scale, uint8_t gap) {
  int16_t xOffset = 0;
  const uint32_t strLength = strlen(str);
  
  for(int i=0; i<strLength; i++) {
    const char c = str[i];
    Gfx_drawCharScaled(x + xOffset, y, c, font, color, scale, gap);

#if defined(GFX_SUPPORT_GLYPH_FONT)
    if(font->glyphs == NULL) {
      xOffset += font->x_advance * scale;
    } else {
      const uint8_t idx = font->char_to_idx(c);
      xOffset += (font->glyphs[idx].width + font->space) * scale;
    }
#else
    xOffset += font->x_advance * scale;
#endif
  }
}


// --- Draw String Scaled Aligned ---
void Gfx_drawStringScaledAligned(int16_t x, int16_t y, const char* str, const gfx_font_t *font, gfx_color_t color, uint8_t scale, uint8_t gap, gfx_align_t align) {
  const int16_t xOffset = _getAlignXOffset(str, font, align, GFX_ROTATION__0) * scale;
  const int16_t yOffset = _getAlignYOffset(str, font, align, GFX_ROTATION__0) * scale;

  Gfx_drawStringScaled(x + xOffset, y + yOffset, str, font, color, scale, gap);
}


// --- Draw Windowed Character ---
void Gfx_drawCharWin(gfx_rect_t* win, int16_t x, int16_t y, char c, const gfx_font_t *font, gfx_color_t color, uint8_t invert, gfx_rotation_t rotation) {
  m_win.x0 = win->x;
  m_win.y0 = win->y;
  m_win.x1 = win->x + win->width - 1;
  m_win.y1 = win->y + win->height - 1;
  Gfx_setWindowRect(&m_win);
  Gfx_drawChar(x, y, c, font, color, invert, rotation);
  Gfx_unsetWindow();
}


// --- Draw String ---
void Gfx_drawString(int16_t x, int16_t y, const char* str, const gfx_font_t *font, gfx_color_t color, uint8_t invert, gfx_rotation_t rotation) {
  uint16_t charWidth;
  uint16_t x_advance;
  uint16_t firstCharWidth;

#if defined(GFX_SUPPORT_GLYPH_FONT)
  if(font->glyphs) {
    uint8_t idx = font->char_to_idx(*str);
    charWidth = font->glyphs[idx].width;
    x_advance = charWidth + font->space;
  } else {
    charWidth = font->width;
    x_advance = font->x_advance;
  }
#else
  charWidth = font->width;
  x_advance = font->x_advance;
#endif

  firstCharWidth = charWidth;

  switch(rotation) {
    case GFX_ROTATION__90:  x -= charWidth - 2; break;
    case GFX_ROTATION__180: x -= charWidth - 1; y -= font->height - 1; break;
    case GFX_ROTATION__270: y -= charWidth - 1; break;
    default: break;
  }

  int16_t x_origin = x;
  int16_t y_origin = y;
  uint16_t width = 0, height = font->height + 1;

#if defined(GFX_SUPPORT_GLYPH_FONT)
  uint16_t max_width = charWidth;
#endif

  while(*str) {
    if(*str == '\n') {
      x = x_origin;
      y += font->y_advance;
      str++;
      height += font->y_advance;
    } else {
      Gfx_drawChar(x, y, *str++, font, color, 0, rotation);
      width += x_advance;

#if defined(GFX_SUPPORT_GLYPH_FONT)
      switch(rotation) {
        case GFX_ROTATION__0:   x += x_advance; break;
        case GFX_ROTATION__90:  y += x_advance; break;
        default: break;
      } 

      if(font->glyphs) {
        // Update x_advance
        uint8_t idx = font->char_to_idx(*str);
        charWidth = font->glyphs[idx].width;
        x_advance = charWidth + font->space;

        if(charWidth > max_width)
          max_width = charWidth;
      } 
        
      switch(rotation) {
        case GFX_ROTATION__180: x -= x_advance; break;
        case GFX_ROTATION__270: y -= x_advance; break;
        default: break;
      }
#else
      switch(rotation) {
        case GFX_ROTATION__0:   x += x_advance; break;
        case GFX_ROTATION__90:  y += x_advance; break;
        case GFX_ROTATION__180: x -= x_advance; break;
        case GFX_ROTATION__270: y -= x_advance; break;
        default: break;
      }
#endif
    }
  }

#if defined(GFX_SUPPORT_GLYPH_FONT)
  uint16_t space;
  if(font->glyphs) {
    space = font->space;
  } else {
    space = font->x_advance - font->width;
  }

  switch(rotation) {
    case GFX_ROTATION__0:   x -= space; break;
    case GFX_ROTATION__90:  y -= space; break;
    case GFX_ROTATION__180: x += x_advance; break;
    case GFX_ROTATION__270: y += x_advance; break;
    default: break;
  }
#else
  uint16_t space = font->x_advance - font->width;
  switch(rotation) {
    case GFX_ROTATION__0:   x -= space; break;
    case GFX_ROTATION__90:  y -= space; break;
    case GFX_ROTATION__180: x += x_advance; break;
    case GFX_ROTATION__270: y += x_advance; break;
    default: break;
  }
#endif



  if(invert) {
    switch(rotation) {
      case GFX_ROTATION__0:   _invertRect(x_origin - 1, y_origin - 1, x + 1 , y_origin + font->height + 1); break;
      case GFX_ROTATION__90:  _invertRect(x_origin - 1, y_origin - 1, x_origin + font->height + 1, y + 1); break;
      case GFX_ROTATION__180: _invertRect(x_origin + firstCharWidth + 1, y_origin - 1, x - 1, y_origin + font->height + 1); break;
      case GFX_ROTATION__270: _invertRect(x_origin - 1, y_origin + firstCharWidth + 1, x_origin + font->height + 1, y - 1); break;
      default: break;
    }
  }
}

// --- Draw Windowed String ---
void Gfx_drawStringWin(gfx_rect_t* win, int16_t x, int16_t y, char* str, const gfx_font_t *font, gfx_color_t color, uint8_t invert, gfx_rotation_t rotation) {
  m_win.x0 = win->x;
  m_win.y0 = win->y;
  m_win.x1 = win->x + win->width - 1;
  m_win.y1 = win->y + win->height - 1;
  Gfx_setWindowRect(&m_win);
  Gfx_drawString(x, y, str, font, color, invert, rotation);
  Gfx_unsetWindow();
}


#if defined(GFX_SUPPORT_GLYPH_FONT)
uint32_t Gfx_getStringWidth(const char* str, const gfx_font_t *font) {
  uint32_t width = 0;

  if(font->glyphs) {
    while(*str) {
      uint8_t idx = font->char_to_idx(*str++);
      width += font->glyphs[idx].width + font->space;
    }
    width -= font->space;
  }
  else {
    width = strlen(str) * font->x_advance - (font->x_advance - font->width);
  }

  return width;
}
#endif



// --- Draw Aligned String ---
void Gfx_drawStringAligned(int16_t x, int16_t y, const char* str, const gfx_font_t *font, gfx_color_t color, uint8_t invert, gfx_align_t align, gfx_rotation_t rotation) {

  // Horizontal align
  switch(align & 0x0F) {
    case GFX_ALIGN__LEFT: break;

    case GFX_ALIGN__CENTER: {
#if defined(GFX_SUPPORT_GLYPH_FONT)
      int16_t offset = Gfx_getStringWidth(str, font) / 2;
#else
      int16_t offset = strlen(str) * font->x_advance / 2 - (font->x_advance - font->width);
#endif
      switch(rotation) {
        case GFX_ROTATION__0:   x -= offset; break;
        case GFX_ROTATION__90:  y -= offset; break;
        case GFX_ROTATION__180: x += offset; break;
        case GFX_ROTATION__270: y += offset; break;
        default: break;
      }
    } break;

    case GFX_ALIGN__RIGHT: {
#if defined(GFX_SUPPORT_GLYPH_FONT)
      int16_t offset = Gfx_getStringWidth(str, font);
#else
      int16_t offset = strlen(str) * font->x_advance;
#endif
      switch(rotation) {
        case GFX_ROTATION__0:   x -= offset; break;
        case GFX_ROTATION__90:  y -= offset; break;
        case GFX_ROTATION__180: x += offset; break;
        case GFX_ROTATION__270: y += offset; break;
        default: break;
      }
    } break;

    default: break;
  }

  // Vertical align
  switch(align & 0xF0) {
    case GFX_ALIGN__TOP: break;

    case GFX_ALIGN__MIDDLE: {
      int16_t offset = font->height/2;
      switch(rotation) {
        case GFX_ROTATION__0:   y -= offset; break;
        case GFX_ROTATION__90:  x += offset; break;
        case GFX_ROTATION__180: y += offset; break;
        case GFX_ROTATION__270: x -= offset; break;
        default: break;
      }
    } break;

    case GFX_ALIGN__BOTTOM: {
      int16_t offset = font->height/2;
      switch(rotation) {
        case GFX_ROTATION__0:   y -= offset; break;
        case GFX_ROTATION__90:  x += offset; break;
        case GFX_ROTATION__180: y += offset; break;
        case GFX_ROTATION__270: x -= offset; break;
        default: break;
      }
    } break;

    default: break;
  }

  Gfx_drawString(x, y, str, font, color, invert, rotation);
}


// --- Clear
void Gfx_clearAll(gfx_color_t color) {
  memset(m_ctx_p->frame_buf, color, GFX_FRAME_BUF_SIZE(m_ctx_p->width, m_ctx_p->height) * sizeof(color));
}

void Gfx_copyBuffer(uint8_t* buf, uint16_t offset, uint16_t size)
{
  memcpy(m_ctx_p->frame_buf + offset, buf, size);
}

/* > Local Function Definition *******************************************************************/

static inline void _drawBitmap(int16_t x, int16_t y, uint16_t width, uint16_t height, const uint8_t* p_bitmap_data, gfx_color_t color, gfx_rotation_t rotation) {
  switch(rotation) {
    case GFX_ROTATION__0:   _drawBitmap0(x, y, width, height, p_bitmap_data, color);   break;
    case GFX_ROTATION__90:  _drawBitmap90(x, y, width, height, p_bitmap_data, color);  break;
    case GFX_ROTATION__180: _drawBitmap180(x, y, width, height, p_bitmap_data, color); break;
    case GFX_ROTATION__270: _drawBitmap270(x, y, width, height, p_bitmap_data, color); break;
    case GFX_ROTATION__H_MIRROR: _drawBitmapMirrorH(x, y, width, height, p_bitmap_data, color); break;
    default: break;
  }
}

static inline void _drawBitmap0(int16_t x, int16_t y, uint16_t width, uint16_t height, const uint8_t* p_bitmap_data, gfx_color_t color) {
  int16_t px, py;

  for(px=0; px<width; px++) {
    for(py=0; py<height; py++) {
      uint8_t iy = py / 8;
      uint8_t by = py % 8;

      if((*(p_bitmap_data + iy*width + px)) & (1<<by))
        Gfx_drawPixel(x + px, y + py, color);
    }
  }
}

static inline void _drawBitmap90(int16_t x, int16_t y, uint16_t width, uint16_t height, const uint8_t* p_bitmap_data, gfx_color_t color) {
  int16_t px, py;

  for(px=0; px<height; px++) {
    for(py=0; py<width; py++) {
      uint8_t ix = px / 8;
      uint8_t bx = px % 8;

      if((*(p_bitmap_data + ix*width + py)) & (1<<bx))
        Gfx_drawPixel(x + height - px - 1, y + py, color);
    }
  }
}

static inline void _drawBitmap180(int16_t x, int16_t y, uint16_t width, uint16_t height, const uint8_t* p_bitmap_data, gfx_color_t color) {
  int16_t px, py;

  for(px=0; px<width; px++) {
    for(py=0; py<height; py++) {
      uint8_t iy = py / 8;
      uint8_t by = py % 8;

      if((*(p_bitmap_data + iy*width + px)) & (1<<by))
        Gfx_drawPixel(x + width - px - 1, y + height - py - 1, color);
    }
  }
}

static inline void _drawBitmap270(int16_t x, int16_t y, uint16_t width, uint16_t height, const uint8_t* p_bitmap_data, gfx_color_t color) {
  int16_t px, py;

  for(px=0; px<height; px++) {
    for(py=0; py<width; py++) {
      uint8_t ix = px / 8;
      uint8_t bx = px % 8;

      if((*(p_bitmap_data + ix*width + py)) & (1<<bx))
        Gfx_drawPixel(x + px, y + width - py - 1, color);
    }
  }
}

static inline void _drawBitmapMirrorH(int16_t x, int16_t y, uint16_t width, uint16_t height, const uint8_t* p_bitmap_data, gfx_color_t color) {
  int16_t px, py;

  for(px=0; px<width; px++) {
    for(py=0; py<height; py++) {
      uint8_t iy = py / 8 + 1;
      uint8_t by = py % 8;

      if((*(p_bitmap_data + iy*width - px - 1)) & (1<<by))
        Gfx_drawPixel(x + px, y + py, color);
    }
  }
}

static void _invertRect(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
  int16_t ix, iy;

  if(x0 > x1) {
    int16_t t = x0;
    x0 = x1;
    x1 = t;
  }

  if(y0 > y1) {
    int16_t t = y0;
    y0 = y1;
    y1 = t;
  }

  for(ix=x0; ix<x1; ix++) {
    for(iy=y0; iy<y1; iy++)
      Gfx_invertPixel(ix, iy);
  }
}


static int16_t _getAlignXOffset(const char* str, const gfx_font_t* font, gfx_align_t align, gfx_rotation_t rotation) {
  int16_t xOffset = 0;

  // Horizontal align
  switch(align & 0x0F) {
    case GFX_ALIGN__LEFT: break;

    case GFX_ALIGN__CENTER: {
#if defined(GFX_SUPPORT_GLYPH_FONT)
      int16_t offset = Gfx_getStringWidth(str, font) / 2;
#else
      int16_t offset = strlen(str) * font->x_advance / 2 - (font->x_advance - font->width);
#endif
      switch(rotation) {
        case GFX_ROTATION__0:   xOffset -= offset; break;
        case GFX_ROTATION__180: xOffset += offset; break;
        default: break;
      }
    } break;

    case GFX_ALIGN__RIGHT: {
#if defined(GFX_SUPPORT_GLYPH_FONT)
      int16_t offset = Gfx_getStringWidth(str, font);
#else
      int16_t offset = strlen(str) * font->x_advance;
#endif
      switch(rotation) {
        case GFX_ROTATION__0:   xOffset -= offset; break;
        case GFX_ROTATION__180: xOffset += offset; break;
        default: break;
      }
    } break;

    default: break;
  }

  // Vertical align
  switch(align & 0xF0) {
    case GFX_ALIGN__TOP: break;

    case GFX_ALIGN__MIDDLE: {
      int16_t offset = font->height/2;
      switch(rotation) {
        case GFX_ROTATION__90:  xOffset += offset; break;
        case GFX_ROTATION__270: xOffset -= offset; break;
        default: break;
      }
    } break;

    case GFX_ALIGN__BOTTOM: {
      int16_t offset = font->height/2;
      switch(rotation) {
        case GFX_ROTATION__90:  xOffset += offset; break;
        case GFX_ROTATION__270: xOffset -= offset; break;
        default: break;
      }
    } break;

    default: break;
  }

  return xOffset;
}


static int16_t _getAlignYOffset(const char* str, const gfx_font_t* font, gfx_align_t align, gfx_rotation_t rotation) {
  int16_t yOffset = 0;

  // Horizontal align
  switch(align & 0x0F) {
    case GFX_ALIGN__LEFT: break;

    case GFX_ALIGN__CENTER: {
#if defined(GFX_SUPPORT_GLYPH_FONT)
      int16_t offset = Gfx_getStringWidth(str, font) / 2;
#else
      int16_t offset = strlen(str) * font->x_advance / 2 - (font->x_advance - font->width);
#endif
      switch(rotation) {
        case GFX_ROTATION__90:  yOffset -= offset; break;
        case GFX_ROTATION__270: yOffset += offset; break;
        default: break;
      }
    } break;

    case GFX_ALIGN__RIGHT: {
#if defined(GFX_SUPPORT_GLyOffsetPH_FONT)
      int16_t offset = Gfx_getStringWidth(str, font);
#else
      int16_t offset = strlen(str) * font->x_advance;
#endif
      switch(rotation) {
        case GFX_ROTATION__90:  yOffset -= offset; break;
        case GFX_ROTATION__270: yOffset += offset; break;
        default: break;
      }
    } break;

    default: break;
  }

  // Vertical align
  switch(align & 0xF0) {
    case GFX_ALIGN__TOP: break;

    case GFX_ALIGN__MIDDLE: {
      int16_t offset = font->height/2;
      switch(rotation) {
        case GFX_ROTATION__0:   yOffset -= offset; break;
        case GFX_ROTATION__180: yOffset += offset; break;
        default: break;
      }
    } break;

    case GFX_ALIGN__BOTTOM: {
      int16_t offset = font->height/2;
      switch(rotation) {
        case GFX_ROTATION__0:   yOffset -= offset; break;
        case GFX_ROTATION__180: yOffset += offset; break;
        default: break;
      }
    } break;

    default: break;
  }

  return yOffset;
}