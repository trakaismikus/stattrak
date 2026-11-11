/*
 * Host stand-in for the ESP-IDF generated sdkconfig.h.
 *
 * The CONFIG_BUTTON_* values are read from ../../sdkconfig.defaults by
 * sim/CMakeLists.txt, so the simulator uses the same button timing as the
 * device. The values below are only fallbacks.
 */
#ifndef _SDKCONFIG_H
#define _SDKCONFIG_H

#ifndef CONFIG_BUTTON_PERIOD_TIME_MS
  #define CONFIG_BUTTON_PERIOD_TIME_MS 5
#endif
#ifndef CONFIG_BUTTON_DEBOUNCE_TICKS
  #define CONFIG_BUTTON_DEBOUNCE_TICKS 2
#endif
#ifndef CONFIG_BUTTON_SHORT_PRESS_TIME_MS
  #define CONFIG_BUTTON_SHORT_PRESS_TIME_MS 180
#endif
#ifndef CONFIG_BUTTON_LONG_PRESS_TIME_MS
  #define CONFIG_BUTTON_LONG_PRESS_TIME_MS 700
#endif
#ifndef CONFIG_BUTTON_LONG_PRESS_HOLD_SERIAL_TIME_MS
  #define CONFIG_BUTTON_LONG_PRESS_HOLD_SERIAL_TIME_MS 500
#endif

#define CONFIG_IDF_TARGET "host"

#endif // _SDKCONFIG_H
