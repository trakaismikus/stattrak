/**************************************************************************************************
* @file   gfx.h
* @brief  Header file for graphics module.
* @author Janis Zaharans
* @date   n/a
**************************************************************************************************/

#ifndef _GFX_H
#define _GFX_H

#ifdef __cplusplus
extern "C" {
#endif

/* > Includes ************************************************************************************/
#include <stdint.h>
#include <stddef.h>

/* > Defines *************************************************************************************/

// Macros
#define swap_int16_t(a, b)  { int16_t t = a; a = b; b = t; }

//#define GFX_PIXEL(x, y)   (*(m_ctx_p->frame_buf + ((y)/8)*m_ctx_p->height + (x)))
// #define GFX_PIXEL(x, y)   (*(m_ctx_p->frame_buf + ((y)/8)*m_ctx_p->width + (x)))

#define GFX_ABS(x)        ((x < 0) ? -(x) : (x))

/* > Enumerations ********************************************************************************/
typedef enum GfxAlignE {
  // Horizontal alignment
  GFX_ALIGN__LEFT   = 0x00,
  GFX_ALIGN__CENTER = 0x01,
  GFX_ALIGN__RIGHT  = 0x02,

  // Vertical alignment
  GFX_ALIGN__TOP    = 0x00,
  GFX_ALIGN__MIDDLE = 0x10,
  GFX_ALIGN__BOTTOM = 0x20,
} gfx_align_t;


typedef enum GfxRotationE {
  GFX_ROTATION__0 = 0,
  GFX_ROTATION__90,
  GFX_ROTATION__180,
  GFX_ROTATION__270,
  GFX_ROTATION__H_MIRROR,
  GFX_ROTATUIN__V_MIRROR,
} gfx_rotation_t;


/* > Type definitions ****************************************************************************/

typedef uint8_t gfx_color_t;

typedef struct GfxPointS {
  int16_t x;
  int16_t y;
} gfx_point_t;


typedef struct GfxRectS {
  int16_t   x;
  int16_t   y;
  uint16_t  width;
  uint16_t  height;
} gfx_rect_t;


typedef struct GfxCtxS {
  uint16_t       width;
  uint16_t       height;
  uint8_t*       frame_buf;
  gfx_rotation_t rotation;
  gfx_point_t    offset;
  void           (*update)(uint8_t*);
} gfx_ctx_t;


typedef union GfxVecS {
  struct {
    int16_t x0;
    int16_t y0;
    int16_t x1;
    int16_t y1;
  };

  int16_t coor[4];
} gfx_vec_t;


typedef struct GfxGlyphS {
  uint32_t offset;
  uint16_t width;
} gfx_glyph_t;


#if defined(GFX_SUPPORT_GLYPH_FONT)
  typedef struct GfxFontS {
    const uint8_t*  data;
    const gfx_glyph_t* glyphs;  // If NULL Monospace, else Glyph
    uint16_t  height;
    uint16_t  y_advance;            // Distance to next row
    
    union {
      struct {
        uint16_t  size;                 // Byte size of single char
        uint16_t  width;
        uint16_t  x_advance;            // Distance to next character
      };// monospaceParam;

      struct {
        uint16_t space;
      };// glyphParam;
    };

    // Function to get char index in data buffer
    // This is needed because all fonts won't have all charset
    uint8_t   (*char_to_idx)(char);
  } gfx_font_t;
#else
  typedef struct GfxFontS {
    const uint8_t*  data;
    uint16_t  size;                 // Byte size of single char
    uint16_t  width;
    uint16_t  height;
    uint16_t  x_advance;            // Distance to next character
    uint16_t  y_advance;            // Distance to next row

    // Function to get char index in data buffer
    // This is needed because all fonts won't have all charset
    uint8_t   (*char_to_idx)(char);
  } gfx_font_t;
#endif


typedef struct GfxBitmapS {
  uint16_t  width;
  uint16_t  height;
  const uint8_t*  bitmap;

  uint16_t  size;
  uint16_t  count;
} gfx_bitmap_t;

/* > Function Declaration ************************************************************************/

// --- Context
void        Gfx_setContext(gfx_ctx_t* ctx);
gfx_ctx_t*  Gfx_getContext(void);
void        Gfx_updateContext(void);

uint16_t Gfx_getWidth(void);
uint16_t Gfx_getHeight(void);

// --- Clipping Window
void Gfx_setWindow(int16_t x, int16_t y, uint16_t width, uint16_t height);
void Gfx_setWindowRect(gfx_vec_t* win);
void Gfx_unsetWindow(void);

// --- Pixel
void Gfx_drawPixel(int16_t x, int16_t y, gfx_color_t color);
void Gfx_invertPixel(int16_t x, int16_t y);

// --- Line
void Gfx_drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, gfx_color_t color);
void Gfx_drawLineDashed(int16_t x0, int16_t y0, int16_t x1, int16_t y1, gfx_color_t color, uint8_t dash);
void Gfx_drawLineDotted(int16_t x0, int16_t y0, int16_t x1, int16_t y1, gfx_color_t color, uint8_t dist);
void Gfx_drawHLine(int16_t x, int16_t y, int16_t width, gfx_color_t color);
void Gfx_drawVLine(int16_t x, int16_t y, int16_t height, gfx_color_t color);

