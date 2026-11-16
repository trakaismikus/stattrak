/***************************************************************************************************
 * @file    : test_units.c
 * @brief   : Unit tests of single firmware modules: storage, battery, button queue, animation
 *            and graphics.
 *
 * The tests call the real firmware code. Hardware is replaced by sim/host (see sim/README.md).
 **************************************************************************************************/

#include <stdlib.h>
#include <string.h>

#include "utest.h"
#include "host.h"

#include "nvs_flash.h"
#include "eeprom.h"
#include "battery.h"
#include "cyclic_buf.h"
#include "animation.h"
#include "bsp_oled.h"
#include "gfx.h"
#include "font.h"
#include "img.h"
#include "app.h"

/* > Storage (main/eeprom.c) **********************************************************************/

enum { E_A = 0, E_B, E_C, E_COUNT };

static const char* kKeys[E_COUNT] = { "a", "b", "c" };
static uint32_t s_values[E_COUNT];
static eeprom_t s_eeprom = {
  .entries = s_values,
  .entryCount = E_COUNT,
  .entryKeys = kKeys,
};

static void _freshStorage(void) {
  HostNvs_setFailWrites(false);
  HostNvs_setFailOpen(false);
  nvs_flash_erase();
  nvs_flash_init();
  memset(s_values, 0xEE, sizeof(s_values));
  memset(&s_eeprom.nvsHandle, 0, sizeof(s_eeprom.nvsHandle));
  s_eeprom.updateNeeded = false;
}

static uint32_t _flashValue(const char* key, uint32_t missing) {
  for(uint32_t i = 0; i < HostNvs_getEntryCount(); i++) {
    const host_nvs_entry_t* entry = HostNvs_getEntry(i);
    if(strcmp(entry->ns, "eeprom") == 0 && strcmp(entry->key, key) == 0)
      return entry->value;
  }
  return missing;
}

TEST(eeprom_missing_keys_read_as_zero) {
  _freshStorage();
  Eeprom_init(&s_eeprom);
  CHECK_EQ(Eeprom_read(E_A), 0);
  CHECK_EQ(Eeprom_read(E_B), 0);
  CHECK_EQ(Eeprom_read(E_C), 0);
}

TEST(eeprom_reads_values_from_flash) {
  _freshStorage();
  HostNvs_put("eeprom", "a", 11);
  HostNvs_put("eeprom", "c", 0xFFFFFFFF);
  Eeprom_init(&s_eeprom);
  CHECK_EQ(Eeprom_read(E_A), 11);
  CHECK_EQ(Eeprom_read(E_B), 0);
  CHECK_EQ(Eeprom_read(E_C), 0xFFFFFFFF);
}

TEST(eeprom_write_goes_to_flash) {
  _freshStorage();
  Eeprom_init(&s_eeprom);
  Eeprom_write(E_B, 42);
  CHECK_EQ(Eeprom_read(E_B), 42);
  CHECK_EQ(_flashValue("b", 0), 42);

  const uint32_t commits = HostNvs_getCommitCount();
  Eeprom_update();
  CHECK_EQ(HostNvs_getCommitCount(), commits + 1);
  Eeprom_update(); /* Nothing new: no second commit */
  CHECK_EQ(HostNvs_getCommitCount(), commits + 1);
}

TEST(eeprom_same_value_is_not_written_again) {
  _freshStorage();
  Eeprom_init(&s_eeprom);
  Eeprom_write(E_A, 5);
  const uint32_t writes = HostNvs_getWriteCount();
  Eeprom_write(E_A, 5);
  CHECK_EQ(HostNvs_getWriteCount(), writes);
}

