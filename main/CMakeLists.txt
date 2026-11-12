
set(SRC_FILES
  "main.c"
  "board.c"
  "system.c"
  "app_view.c"
  "app.c"
  "apps.c"
  "eeprom.c"
  "battery.c"
  "ble_commands.c"
  "gfx_fx.c"
  
  "board/bsp_adc.c"
  "board/bsp_buzzer.c"
  "board/bsp_button.c"
  "board/bsp_oled.c"

  "resources/font.c"
  "resources/font_glyph.c"
  "resources/img.c"
  "resources/app_icons.c"

  "views/app_host_view.c"
  "views/app_settings_view.c"
  "views/app_sync_view.c"
  "views/ask_view.c"
  "views/intro_view.c"
  "views/launcher_view.c"
  "views/kill_dead_view.c"
  "views/settings_view.c"
  "views/space_view.c"
  "views/stats_view.c"
  "views/total_view.c"
  "views/update_view.c"
  "views/fw_accept_view.c"
  "views/msg_view.c"
  "views/hold_button.c"
  "views/auto_off_view.c"
  "views/battery_low_view.c"
  "views/battery_critical_view.c"
)

set(INC_DIRS
  "."
  "views"
  "board"
  "resources"
  )

idf_component_register(
  SRCS ${SRC_FILES}
  PRIV_REQUIRES spi_flash nvs_flash bt app_update esp_ringbuf app_ble ble_ota gfx ssd1307 gui esp_adc stapp
  INCLUDE_DIRS ${INC_DIRS})

add_compile_definitions(GFX_SUPPORT_GLYPH_FONT=1)