// --- Rectangle
void Gfx_drawRect(int16_t x, int16_t y, int16_t width, int16_t height, gfx_color_t color);
void Gfx_fillRect(int16_t x, int16_t y, int16_t width, int16_t height, gfx_color_t color);
void Gfx_invertRect(int16_t x, int16_t y, int16_t width, int16_t height);
void Gfx_ditherRect(int16_t x, int16_t y, int16_t width, int16_t height, gfx_color_t color);
void Gfx_drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, gfx_color_t color);
void Gfx_fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, gfx_color_t color);

// --- Circle
void Gfx_drawCircle(int16_t x0, int16_t y0, uint8_t radius, gfx_color_t color);
void Gfx_fillCircle(int16_t x0, int16_t y0, int16_t r, gfx_color_t color);
void Gfx_drawCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t cornername, gfx_color_t color);
void Gfx_fillCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t cornername, int16_t delta, gfx_color_t color);

// --- Ellipse
void Gfx_drawEllipseHelper(int16_t x, int16_t y, uint16_t width, uint16_t height, uint8_t corner, gfx_color_t color);
void Gfx_drawEllipse(int16_t x, int16_t y, uint16_t width, uint16_t height, gfx_color_t color);
void Gfx_fillEllipse(int16_t x, int16_t y, uint16_t width, uint16_t height, gfx_color_t color);

// --- Triangle
void Gfx_fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, gfx_color_t color);

// --- Arc
void Gfx_drawArc(float x, float y, float rx, float ry, int16_t w, int16_t startAngle, int16_t endAngle, gfx_color_t color);

// --- Bitmap
void Gfx_drawBitmap(int16_t x, int16_t y, const gfx_bitmap_t* bitmap, gfx_color_t color, gfx_rotation_t rotation);
void Gfx_drawBitmapRaw(int16_t x, int16_t y, const uint8_t* bitmap, uint8_t width, uint8_t height, gfx_color_t color, gfx_rotation_t rotation);
void Gfx_drawBitmapArray(int16_t x, int16_t y, const gfx_bitmap_t* bitmap, uint8_t idx, gfx_color_t color, gfx_rotation_t rotation);

// --- Text
void Gfx_drawChar(int16_t x, int16_t y, char c, const gfx_font_t *font, gfx_color_t color, uint8_t invert, gfx_rotation_t rotation);
void Gfx_drawString(int16_t x, int16_t y, const char* str, const gfx_font_t *font, gfx_color_t color, uint8_t invert, gfx_rotation_t rotation);
void Gfx_drawStringAligned(int16_t x, int16_t y, const char* str, const gfx_font_t *font, gfx_color_t color, uint8_t invert, gfx_align_t align, gfx_rotation_t rotation);

void Gfx_drawCharWin(gfx_rect_t* win, int16_t x, int16_t y, char c, const gfx_font_t *font, gfx_color_t color, uint8_t invert, gfx_rotation_t rotation);
void Gfx_drawCharScaled(int16_t x, int16_t y, char c, const gfx_font_t *font, gfx_color_t color, uint8_t scale, uint8_t gap);
void Gfx_drawStringScaled(int16_t x, int16_t y, const char* str, const gfx_font_t *font, gfx_color_t color, uint8_t scale, uint8_t gap);
void Gfx_drawStringScaledAligned(int16_t x, int16_t y, const char* str, const gfx_font_t *font, gfx_color_t color, uint8_t scale, uint8_t gap, gfx_align_t align);
void Gfx_drawStringWin(gfx_rect_t* win, int16_t x, int16_t y, char* str, const gfx_font_t *font, gfx_color_t color, uint8_t invert, gfx_rotation_t rotation);

#if defined(GFX_SUPPORT_GLYPH_FONT)
uint32_t Gfx_getStringWidth(const char* str, const gfx_font_t *font);
#endif

// --- Clear
void Gfx_clearAll(gfx_color_t color);


#ifdef __cplusplus
}
#endif 

#endif // _GFX_H