TEST(eeprom_increment_and_decrement_have_limits) {
  _freshStorage();
  Eeprom_init(&s_eeprom);
  CHECK_EQ(Eeprom_increment(E_A, 2), 1);
  CHECK_EQ(Eeprom_increment(E_A, 2), 2);
  CHECK_EQ(Eeprom_increment(E_A, 2), 2);
  CHECK_EQ(_flashValue("a", 0), 2);
  CHECK_EQ(Eeprom_decrement(E_A, 1), 1);
  CHECK_EQ(Eeprom_decrement(E_A, 1), 1);
  CHECK_EQ(Eeprom_decrement(E_A, 0), 0);
  CHECK_EQ(Eeprom_decrement(E_A, 0), 0);

  Eeprom_write(E_C, 0xFFFFFFFF);
  CHECK_EQ(Eeprom_increment(E_C, 0xFFFFFFFF), 0xFFFFFFFF); /* No wrap to zero */
}

TEST(eeprom_bad_id_is_ignored) {
  _freshStorage();
  Eeprom_init(&s_eeprom);
  Eeprom_write(E_COUNT, 7);
  Eeprom_write(0xFFFFFFFF, 7);
  CHECK_EQ(Eeprom_read(E_COUNT), 0);
  CHECK_EQ(Eeprom_read(0xFFFFFFFF), 0);
  CHECK_EQ(Eeprom_increment(E_COUNT, 10), 0);
  CHECK_EQ(Eeprom_decrement(E_COUNT, 0), 0);
  CHECK_EQ(HostNvs_getEntryCount(), 0);
}

TEST(eeprom_write_defaults) {
  static const uint32_t kDefaults[E_COUNT] = { 1, 0, 8 };
  _freshStorage();
  Eeprom_init(&s_eeprom);
  Eeprom_writeDefaults(kDefaults);
  CHECK_EQ(Eeprom_read(E_A), 1);
  CHECK_EQ(Eeprom_read(E_B), 0);
  CHECK_EQ(Eeprom_read(E_C), 8);
  CHECK_EQ(_flashValue("a", 0), 1);
  CHECK_EQ(_flashValue("c", 0), 8);
  Eeprom_writeDefaults(NULL); /* Must not crash */
}

TEST(eeprom_format_erases_and_sets_defaults) {
  static const uint32_t kDefaults[E_COUNT] = { 1, 0, 8 };
  _freshStorage();
  Eeprom_init(&s_eeprom);
  Eeprom_write(E_A, 100);
  Eeprom_write(E_B, 200);
  Eeprom_format(kDefaults);
  CHECK_EQ(Eeprom_read(E_A), 1);
  CHECK_EQ(Eeprom_read(E_B), 0);
  CHECK_EQ(Eeprom_read(E_C), 8);
  CHECK_EQ(_flashValue("b", 0), 0);

  /* The storage must work after the format */
  Eeprom_write(E_B, 3);
  CHECK_EQ(_flashValue("b", 0), 3);
}

TEST(eeprom_works_in_ram_when_flash_writes_fail) {
  _freshStorage();
  Eeprom_init(&s_eeprom);
  HostNvs_setFailWrites(true);
  Eeprom_write(E_A, 9);
  CHECK_EQ(Eeprom_increment(E_A, 100), 10);
  Eeprom_update();
  CHECK_EQ(Eeprom_read(E_A), 10);
  CHECK_EQ(_flashValue("a", 12345), 12345);
  HostNvs_setFailWrites(false);
}

TEST(eeprom_works_in_ram_when_flash_cannot_open) {
  _freshStorage();
  HostNvs_setFailOpen(true);
  memset(s_values, 0, sizeof(s_values));
  Eeprom_init(&s_eeprom);
  CHECK_EQ(Eeprom_read(E_A), 0);
  Eeprom_write(E_A, 4);
  CHECK_EQ(Eeprom_read(E_A), 4);
  Eeprom_update();
  HostNvs_setFailOpen(false);
}

