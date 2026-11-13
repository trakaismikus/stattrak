/***************************************************************************************************
 * @file    : gfx_tool.c
 * @brief   : The firmware draw code (components/gfx) and the firmware fonts and pictures
 *            (main/resources) as a small command line program.
 *
 * Two uses:
 *
 *   gfx_tool assets   Prints all firmware fonts and pictures as JSON. tools/design.py makes
 *                     updater/designer/assets.js from it, so the screen designer shows the same
 *                     pixels as the device.
 *
 *   gfx_tool draw     Reads draw commands from standard input and draws them with the real
 *                     firmware functions. The tests compare the result with the designer, to
 *                     make sure that a design looks the same on the device.
 *
 * Draw commands (one per line):
 *   clear
 *   text <font> <x> <y> <L|C|R> <color> <invert 0|1> <text to the end of the line>
 *   scaled <font> <x> <y> <L|C|R> <color> <scale> <gap 0|1> <text to the end of the line>
 *   rect <x> <y> <w> <h> <color>
 *   fill <x> <y> <w> <h> <color>
 *   rrect <x> <y> <w> <h> <radius> <color>
 *   frrect <x> <y> <w> <h> <radius> <color>
 *   invert <x> <y> <w> <h>
 *   line <x0> <y0> <x1> <y1> <color>
 *   circle <x> <y> <radius> <color>
 *   fcircle <x> <y> <radius> <color>
 *   pixel <x> <y> <color>
 *   image <name> <x> <y> <color>
 *   bitmap <x> <y> <w> <h> <color> <hex>   a picture in the firmware format, 2 hex digits per byte
 *   frame                              prints the display: 32 lines of 128 characters, '#' = on
 **************************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "gfx_config.h"
#include "font.h"
#include "font_glyph.h"
#include "img.h"

#define DISPLAY_WIDTH   (128)
#define DISPLAY_HEIGHT  (32)

/* A work area that is larger than the display, to measure characters */
#define WORK_WIDTH      (128)
#define WORK_HEIGHT     (64)

typedef struct {
  const char*       name;     /* Name in a design file */
  const gfx_font_t* font;
  const char*       symbol;   /* Name in the firmware source */
  const char*       label;    /* Name for people */
} tool_font_t;

typedef struct {
  const char*         name;
  const gfx_bitmap_t* bitmap;
  const char*         symbol;
  const char*         label;
} tool_image_t;

static const tool_font_t kFonts[] = {
  { "5x7",     &font5x7,                    "font5x7",                    "Text 5x7" },
  { "5x5",     &font5x5,                    "font5x5",                    "Caps 5x5" },
  { "3x5",     &font3x5,                    "font3x5",                    "Small 3x5" },
  { "big",     &kFontNightverbBig,          "kFontNightverbBig",          "Title 7" },
  { "numbers", &kFontNightverbLargeNumbers, "kFontNightverbLargeNumbers", "Numbers 14" },
};

static const tool_image_t kImages[] = {
  { "stattrak", &kBmpStattrak, "kBmpStattrak", "StatTrak logo" },
  { "tridos",   &kBmpTridos,   "kBmpTridos",   "TRIDOS logo" },
  { "warning",  &kBmpWarning,  "kBmpWarning",  "Warning sign" },
};

#define FONT_COUNT   (sizeof(kFonts) / sizeof(kFonts[0]))
#define IMAGE_COUNT  (sizeof(kImages) / sizeof(kImages[0]))

static uint8_t   s_frameBuf[GFX_FRAME_BUF_SIZE(WORK_WIDTH, WORK_HEIGHT)];
static gfx_ctx_t s_ctx;

static void _noUpdate(uint8_t* buf) { (void)buf; }

