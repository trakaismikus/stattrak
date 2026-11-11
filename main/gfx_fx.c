
/* > Includes *************************************************************************************/
#include "gfx.h"
#include "gfx_fx.h"
#include "animation.h"
#include <string.h>

/* > Local Function Declarations ******************************************************************/
static void _setupAnimation(animation_t * animation);
static void _updateAnimation(animation_t * animation, uint32_t progress);


/* > Type Definitions *****************************************************************************/
typedef struct {
  int16_t value;
  int16_t offset;
  int16_t oldOffset;
} anim_value_t;

/* > Constants ************************************************************************************/
static const animation_functions_t kAnimationFunc = {
  .setup = _setupAnimation,
  .update = _updateAnimation,
};

/* > Local Variables ******************************************************************************/
static anim_value_t s_animValue;
static animation_t s_anim = {
  .func = &kAnimationFunc,
  .curve = ANIMATION_CURVE__EASE_IN,
  .duration = ANIMATION_MS_TO_TICKS(300),
  .playCount = 1,
  .ctx = &s_animValue,
};

static uint8_t s_animState = 0;


/* > Local Function Definitions *******************************************************************/

void GfxFx_initCollapse(bool collapsed) {
  s_animState = collapsed ? 1 : 0;
  s_animValue.offset = collapsed ? 64 : 0;
  s_animValue.oldOffset = s_animValue.offset;
}

void GfxFx_startCollapse(void) {
  s_animValue.value = 64;
  Animation_start(&s_anim);
}

void GfxFx_reverseCollapse(void) {
  s_animValue.value = 0;
  Animation_start(&s_anim);
}

bool GfxFx_isCollapsed(void) {
  return (s_animState == 1 && s_animValue.offset == 64);
}

bool GfxFx_isNormal(void) {
  return (s_animState == 0 && s_animValue.offset == 0);
}

void GfxFx_renderCollapse(void) {
  gfx_ctx_t* ctx = Gfx_getContext();

   // Move content toward horizontal center
   if(s_animState == 0) {
    // Gfx_drawBitmap(0, 0, &kBmpStattrak, 1, GFX_ROTATION__0);

    uint8_t shift = (s_animValue.offset >> 2);
    uint8_t y = 0;
    const uint8_t kHalfHeight = ctx->height / 16;
    const uint8_t kHeight = ctx->height / 8;

    if(shift > 0) {
      uint8_t byteShift = (shift-1) / 8;
      if(byteShift > 0) {
        // Move top towards center
        memmove(&ctx->frame_buf[byteShift * ctx->width], &ctx->frame_buf[(byteShift - 1) * ctx->width], ctx->width);
        memset(&ctx->frame_buf[0], 0, byteShift * ctx->width);

        // Move bottom towards center
        memmove(&ctx->frame_buf[kHalfHeight * ctx->width],
                &ctx->frame_buf[(kHalfHeight + byteShift) * ctx->width],
                ctx->width * (kHalfHeight - byteShift));
        memset(&ctx->frame_buf[(kHeight - byteShift) * ctx->width], 0, byteShift * ctx->width);

        shift -= (byteShift * 8);
        y += byteShift;
      }

      for(uint8_t x=0; x<ctx->width; x++) {
        // Top half
        uint8_t yOffset = y * ctx->width;
        uint8_t temp = ctx->frame_buf[x + yOffset] >> (8 - shift);
        ctx->frame_buf[x + yOffset] = ctx->frame_buf[x + yOffset] << shift;
        if(y < 1)
          ctx->frame_buf[ctx->width + x + yOffset] = ctx->frame_buf[ctx->width + x + yOffset] << shift | temp;

        // Bottom half
        temp = ctx->frame_buf[x + (kHeight - 1) * ctx->width] & (0xFF >> (8 - shift));
        ctx->frame_buf[x + (kHeight - 1) * ctx->width] >>= shift;
        ctx->frame_buf[x + (kHeight - 2) * ctx->width] = ctx->frame_buf[x + (kHeight - 2) * ctx->width] >> shift | (temp << (8 - shift));
      }
    }
    if(s_animValue.offset > 0)
      Gfx_drawLine(64 - s_animValue.offset, 16, 64 + s_animValue.offset, 16, 1);
    
    if(s_animValue.offset >= 64) {
      s_animState = 1;
      s_animValue.offset = 0;
      s_animValue.oldOffset = 0;
      Animation_start(&s_anim);
    }
  } else if(s_animState == 1) {
    Gfx_clearAll(0);
    
    if(s_animValue.offset < 64) {
      Gfx_drawLine(0 + s_animValue.offset, 16, 127 - s_animValue.offset, 16, 1);

      if(s_animValue.offset == 0 && s_animValue.value == 0) {
        s_animState = 0;
        s_animValue.offset = 64;
        s_animValue.oldOffset = 64;
        Animation_start(&s_anim);
      }
    }
  }
}


static void _setupAnimation(animation_t * animation) {
  anim_value_t* animValue = (anim_value_t*)animation->ctx;
  animValue->oldOffset = animValue->offset;
}


static void _updateAnimation(animation_t * animation, uint32_t progress) {
  anim_value_t* animValue = (anim_value_t*)animation->ctx;
  animValue->offset = ANIM_LERP(animValue->oldOffset, animValue->value, progress);
}