TEST(nvs_keys_of_the_firmware_are_valid) {
  /* NVS keys can have 15 characters at most. A longer key is never stored. */
  static const char* kFirmwareKeys[] = {
    #define ENTRY(_id, _key, _default, _max) #_key,
      ENTRY_LIST
    #undef ENTRY
  };
  for(uint32_t i = 0; i < sizeof(kFirmwareKeys) / sizeof(kFirmwareKeys[0]); i++) {
    CHECK(strlen(kFirmwareKeys[i]) > 0);
    CHECK(strlen(kFirmwareKeys[i]) < NVS_KEY_NAME_MAX_SIZE);
    for(uint32_t j = 0; j < i; j++)
      CHECK(strcmp(kFirmwareKeys[i], kFirmwareKeys[j]) != 0);
  }
  CHECK_EQ(sizeof(kFirmwareKeys) / sizeof(kFirmwareKeys[0]), ENTRY_ID_COUNT);
}

/* > Battery (main/battery.c) *********************************************************************/

static uint8_t _level(uint8_t type, uint16_t millivolts) {
  Battery_setType(type);
  HostBoard_setVoltage(millivolts);
  Battery_read(); /* The firmware takes one voltage sample per loop */
  return Battery_readLevel();
}

static bool _critical(uint8_t type, uint16_t millivolts) {
  Battery_setType(type);
  HostBoard_setVoltage(millivolts);
  Battery_read();
  return Battery_isCritical();
}

TEST(battery_level_of_a_1s_lipo) {
  /* The lithium curve: flat in the middle, steep at the start and at the end */
  CHECK_EQ(_level(BATTERY_TYPE__LIPO_1S, 4200), 100);
  CHECK_EQ(_level(BATTERY_TYPE__LIPO_1S, 4060), 90);
  CHECK_EQ(_level(BATTERY_TYPE__LIPO_1S, 3820), 50);
  CHECK_EQ(_level(BATTERY_TYPE__LIPO_1S, 3700), 13);   /* 3.68 V = 10 %, 3.74 V = 20 % */
  CHECK_EQ(_level(BATTERY_TYPE__LIPO_1S, 3680), 10);
  CHECK_EQ(_level(BATTERY_TYPE__LIPO_1S, 3500), 0);
  CHECK_EQ(_level(BATTERY_TYPE__LIPO_1S, 5000), 100);  /* Not more than 100 */
  CHECK_EQ(_level(BATTERY_TYPE__LIPO_1S, 3000), 0);    /* Not less than 0 */
  CHECK_EQ(_level(BATTERY_TYPE__LIPO_1S, 0), 0);
  CHECK_EQ(_level(BATTERY_TYPE__LIPO_1S, 65535), 100);
}

TEST(battery_level_of_other_types) {
  CHECK_EQ(_level(BATTERY_TYPE__RCR123A, 3820), 50);
  CHECK_EQ(_level(BATTERY_TYPE__LIPO_2S, 7640), 50);
  CHECK_EQ(_level(BATTERY_TYPE__LIPO_3S, 11460), 50);
  CHECK_EQ(_level(BATTERY_TYPE__NIMH_3S, 3630), 50);
  CHECK_EQ(_level(BATTERY_TYPE__NIMH_8S, 9680), 50);
  CHECK_EQ(_level(BATTERY_TYPE__NIMH_8S, 11200), 100);
  CHECK_EQ(_level(BATTERY_TYPE__NIMH_8S, 8000), 0);
}

/* Samples each 10 ms (the main loop) for `ms` at one voltage */
static void _hold(uint16_t millivolts, uint32_t ms) {
  HostBoard_setVoltage(millivolts);
  for(uint32_t t = 0; t < ms; t += 10) {
    Host_sleepMs(10);
    Battery_read();
  }
}

TEST(battery_level_is_calm_for_a_short_load) {
  /* A dip of 30 mV for 3 s (the radio, the buzzer) is 10 % on the flat middle: no change */
  CHECK_EQ(_level(BATTERY_TYPE__LIPO_1S, 3820), 50);
  _hold(3790, 3000);
  CHECK_EQ(Battery_readLevel(), 50);
  /* Noise of one ADC step around the value: no change */
  for(int i = 0; i < 100; i++)
    _hold((i & 1) ? 3824 : 3816, 100);
  CHECK_EQ(Battery_readLevel(), 50);
  _hold(3820, 1000);
  CHECK_EQ(Battery_readLevel(), 50);
}