static void _setContext(uint16_t width, uint16_t height) {
  memset(&s_ctx, 0, sizeof(s_ctx));
  s_ctx.width = width;
  s_ctx.height = height;
  s_ctx.frame_buf = s_frameBuf;
  s_ctx.rotation = GFX_ROTATION__0;
  s_ctx.update = _noUpdate;
  Gfx_setContext(&s_ctx);
  Gfx_unsetWindow();
  Gfx_clearAll(0);
}

static const tool_font_t* _findFont(const char* name) {
  for(size_t i = 0; i < FONT_COUNT; i++) {
    if(strcmp(kFonts[i].name, name) == 0)
      return &kFonts[i];
  }
  return NULL;
}

static const tool_image_t* _findImage(const char* name) {
  for(size_t i = 0; i < IMAGE_COUNT; i++) {
    if(strcmp(kImages[i].name, name) == 0)
      return &kImages[i];
  }
  return NULL;
}

static void _printRows(uint16_t width, uint16_t height, const char* indent, bool json) {
  for(uint16_t y = 0; y < height; y++) {
    if(json)
      printf("%s\"", indent);
    for(uint16_t x = 0; x < width; x++)
      putchar(Gfx_getPixelColor(&s_ctx, (int16_t)x, (int16_t)y) ? '#' : '.');
    if(json)
      printf("\"%s\n", (y + 1 < height) ? "," : "");
    else
      putchar('\n');
  }
}

/* > assets ***************************************************************************************/

static void _printJsonChar(char c) {
  if(c == '"' || c == '\\')
    printf("\\%c", c);
  else
    putchar(c);
}

static int _assets(void) {
  printf("{\n");
  printf("  \"display\": { \"width\": %d, \"height\": %d },\n", DISPLAY_WIDTH, DISPLAY_HEIGHT);

  printf("  \"fonts\": [\n");
  for(size_t f = 0; f < FONT_COUNT; f++) {
    const gfx_font_t* font = kFonts[f].font;
    const bool glyph = (font->glyphs != NULL);

    printf("    {\n");
    printf("      \"name\": \"%s\",\n", kFonts[f].name);
    printf("      \"symbol\": \"%s\",\n", kFonts[f].symbol);
    printf("      \"label\": \"%s\",\n", kFonts[f].label);
    printf("      \"height\": %u,\n", (unsigned)font->height);
    /* Distance between two characters, after the width of the first one */
    printf("      \"space\": %u,\n", glyph ? (unsigned)font->space : (unsigned)(font->x_advance - font->width));
    printf("      \"chars\": {\n");

    for(int c = ' '; c <= '~'; c++) {
      const uint8_t idx = font->char_to_idx((char)c);
      const uint16_t width = glyph ? font->glyphs[idx].width : font->width;

      /* Draw the character with the firmware function and read the pixels back */
      _setContext(WORK_WIDTH, WORK_HEIGHT);
      Gfx_drawChar(0, 0, (char)c, font, 1, 0, GFX_ROTATION__0);

      printf("        \"");
      _printJsonChar((char)c);
      printf("\": { \"w\": %u, \"rows\": [", (unsigned)width);
      for(uint16_t y = 0; y < font->height; y++) {
        printf("\"");
        for(uint16_t x = 0; x < width; x++)
          putchar(Gfx_getPixelColor(&s_ctx, (int16_t)x, (int16_t)y) ? '#' : '.');
        printf("\"%s", (y + 1 < font->height) ? "," : "");
      }
      printf("] }%s\n", (c < '~') ? "," : "");
    }

    printf("      }\n");
    printf("    }%s\n", (f + 1 < FONT_COUNT) ? "," : "");
  }
  printf("  ],\n");

  printf("  \"images\": [\n");
  for(size_t i = 0; i < IMAGE_COUNT; i++) {
    const gfx_bitmap_t* bitmap = kImages[i].bitmap;

    _setContext(WORK_WIDTH, WORK_HEIGHT);
    Gfx_drawBitmap(0, 0, bitmap, 1, GFX_ROTATION__0);

    printf("    {\n");
    printf("      \"name\": \"%s\",\n", kImages[i].name);
    printf("      \"symbol\": \"%s\",\n", kImages[i].symbol);
    printf("      \"label\": \"%s\",\n", kImages[i].label);
    printf("      \"w\": %u,\n", (unsigned)bitmap->width);
    printf("      \"h\": %u,\n", (unsigned)bitmap->height);
    printf("      \"rows\": [\n");
    _printRows(bitmap->width, bitmap->height, "        ", true);
    printf("      ]\n");
    printf("    }%s\n", (i + 1 < IMAGE_COUNT) ? "," : "");
  }
  printf("  ]\n");
  printf("}\n");
  return 0;
}

