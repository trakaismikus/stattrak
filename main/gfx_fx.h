#ifndef _GFX_FX_H
#define _GFX_FX_H

#include <stdint.h>
#include <stdbool.h>

void GfxFx_initCollapse(bool collapsed);
void GfxFx_startCollapse(void);
void GfxFx_reverseCollapse(void);
bool GfxFx_isCollapsed(void);
bool GfxFx_isNormal(void);
void GfxFx_renderCollapse(void);


#endif // _GFX_FX_H