TEST(battery_level_follows_a_lasting_change) {
  CHECK_EQ(_level(BATTERY_TYPE__LIPO_1S, 3820), 50);
  _hold(3790, 3 * 60 * 1000);     /* The battery is at 40 % now */
  CHECK_EQ(Battery_readLevel(), 40);
  CHECK(Battery_readVoltage() >= 3789 && Battery_readVoltage() <= 3791);
  /* A small rise does not move it up again: 3796 mV = 42 % */
  _hold(3796, 3 * 60 * 1000);
  CHECK_EQ(Battery_readLevel(), 40);
  /* A charger: it goes up */
  _hold(3920, 3 * 60 * 1000);
  CHECK_EQ(Battery_readLevel(), 70);
}

TEST(battery_level_reaches_0_and_100) {
  /* Sol review S1: the dead band kept 1 % (the "Low Bat: 0%" warning never came) and 99 % */
  CHECK_EQ(_level(BATTERY_TYPE__LIPO_1S, 3518), 1);
  _hold(3400, 3 * 60 * 1000);
  CHECK_EQ(Battery_readLevel(), 0);
  CHECK_EQ(_level(BATTERY_TYPE__LIPO_1S, 4186), 99);
  _hold(4200, 3 * 60 * 1000);
  CHECK_EQ(Battery_readLevel(), 100);
}

TEST(battery_critical_check_is_fast) {
  /* The average is slow, the critical check uses the last sample (as before) */
  CHECK(!_critical(BATTERY_TYPE__LIPO_1S, 3820));
  _hold(3200, 20);
  CHECK(Battery_isCritical());
  CHECK(Battery_readLevel() > 40);
}

TEST(battery_without_a_type_is_always_full) {
  CHECK_EQ(_level(BATTERY_TYPE__UNKNOWN, 0), 100);
  CHECK_EQ(_level(BATTERY_TYPE__USB, 0), 100);
  CHECK_EQ(_level(BATTERY_TYPE__USB, 5000), 100);
  CHECK(!_critical(BATTERY_TYPE__UNKNOWN, 0));
  CHECK(!_critical(BATTERY_TYPE__USB, 0));    /* 0 V on USB power must not switch the device off */
  CHECK(!_critical(BATTERY_TYPE__USB, 5000));
}

TEST(battery_critical_limit) {
  CHECK(_critical(BATTERY_TYPE__LIPO_1S, 3300));
  CHECK(!_critical(BATTERY_TYPE__LIPO_1S, 3301));
  CHECK(_critical(BATTERY_TYPE__LIPO_3S, 9900));
  CHECK(!_critical(BATTERY_TYPE__LIPO_3S, 9901));
  CHECK(_critical(BATTERY_TYPE__NIMH_8S, 7200));
  CHECK(!_critical(BATTERY_TYPE__NIMH_8S, 7201));
}

TEST(battery_type_that_does_not_exist_is_safe) {
  /* A value from an other firmware version. It must act as "no battery". */
  static const uint8_t kBadTypes[] = { BATTERY_TYPE__NIMH_8S + 1, 9, 100, 255 };
  for(uint32_t i = 0; i < sizeof(kBadTypes); i++) {
    CHECK_EQ(_level(kBadTypes[i], 3000), 100);
    CHECK(!_critical(kBadTypes[i], 0));
    CHECK(!_critical(kBadTypes[i], 3000));
  }
}

/* > Button event queue (main/cyclic_buf.h) *******************************************************/