/* > draw *****************************************************************************************/

/* Returns the text after `count` numbers/words, without the line end */
static char* _rest(char* line, int count) {
  char* p = line;
  for(int i = 0; i < count; i++) {
    while(*p == ' ') p++;
    while(*p != '\0' && *p != ' ') p++;
  }
  if(*p == ' ')
    p++;
  return p;
}

static gfx_align_t _align(char align) {
  return (align == 'C') ? GFX_ALIGN__CENTER : (align == 'R') ? GFX_ALIGN__RIGHT : GFX_ALIGN__LEFT;
}

static int _draw(void) {
  static char line[4096];
  static uint8_t bitmap[GFX_FRAME_BUF_SIZE(DISPLAY_WIDTH, DISPLAY_HEIGHT)];
  int lineNumber = 0;

  _setContext(DISPLAY_WIDTH, DISPLAY_HEIGHT);

  while(fgets(line, sizeof(line), stdin) != NULL) {
    char cmd[16] = { 0 };
    char name[32] = { 0 };
    char align = 'L';
    int a = 0, b = 0, c = 0, d = 0, e = 0, f = 0;

    lineNumber++;
    line[strcspn(line, "\r\n")] = '\0';
    if(sscanf(line, "%15s", cmd) != 1)
      continue;

    if(strcmp(cmd, "clear") == 0) {
      Gfx_clearAll(0);
    } else if(strcmp(cmd, "frame") == 0) {
      _printRows(DISPLAY_WIDTH, DISPLAY_HEIGHT, "", false);
      fflush(stdout);
    } else if(strcmp(cmd, "text") == 0 && sscanf(line, "%*s %31s %d %d %c %d %d", name, &a, &b, &align, &c, &d) == 6) {
      const tool_font_t* font = _findFont(name);
      if(font == NULL)
        goto bad;
      Gfx_drawStringAligned((int16_t)a, (int16_t)b, _rest(line, 7), font->font, (gfx_color_t)c, (uint8_t)d,
                            _align(align), GFX_ROTATION__0);
    } else if(strcmp(cmd, "scaled") == 0 && sscanf(line, "%*s %31s %d %d %c %d %d %d", name, &a, &b, &align, &c, &d, &e) == 7) {
      const tool_font_t* font = _findFont(name);
      if(font == NULL || d < 1 || d > 8)
        goto bad;
      Gfx_drawStringScaledAligned((int16_t)a, (int16_t)b, _rest(line, 8), font->font, (gfx_color_t)c, (uint8_t)d,
                                  (uint8_t)e, _align(align));
    } else if(strcmp(cmd, "rect") == 0 && sscanf(line, "%*s %d %d %d %d %d", &a, &b, &c, &d, &e) == 5) {
      Gfx_drawRect((int16_t)a, (int16_t)b, (int16_t)c, (int16_t)d, (gfx_color_t)e);
    } else if(strcmp(cmd, "fill") == 0 && sscanf(line, "%*s %d %d %d %d %d", &a, &b, &c, &d, &e) == 5) {
      Gfx_fillRect((int16_t)a, (int16_t)b, (int16_t)c, (int16_t)d, (gfx_color_t)e);
    } else if(strcmp(cmd, "rrect") == 0 && sscanf(line, "%*s %d %d %d %d %d %d", &a, &b, &c, &d, &e, &f) == 6) {
      Gfx_drawRoundRect((int16_t)a, (int16_t)b, (int16_t)c, (int16_t)d, (int16_t)e, (gfx_color_t)f);
    } else if(strcmp(cmd, "frrect") == 0 && sscanf(line, "%*s %d %d %d %d %d %d", &a, &b, &c, &d, &e, &f) == 6) {
      Gfx_fillRoundRect((int16_t)a, (int16_t)b, (int16_t)c, (int16_t)d, (int16_t)e, (gfx_color_t)f);
    } else if(strcmp(cmd, "invert") == 0 && sscanf(line, "%*s %d %d %d %d", &a, &b, &c, &d) == 4) {
      Gfx_invertRect((int16_t)a, (int16_t)b, (int16_t)c, (int16_t)d);
    } else if(strcmp(cmd, "line") == 0 && sscanf(line, "%*s %d %d %d %d %d", &a, &b, &c, &d, &e) == 5) {
      Gfx_drawLine((int16_t)a, (int16_t)b, (int16_t)c, (int16_t)d, (gfx_color_t)e);
    } else if(strcmp(cmd, "circle") == 0 && sscanf(line, "%*s %d %d %d %d", &a, &b, &c, &d) == 4) {
      Gfx_drawCircle((int16_t)a, (int16_t)b, (uint8_t)c, (gfx_color_t)d);
    } else if(strcmp(cmd, "fcircle") == 0 && sscanf(line, "%*s %d %d %d %d", &a, &b, &c, &d) == 4) {
      Gfx_fillCircle((int16_t)a, (int16_t)b, (int16_t)c, (gfx_color_t)d);
    } else if(strcmp(cmd, "pixel") == 0 && sscanf(line, "%*s %d %d %d", &a, &b, &c) == 3) {
      Gfx_drawPixel((int16_t)a, (int16_t)b, (gfx_color_t)c);
    } else if(strcmp(cmd, "image") == 0 && sscanf(line, "%*s %31s %d %d %d", name, &a, &b, &c) == 4) {
      const tool_image_t* image = _findImage(name);
      if(image == NULL)
        goto bad;
      Gfx_drawBitmap((int16_t)a, (int16_t)b, image->bitmap, (gfx_color_t)c, GFX_ROTATION__0);
    } else if(strcmp(cmd, "bitmap") == 0 && sscanf(line, "%*s %d %d %d %d %d", &a, &b, &c, &d, &e) == 5) {
      /* Firmware picture format: one byte for each column of each band of 8 rows */
      const char* hex = _rest(line, 6);
      if(c < 1 || c > DISPLAY_WIDTH || d < 1 || d > DISPLAY_HEIGHT)
        goto bad;
      const size_t size = (size_t)c * (size_t)((d + 7) / 8);
      if(strlen(hex) != 2 * size)
        goto bad;
      for(size_t i = 0; i < size; i++) {
        unsigned int byte = 0;
        if(sscanf(&hex[2 * i], "%2x", &byte) != 1)
          goto bad;
        bitmap[i] = (uint8_t)byte;
      }
      Gfx_drawBitmapRaw((int16_t)a, (int16_t)b, bitmap, (uint8_t)c, (uint8_t)d, (gfx_color_t)e, GFX_ROTATION__0);
    } else {
      goto bad;
    }
    continue;

bad:
    fprintf(stderr, "gfx_tool: line %d is not a draw command: %s\n", lineNumber, line);
    return 2;
  }

  return 0;
}

/* > Main *****************************************************************************************/

int main(int argc, char** argv) {
  if(argc == 2 && strcmp(argv[1], "assets") == 0)
    return _assets();
  if(argc == 2 && strcmp(argv[1], "draw") == 0)
    return _draw();

  fprintf(stderr, "Usage: gfx_tool assets | gfx_tool draw < commands.txt\n");
  return 1;
}
