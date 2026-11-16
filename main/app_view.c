/***************************************************************************************************
 * @file    : app_view.c
 * @brief   : App View source code
 * @author  : Janis Zaharans
 * @date    : Jun 16, 2022
 **************************************************************************************************/

/* > Includes *************************************************************************************/
#include "app_view.h"
#include "app.h"
#include "animation.h"

/* > Local Function Declarations ********************************************************************/
static void _setupAnimation(animation_t * animation);
static void _updateAnimation(animation_t * animation, uint32_t progress);


/* > Local Variables ******************************************************************************/

view_t g_blankView;

static view_t* s_viewList[] = {
  #define VIEW(_id, _name) &g_##_name##View,
    VIEW_LIST
  #undef VIEW
};

static view_id_t s_viewId = VIEW_ID__BLANK;
static view_id_t s_nextViewId = VIEW_ID__BLANK;
static view_id_t s_prevViewId = VIEW_ID__BLANK;

static const animation_functions_t kAnimationFunc = {
  .setup = _setupAnimation,
  .update = _updateAnimation,
};

static int16_t yOffset = 0, yOld = 0;

// Where the new view comes from in the change animation, and how long the change takes
static app_view_from_t s_from = APP_VIEW_FROM__TOP;
static const uint32_t kDefaultChangeMs = 300;

static animation_t s_anim = {
  .func = &kAnimationFunc,
  .curve = ANIMATION_CURVE__EASE_OUT,
  .duration = ANIMATION_MS_TO_TICKS(300),
  .playCount = 1,
};


/* > Function Definitions *************************************************************************/

void AppView_set(view_id_t viewId) {
  AppView_setFrom(viewId, APP_VIEW_FROM__TOP, kDefaultChangeMs);
}

void AppView_setFrom(view_id_t viewId, app_view_from_t from, uint32_t durationMs) {
  if(viewId >= VIEW_COUNT)
    return;

  if(s_nextViewId != viewId) {
    // Pending view is replaced before it became active, undo its init
    if(s_nextViewId != s_viewId && s_viewList[s_nextViewId]->deinit)
      s_viewList[s_nextViewId]->deinit();

    s_nextViewId = viewId;

    if(s_nextViewId != s_viewId) {
      // Init new view
      if(s_viewList[s_nextViewId]->init)
        s_viewList[s_nextViewId]->init();

      s_from = from;
      s_anim.duration = ANIMATION_MS_TO_TICKS(durationMs);
      Animation_start(&s_anim);
    }
  }
}

view_id_t AppView_get(void) {
  return s_viewId;
}

view_id_t AppView_getNext(void) {
  return s_nextViewId;
}

bool AppView_isActive(view_id_t viewId) {
  return (s_viewId == viewId || s_nextViewId == viewId);
}

view_id_t AppView_getPrevious(void) {
  return s_prevViewId;
}

static bool s_drawing = false;

bool AppView_isDrawing(void) {
  return s_drawing;
}

void AppView_show(void) {
  s_drawing = true;
  gfx_ctx_t* gfx = Gfx_getContext();

  if(s_viewId != s_nextViewId) {
    // From the top: the old view moves down, the new view comes down from above.
    // From the bottom: the old view moves up, the new view comes up from below.
    const int16_t dir = (s_from == APP_VIEW_FROM__BOTTOM) ? -1 : 1;

    gfx->offset.y = dir * yOffset;
    if(s_viewList[s_viewId]->draw)
      s_viewList[s_viewId]->draw();

    gfx->offset.y = dir * (yOffset - 32);
    if(s_viewList[s_nextViewId]->draw)
      s_viewList[s_nextViewId]->draw();

    if(s_anim.active == false) {
       // Deinit previous view
      if(s_viewList[s_viewId]->deinit)
        s_viewList[s_viewId]->deinit();

      s_prevViewId = s_viewId;
      s_viewId = s_nextViewId;

      // Init new view
      // if(s_viewList[s_viewId]->init)
      //   s_viewList[s_viewId]->init();
    }
    gfx->offset.y = 0;
  }
  else {
    if(s_viewList[s_viewId]->draw)
      s_viewList[s_viewId]->draw();
  }
  s_drawing = false;
}

void AppView_update(void) {
  if(s_viewId != s_nextViewId) {
    // Deinit previous view
    if(s_viewList[s_viewId]->deinit)
      s_viewList[s_viewId]->deinit();

    // Switch view
    s_prevViewId = s_viewId;
    s_viewId = s_nextViewId;

    // Init new view
    if(s_viewList[s_viewId]->init)
      s_viewList[s_viewId]->init();
  }
}

// While the app menu slides in, button events go to the menu: a fast press after "both buttons"
// moves the menu, it does not count in the app that slides away. All other view changes keep
// the events in the current view (warnings go back to AppView_getPrevious()).
bool AppView_eventHandler(uint16_t event) {
  const view_id_t target = (s_nextViewId == VIEW_ID__LAUNCHER) ? s_nextViewId : s_viewId;

  // The repeats of a hold (each 0.5 s) go only to the view that got its long press: a hold that
  // answers a button on the display (0.7 s) must not also act on the next view (-1 kill). A hold
  // that starts in the slide of a view change keeps its repeats in the new view.
  static view_id_t s_longView[2] = { VIEW_ID__BLANK, VIEW_ID__BLANK };
  if(event == EVENT__LONG_PRESS_1 || event == EVENT__LONG_PRESS_2)
    s_longView[(event == EVENT__LONG_PRESS_1) ? 0 : 1] = target;
  if((event == EVENT__LONG_HOLD_1 || event == EVENT__LONG_HOLD_2) &&
     s_longView[(event == EVENT__LONG_HOLD_1) ? 0 : 1] != target)
    return true;

  if(s_viewList[target]->eventHandler)
    return s_viewList[target]->eventHandler(event);
  else
    return false;
}


/* > Local Function Definitions *******************************************************************/

static void _setupAnimation(animation_t * animation) {
  (void)animation;
  yOffset = 0;
  yOld = yOffset;
}


static void _updateAnimation(animation_t * animation, uint32_t progress) {
  (void)animation;
  yOffset = ANIM_LERP(yOld, 32, progress);
}