TEST(cyclic_buf_keeps_the_order) {
  uint8_t storage[8];
  cyclic_buf_t buf;
  CyclicBuf_init(&buf, storage, sizeof(storage));

  CHECK(CyclicBuf_isEmpty(&buf));
  CHECK_EQ(CyclicBuf_getWritable(&buf), 7);

  for(uint8_t i = 1; i <= 7; i++)
    CyclicBuf_write(&buf, i);
  CHECK(CyclicBuf_isFull(&buf));
  CHECK_EQ(CyclicBuf_getReadable(&buf), 7);

  for(uint8_t i = 1; i <= 7; i++)
    CHECK_EQ(CyclicBuf_read(&buf), i);
  CHECK(CyclicBuf_isEmpty(&buf));
}

TEST(cyclic_buf_wraps_around) {
  uint8_t storage[4];
  cyclic_buf_t buf;
  CyclicBuf_init(&buf, storage, sizeof(storage));

  for(uint32_t round = 0; round < 50; round++) {
    CyclicBuf_write(&buf, (uint8_t)round);
    CyclicBuf_write(&buf, (uint8_t)(round + 100));
    CHECK_EQ(CyclicBuf_getReadable(&buf), 2);
    CHECK_EQ(CyclicBuf_read(&buf), (uint8_t)round);
    CHECK_EQ(CyclicBuf_read(&buf), (uint8_t)(round + 100));
  }
  CHECK(CyclicBuf_isEmpty(&buf));
}

/* > Animation (components/gui/animation.c) *******************************************************/

static int s_setupCount, s_teardownCount;
static uint32_t s_lastProgress;

static void _animSetup(animation_t* animation)    { (void)animation; s_setupCount++; }
static void _animTeardown(animation_t* animation) { (void)animation; s_teardownCount++; }
static void _animUpdate(animation_t* animation, uint32_t progress) { (void)animation; s_lastProgress = progress; }

static const animation_functions_t kAnimFunc = {
  .setup = _animSetup,
  .update = _animUpdate,
  .teardown = _animTeardown,
};

TEST(animation_runs_and_stops) {
  animation_t anim = { .func = &kAnimFunc, .curve = ANIMATION_CURVE__LINEAR, .duration = 100, .playCount = 1 };
  s_setupCount = s_teardownCount = 0;
  s_lastProgress = 12345;

  Animation_setTickCount(1000);
  Animation_start(&anim);
  CHECK(anim.active);
  CHECK_EQ(s_setupCount, 1);

  Animation_runTickHandler(1050);
  CHECK_EQ(s_lastProgress, ANIMATION__NORMALIZED_MAX / 2);
  CHECK(anim.active);

  Animation_runTickHandler(1100);
  CHECK_EQ(s_lastProgress, ANIMATION__NORMALIZED_MAX);

  Animation_runTickHandler(1101);
  CHECK(!anim.active);
  CHECK_EQ(s_teardownCount, 1);
}

TEST(animation_progress_stays_in_range) {
  static const animation_curve_t kCurves[] = {
    ANIMATION_CURVE__LINEAR, ANIMATION_CURVE__EASE_IN, ANIMATION_CURVE__EASE_OUT, ANIMATION_CURVE__EASE_IN_OUT,
  };
  for(uint32_t c = 0; c < sizeof(kCurves) / sizeof(kCurves[0]); c++) {
    animation_t anim = { .func = &kAnimFunc, .curve = kCurves[c], .duration = 300, .playCount = 1 };
    Animation_setTickCount(0);
    Animation_start(&anim);
    for(uint32_t t = 0; t <= 300; t += 10) {
      Animation_runTickHandler(t);
      CHECK(s_lastProgress <= ANIMATION__NORMALIZED_MAX);
    }
    Animation_runTickHandler(400);
    CHECK(!anim.active);
  }
}

