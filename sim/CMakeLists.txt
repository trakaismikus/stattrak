cmake_minimum_required(VERSION 3.10)

project(StatTralSimulator C)

set(CMAKE_C_STANDARD 11)


# Get Version from git tags
# Get project version from git tags
execute_process(
    COMMAND git describe --tags --dirty --always
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_DESCRIBE_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

if(GIT_DESCRIBE_VERSION)
  string(REGEX REPLACE
      "^v?([0-9]+\\.[0-9]+\\.[0-9]+).*$"
      "\\1"
      PROJECT_VER
      "${GIT_DESCRIBE_VERSION}"
  )
  message("Project version: " ${PROJECT_VER})
endif()


set(target simulator)

set(STATTRAK_SOURCE_DIR ${CMAKE_SOURCE_DIR}/../main)
set(COMPONENT_SOURCE_DIR ${CMAKE_SOURCE_DIR}/../components)

add_executable(${target} 
  simulator.c

  board/bsp_button.c
  board/bsp_oled.c
  board/bsp_buzzer.c
  board/board.c

  mock/ota.c
  mock/nvs_flash.c

  ${STATTRAK_SOURCE_DIR}/app.c
  ${STATTRAK_SOURCE_DIR}/battery.c
  ${STATTRAK_SOURCE_DIR}/eeprom.c
  ${STATTRAK_SOURCE_DIR}/app_view.c
  ${STATTRAK_SOURCE_DIR}/gfx_fx.c
  
  ${STATTRAK_SOURCE_DIR}/views/intro_view.c
  ${STATTRAK_SOURCE_DIR}/views/ask_view.c
  ${STATTRAK_SOURCE_DIR}/views/total_view.c
  ${STATTRAK_SOURCE_DIR}/views/kill_dead_view.c
  ${STATTRAK_SOURCE_DIR}/views/settings_view.c
  ${STATTRAK_SOURCE_DIR}/views/update_view.c
  ${STATTRAK_SOURCE_DIR}/views/stats_view.c
  ${STATTRAK_SOURCE_DIR}/views/fw_accept_view.c
  ${STATTRAK_SOURCE_DIR}/views/msg_view.c
  ${STATTRAK_SOURCE_DIR}/views/auto_off_view.c
  ${STATTRAK_SOURCE_DIR}/views/battery_low_view.c
  ${STATTRAK_SOURCE_DIR}/views/battery_critical_view.c

  ${STATTRAK_SOURCE_DIR}/resources/font.c
  ${STATTRAK_SOURCE_DIR}/resources/font_glyph.c
  ${STATTRAK_SOURCE_DIR}/resources/img.c

  ${COMPONENT_SOURCE_DIR}/gfx/gfx.c
  ${COMPONENT_SOURCE_DIR}/gui/animation.c
  ${COMPONENT_SOURCE_DIR}/gui/queue.c
  )

# Link SDL2
if(NOT EMSCRIPTEN)
  find_package(SDL2 REQUIRED) # SDL2 is required (on mac install via "brew install sdl2")
  target_link_libraries(${target} PRIVATE SDL2::SDL2 SDL2::SDL2main)
endif()

target_include_directories(${target} PRIVATE 
  mock

  ${SDL2_INCLUDE_DIRS}
  ${CMAKE_SOURCE_DIR}/board

  ${STATTRAK_SOURCE_DIR}
  ${STATTRAK_SOURCE_DIR}/resources
  ${STATTRAK_SOURCE_DIR}/views

  ${COMPONENT_SOURCE_DIR}/gfx
  ${COMPONENT_SOURCE_DIR}/gui
  ${COMPONENT_SOURCE_DIR}/ssd1307
  ${COMPONENT_SOURCE_DIR}/ble_ota
  )

target_compile_definitions(${target} PRIVATE 
  SIMULATOR=1
  GFX_SUPPORT_GLYPH_FONT=1
  PROJECT_VER="${PROJECT_VER}"
  )

add_custom_target(run
  COMMAND ${CMAKE_BINARY_DIR}/simulator
  DEPENDS ${target}
  USES_TERMINAL
)


# Emscripten-specific options
if(EMSCRIPTEN)

  if(SINGLE_FILE)
    message("Generating single-file wasm...")
    target_link_options(${target} PRIVATE
      -sSINGLE_FILE=1
    )

    execute_process(
      COMMAND base64 -i ${CMAKE_SOURCE_DIR}/web/StatTrakv3.jpg
      OUTPUT_VARIABLE STATTRAK_IMG_SRC
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set(STATTRAK_IMG_SRC "data:image/jpeg;base64,${STATTRAK_IMG_SRC}")
  else()
    set(STATTRAK_IMG_SRC "StatTrakv3.jpg")
    file(COPY ${CMAKE_SOURCE_DIR}/web/StatTrakv3.jpg DESTINATION ${CMAKE_BINARY_DIR})
  endif()

  configure_file(${CMAKE_SOURCE_DIR}/web/shell.html.in ${CMAKE_BINARY_DIR}/shell.html)

  set_target_properties(${target} PROPERTIES
    SUFFIX ".html"
  )

  target_compile_options(${target} PRIVATE
    -sUSE_SDL=2
  )

  target_link_options(${target} PRIVATE
    -sUSE_SDL=2
    -sALLOW_MEMORY_GROWTH=1
    --shell-file ${CMAKE_BINARY_DIR}/shell.html
  )


  # Compile
  # emcmake cmake [source dir]
  # cmake --build [build dir]

  # Run
  #emrun --no_browser --port 8080 .
endif()