TEST(animation_more_than_the_maximum_is_ignored) {
  animation_t anims[12];
  Animation_setTickCount(0);
  for(uint32_t i = 0; i < 12; i++) {
    anims[i] = (animation_t){ .func = &kAnimFunc, .curve = ANIMATION_CURVE__LINEAR, .duration = 50, .playCount = 1 };
    Animation_start(&anims[i]);
  }
  CHECK(anims[9].active);
  CHECK(!anims[10].active);   /* The list has 10 places */
  CHECK(!anims[11].active);

  Animation_runTickHandler(100);
  for(uint32_t i = 0; i < 12; i++)
    CHECK(!anims[i].active);
}

/* > Graphics (components/gfx/gfx.c) **************************************************************/

static int _pixelCount(void) {
  const gfx_ctx_t* ctx = Gfx_getContext();
  int count = 0;
  for(uint32_t i = 0; i < HOST_FRAME_SIZE; i++) {
    for(uint8_t bit = 0; bit < 8; bit++)
      count += (ctx->frame_buf[i] >> bit) & 1;
  }
  return count;
}

TEST(gfx_draws_inside_the_display_only) {
  BspOled_init();
  Gfx_clearAll(0);
  CHECK_EQ(_pixelCount(), 0);

  Gfx_drawPixel(0, 0, 1);
  Gfx_drawPixel(127, 31, 1);
  CHECK_EQ(_pixelCount(), 2);

  /* Outside: nothing must change, and nothing must be written outside the frame buffer */
  Gfx_drawPixel(-1, 0, 1);
  Gfx_drawPixel(0, -1, 1);
  Gfx_drawPixel(128, 0, 1);
  Gfx_drawPixel(0, 32, 1);
  Gfx_drawPixel(32767, 32767, 1);
  Gfx_drawPixel(-32768, -32768, 1);
  CHECK_EQ(_pixelCount(), 2);
  CHECK(HostOled_guardsIntact());

  Gfx_fillRect(0, 0, 128, 32, 1);
  CHECK_EQ(_pixelCount(), 128 * 32);
  Gfx_fillRect(-50, -50, 300, 300, 0);
  CHECK_EQ(_pixelCount(), 0);
  CHECK(HostOled_guardsIntact());
}

TEST(gfx_random_drawing_stays_inside_the_frame_buffer) {
  static const char* kStrings[] = { "", "0", "KILL", "Do a Factory Reset?", "9.6V(NiMH)", "\x7f\x80\xff!~" };
  static const gfx_font_t* kFonts[] = { &font5x7, &font3x5, &font5x5 };

  BspOled_init();
  srand(20261110);

  for(uint32_t i = 0; i < 20000; i++) {
    const int16_t x = (int16_t)(rand() % 500 - 250);
    const int16_t y = (int16_t)(rand() % 200 - 100);
    const int16_t w = (int16_t)(rand() % 300 - 20);
    const int16_t h = (int16_t)(rand() % 100 - 20);
    const gfx_font_t* font = kFonts[rand() % 3];
    const char* str = kStrings[rand() % 6];

    switch(rand() % 24) {
      case 0:  Gfx_drawPixel(x, y, rand() & 1); break;
      case 1:  Gfx_drawLine(x, y, (int16_t)(x + w), (int16_t)(y + h), 1); break;
      case 2:  Gfx_drawHLine(x, y, w, 1); break;
      case 3:  Gfx_drawVLine(x, y, h, 1); break;
      case 4:  Gfx_drawRect(x, y, w, h, 1); break;
      case 5:  Gfx_fillRect(x, y, w, h, rand() & 1); break;
      case 6:  Gfx_invertRect(x, y, w, h); break;
      case 7:  Gfx_drawRoundRect(x, y, (int16_t)(abs(w) + 8), (int16_t)(abs(h) + 8), 3, 1); break;
      case 8:  Gfx_fillRoundRect(x, y, (int16_t)(abs(w) + 8), (int16_t)(abs(h) + 8), 3, 1); break;
      case 9:  Gfx_drawString(x, y, str, font, 1, rand() & 1, GFX_ROTATION__0); break;
      case 10: Gfx_drawStringAligned(x, y, str, font, 1, 0, (gfx_align_t)(rand() % 3), GFX_ROTATION__0); break;
      case 11: Gfx_drawCharScaled(x, y, (char)(rand() % 256), &font5x7, 1, (uint8_t)(rand() % 5), 1); break;
      case 12: Gfx_drawLineDashed(x, y, (int16_t)(x + w), (int16_t)(y + h), 1, 2); break;
      case 13: Gfx_drawLineDotted(x, y, (int16_t)(x + w), (int16_t)(y + h), 1, 3); break;
      case 14: Gfx_drawCircle(x, y, (uint8_t)(rand() % 60), 1); break;
      case 15: Gfx_fillCircle(x, y, (int16_t)(rand() % 60), 1); break;
      case 16: Gfx_drawEllipse(x, y, (uint16_t)(rand() % 150), (uint16_t)(rand() % 60), 1); break;
      case 17: Gfx_fillEllipse(x, y, (uint16_t)(rand() % 150), (uint16_t)(rand() % 60), 1); break;
      case 18: Gfx_fillTriangle(x, y, (int16_t)(x + w), y, (int16_t)(x + w / 2), (int16_t)(y + h), 1); break;
      case 19: Gfx_drawBitmap(x, y, (rand() & 1) ? &kBmpStattrak : &kBmpWarning, 1, GFX_ROTATION__0); break;
      case 20: Gfx_ditherRect(x, y, w, h, 1); break;
      case 21: Gfx_drawStringScaled(x, y, str, &font5x7, 1, (uint8_t)(1 + rand() % 4), 1); break;
      case 22: Gfx_drawStringScaledAligned(x, y, str, &font5x5, 1, 2, 0, (gfx_align_t)(rand() % 3)); break;
      default:
        Gfx_setWindow(x, y, (uint16_t)abs(w), (uint16_t)abs(h));
        Gfx_drawString((int16_t)(x - 5), (int16_t)(y - 2), str, font, 1, 0, GFX_ROTATION__0);
        Gfx_unsetWindow();
        break;
    }

    if(!HostOled_guardsIntact()) {
      CHECK(HostOled_guardsIntact());
      break;
    }
  }

  CHECK(HostOled_guardsIntact());
}

/* > Main *****************************************************************************************/

int main(void) {
  /* USB power: the battery tests move the time (Host_sleepMs) without the power latch */
  const host_config_t config = { .voltageMv = 4000, .usbPower = true };
  Host_init(&config, NULL);

  RUN(eeprom_missing_keys_read_as_zero);
  RUN(eeprom_reads_values_from_flash);
  RUN(eeprom_write_goes_to_flash);
  RUN(eeprom_same_value_is_not_written_again);
  RUN(eeprom_increment_and_decrement_have_limits);
  RUN(eeprom_bad_id_is_ignored);
  RUN(eeprom_write_defaults);
  RUN(eeprom_format_erases_and_sets_defaults);
  RUN(eeprom_works_in_ram_when_flash_writes_fail);
  RUN(eeprom_works_in_ram_when_flash_cannot_open);
  RUN(nvs_keys_of_the_firmware_are_valid);

  RUN(battery_level_of_a_1s_lipo);
  RUN(battery_level_of_other_types);
  RUN(battery_without_a_type_is_always_full);
  RUN(battery_critical_limit);
  RUN(battery_type_that_does_not_exist_is_safe);
  RUN(battery_level_is_calm_for_a_short_load);
  RUN(battery_level_follows_a_lasting_change);
  RUN(battery_level_reaches_0_and_100);
  RUN(battery_critical_check_is_fast);

  RUN(cyclic_buf_keeps_the_order);
  RUN(cyclic_buf_wraps_around);

  RUN(animation_runs_and_stops);
  RUN(animation_progress_stays_in_range);
  RUN(animation_more_than_the_maximum_is_ignored);

  RUN(gfx_draws_inside_the_display_only);
  RUN(gfx_random_drawing_stays_inside_the_frame_buffer);

  return utest_finish();
}
