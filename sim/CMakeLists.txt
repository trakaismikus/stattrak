cmake_minimum_required(VERSION 3.16)

project(StatTrakSimulator C)

# The StatTrak firmware on a computer.
#
#   stattrak_headless   no window, script driven. Used by the tests and by tools/sim.py.
#   simulator           a window (SDL2), or a web page (Emscripten).
#   test_units          unit tests of single firmware modules.
#   test_ble_ota        tests of the Bluetooth firmware-update packet code.
#
# The real firmware sources are compiled without changes. Only the hardware and the
# ESP-IDF SDK are replaced, by the files in sim/host/.

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)   # The firmware uses GNU C, as ESP-IDF does

option(STATTRAK_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(STATTRAK_32BIT    "Build 32-bit code, like the ESP32-C2 (needs gcc-multilib)" OFF)
option(STATTRAK_SDL      "Build the window simulator (needs SDL2)" ON)
option(STATTRAK_TESTS    "Build the unit test programs" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  if(EMSCRIPTEN)
    set(CMAKE_BUILD_TYPE Release)   # A small web page
  else()
    set(CMAKE_BUILD_TYPE Debug)     # Good error reports in tests
  endif()
endif()

set(ROOT_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(MAIN_DIR      ${ROOT_DIR}/main)
set(COMPONENT_DIR ${ROOT_DIR}/components)
set(HOST_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/host)
set(TEST_DIR      ${ROOT_DIR}/tests/unit)


# --- Firmware version from version.txt (the same file as the firmware build)
file(STRINGS ${ROOT_DIR}/version.txt PROJECT_VER LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${ROOT_DIR}/version.txt)
message(STATUS "Project version: ${PROJECT_VER}")


# --- Button timing from sdkconfig.defaults, so the simulator has the timing of the device
set(BUTTON_DEFINITIONS "")
file(STRINGS ${ROOT_DIR}/sdkconfig.defaults BUTTON_CONFIG_LINES REGEX "^CONFIG_BUTTON_[A-Z_]+=[0-9]+")
foreach(line ${BUTTON_CONFIG_LINES})
  string(STRIP "${line}" line)
  list(APPEND BUTTON_DEFINITIONS "${line}")
endforeach()
message(STATUS "Button timing: ${BUTTON_DEFINITIONS}")


# --- Firmware sources, compiled without changes
set(FIRMWARE_SOURCES
  ${MAIN_DIR}/main.c
  ${MAIN_DIR}/app.c
  ${MAIN_DIR}/app_view.c
  ${MAIN_DIR}/apps.c
  ${MAIN_DIR}/battery.c
  ${MAIN_DIR}/ble_commands.c
  ${MAIN_DIR}/app_data.c
  ${MAIN_DIR}/backup.c
  ${MAIN_DIR}/data_store.c
  ${MAIN_DIR}/device_log.c
  ${MAIN_DIR}/start_pic.c
  ${MAIN_DIR}/eeprom.c
  ${MAIN_DIR}/gfx_fx.c

  ${MAIN_DIR}/board/bsp_button.c

  ${MAIN_DIR}/views/app_host_view.c
  ${MAIN_DIR}/views/app_settings_view.c
  ${MAIN_DIR}/views/app_sync_view.c
  ${MAIN_DIR}/views/ask_view.c
  ${MAIN_DIR}/views/auto_off_view.c
  ${MAIN_DIR}/views/battery_critical_view.c
  ${MAIN_DIR}/views/battery_low_view.c
  ${MAIN_DIR}/views/battery_unset_view.c
  ${MAIN_DIR}/views/bt_start.c
  ${MAIN_DIR}/views/device_model_view.c
  ${MAIN_DIR}/views/fw_accept_view.c
  ${MAIN_DIR}/views/hold_button.c
  ${MAIN_DIR}/views/intro_view.c
  ${MAIN_DIR}/views/kill_dead_view.c
  ${MAIN_DIR}/views/launcher_view.c
  ${MAIN_DIR}/views/msg_view.c
  ${MAIN_DIR}/views/settings_view.c
  ${MAIN_DIR}/views/space_view.c
  ${MAIN_DIR}/views/stats_view.c
  ${MAIN_DIR}/views/total_view.c
  ${MAIN_DIR}/views/update_view.c

  ${MAIN_DIR}/resources/font.c
  ${MAIN_DIR}/resources/font_glyph.c
  ${MAIN_DIR}/resources/img.c
  ${MAIN_DIR}/resources/app_icons.c

  ${COMPONENT_DIR}/gfx/gfx.c
  ${COMPONENT_DIR}/gui/animation.c
  ${COMPONENT_DIR}/stapp/stapp_check.c
  ${COMPONENT_DIR}/stapp/stapp_vm.c
)

# --- Stand-ins for the hardware and the SDK
set(HOST_SOURCES
  ${HOST_DIR}/host_core.c
  ${HOST_DIR}/host_board.c
  ${HOST_DIR}/host_button.c
  ${HOST_DIR}/host_nvs.c
  ${HOST_DIR}/host_ota.c
  ${HOST_DIR}/sim_apps.c
)

# -iquote and not -I: main/time.h must not replace the system <time.h>.
set(QUOTE_INCLUDE_DIRS
  ${HOST_DIR}/include
  ${HOST_DIR}
  ${MAIN_DIR}
  ${MAIN_DIR}/board
  ${MAIN_DIR}/views
  ${MAIN_DIR}/resources
  ${COMPONENT_DIR}/gfx
  ${COMPONENT_DIR}/gui
  ${COMPONENT_DIR}/stapp
  ${COMPONENT_DIR}/ssd1307
  ${COMPONENT_DIR}/ble_ota
  ${COMPONENT_DIR}/app_ble
)

set(COMMON_OPTIONS "")
foreach(dir ${QUOTE_INCLUDE_DIRS})
  list(APPEND COMMON_OPTIONS "SHELL:-iquote ${dir}")
endforeach()

set(COMMON_LINK_OPTIONS "")

if(STATTRAK_32BIT)
  list(APPEND COMMON_OPTIONS -m32)
  list(APPEND COMMON_LINK_OPTIONS -m32)
endif()

if(STATTRAK_SANITIZE)
  list(APPEND COMMON_OPTIONS -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  list(APPEND COMMON_LINK_OPTIONS -fsanitize=address,undefined)
endif()

set(COMMON_DEFINITIONS
  GFX_SUPPORT_GLYPH_FONT=1
  PROJECT_VER="${PROJECT_VER}"
  ${BUTTON_DEFINITIONS}
)

# The firmware uses "%ld" for uint32_t. That is correct for the device compiler, where
# uint32_t is "unsigned long". A computer compiler gives a warning for each one.
set(FIRMWARE_WARNINGS -Wall -Wno-format)

add_library(stattrak_firmware OBJECT ${FIRMWARE_SOURCES})
target_compile_options(stattrak_firmware PRIVATE ${COMMON_OPTIONS} ${FIRMWARE_WARNINGS})
target_compile_definitions(stattrak_firmware PRIVATE ${COMMON_DEFINITIONS})

add_library(stattrak_host OBJECT ${HOST_SOURCES})
target_compile_options(stattrak_host PRIVATE ${COMMON_OPTIONS} -Wall -Wextra)
target_compile_definitions(stattrak_host PRIVATE ${COMMON_DEFINITIONS})

# Helper: a program that contains the firmware and the stand-ins
function(stattrak_add_program name)
  add_executable(${name} ${ARGN} $<TARGET_OBJECTS:stattrak_firmware> $<TARGET_OBJECTS:stattrak_host>)
  target_compile_options(${name} PRIVATE ${COMMON_OPTIONS} -Wall -Wextra)
  target_compile_definitions(${name} PRIVATE ${COMMON_DEFINITIONS})
  target_link_options(${name} PRIVATE ${COMMON_LINK_OPTIONS})
  target_link_libraries(${name} PRIVATE m)
endfunction()


# --- Headless simulator (not for the web build)
if(NOT EMSCRIPTEN)
  stattrak_add_program(stattrak_headless headless/main.c)
endif()


# --- Draw tool: the firmware fonts, pictures and draw functions for tools/design.py
if(NOT EMSCRIPTEN)
  add_executable(gfx_tool tools/gfx_tool.c
    ${COMPONENT_DIR}/gfx/gfx.c
    ${MAIN_DIR}/resources/font.c
    ${MAIN_DIR}/resources/font_glyph.c
    ${MAIN_DIR}/resources/img.c)
  target_compile_options(gfx_tool PRIVATE ${COMMON_OPTIONS} ${FIRMWARE_WARNINGS})
  target_compile_definitions(gfx_tool PRIVATE GFX_SUPPORT_GLYPH_FONT=1)
  target_link_options(gfx_tool PRIVATE ${COMMON_LINK_OPTIONS})
  target_link_libraries(gfx_tool PRIVATE m)
endif()


# --- App package check: the C check of the firmware for tests/test_app_match.py
if(NOT EMSCRIPTEN)
  add_executable(stapp_tool tools/stapp_tool.c ${COMPONENT_DIR}/stapp/stapp_check.c ${COMPONENT_DIR}/stapp/stapp_vm.c)
  target_compile_options(stapp_tool PRIVATE ${COMMON_OPTIONS} ${FIRMWARE_WARNINGS})
  target_link_options(stapp_tool PRIVATE ${COMMON_LINK_OPTIONS})
endif()


# --- Unit tests
if(STATTRAK_TESTS AND NOT EMSCRIPTEN)
  stattrak_add_program(test_units ${TEST_DIR}/test_units.c)
  target_compile_options(test_units PRIVATE "SHELL:-iquote ${TEST_DIR}")

  # The app engine alone (components/stapp), with a fuzz test of broken packages
  add_executable(test_stapp ${TEST_DIR}/test_stapp.c ${COMPONENT_DIR}/stapp/stapp_check.c ${COMPONENT_DIR}/stapp/stapp_vm.c)
  target_compile_options(test_stapp PRIVATE ${COMMON_OPTIONS} ${FIRMWARE_WARNINGS} "SHELL:-iquote ${TEST_DIR}")
  target_link_options(test_stapp PRIVATE ${COMMON_LINK_OPTIONS})

  # The Bluetooth update packet code, with a small stand-in for the NimBLE headers.
  add_executable(test_ble_ota ${TEST_DIR}/test_ble_ota.c ${COMPONENT_DIR}/ble_ota/ble_ota.c)
  target_compile_options(test_ble_ota PRIVATE
    "SHELL:-iquote ${TEST_DIR}/nimble"
    "SHELL:-iquote ${TEST_DIR}"
    "SHELL:-iquote ${COMPONENT_DIR}/ble_ota"
    "SHELL:-iquote ${COMPONENT_DIR}/app_ble"
    -Wall -Wno-unused-but-set-variable)
  target_compile_options(test_ble_ota PRIVATE -Wno-format)
  if(STATTRAK_32BIT)
    target_compile_options(test_ble_ota PRIVATE -m32)
    target_link_options(test_ble_ota PRIVATE -m32)
  endif()
  if(STATTRAK_SANITIZE)
    # No alignment check: the packet code reads fields at odd addresses. The device CPU allows that.
    target_compile_options(test_ble_ota PRIVATE -fsanitize=address,undefined -fno-sanitize=alignment -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_link_options(test_ble_ota PRIVATE -fsanitize=address,undefined)
  endif()
endif()


# --- Window simulator (SDL2) and web simulator (Emscripten)
set(BUILD_SIMULATOR OFF)
if(EMSCRIPTEN)
  set(BUILD_SIMULATOR ON)
elseif(STATTRAK_SDL)
  find_package(SDL2 QUIET)   # macOS: "brew install sdl2", Ubuntu: "apt install libsdl2-dev"
  if(SDL2_FOUND)
    set(BUILD_SIMULATOR ON)
  else()
    message(STATUS "SDL2 not found: the window simulator is not built. The headless simulator is.")
  endif()
endif()

if(BUILD_SIMULATOR)
  stattrak_add_program(simulator sdl/main.c)

  if(NOT EMSCRIPTEN)
    if(TARGET SDL2::SDL2)
      if(TARGET SDL2::SDL2main)
        target_link_libraries(simulator PRIVATE SDL2::SDL2main)
      endif()
      target_link_libraries(simulator PRIVATE SDL2::SDL2)
    else()
      target_include_directories(simulator PRIVATE ${SDL2_INCLUDE_DIRS})
      target_link_libraries(simulator PRIVATE ${SDL2_LIBRARIES})
    endif()

    add_custom_target(run
      COMMAND simulator
      DEPENDS simulator
      USES_TERMINAL
    )
  endif()
endif()


# --- Web page (Emscripten)
#
#   emcmake cmake -S sim -B build/web            (add -DSINGLE_FILE=1 for one html file)
#   cmake --build build/web
#   python3 -m http.server -d build/web          then open http://localhost:8000/simulator.html
if(EMSCRIPTEN)

  if(SINGLE_FILE)
    message(STATUS "Generating single-file wasm...")
    target_link_options(simulator PRIVATE -sSINGLE_FILE=1)

    # The picture goes into the html file as base64 text
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    execute_process(
      COMMAND ${Python3_EXECUTABLE} -c "import base64,sys;sys.stdout.write(base64.b64encode(open(sys.argv[1],'rb').read()).decode())"
              ${ROOT_DIR}/updater/assets/device.webp
      OUTPUT_VARIABLE STATTRAK_IMG_BASE64
      OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE STATTRAK_IMG_RESULT
    )
    if(NOT STATTRAK_IMG_RESULT EQUAL 0)
      message(FATAL_ERROR "Cannot read updater/assets/device.webp")
    endif()
    set(STATTRAK_IMG_SRC "data:image/webp;base64,${STATTRAK_IMG_BASE64}")
  else()
    set(STATTRAK_IMG_SRC "device.webp")
    file(COPY ${ROOT_DIR}/updater/assets/device.webp DESTINATION ${CMAKE_BINARY_DIR})
  endif()

  # The apps of the web updater catalog (updater/apps/*.stapp) go into the page as hex text: the
  # "Apps" panel installs them. Run "python tools/app.py catalog" first to have the newest ones.
  file(GLOB SIM_APP_FILES CONFIGURE_DEPENDS ${ROOT_DIR}/updater/apps/*.stapp)
  set(SIM_APPS_JS "")
  foreach(app_file ${SIM_APP_FILES})
    file(READ ${app_file} app_hex HEX)
    string(APPEND SIM_APPS_JS "\"${app_hex}\",")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${app_file})
  endforeach()
  set(SIM_APPS_JS "[${SIM_APPS_JS}]")

  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html.in ${CMAKE_BINARY_DIR}/shell.html)

  set_target_properties(simulator PROPERTIES SUFFIX ".html")

  target_compile_options(simulator PRIVATE -sUSE_SDL=2)

  # ASYNCIFY: the firmware main loop never returns, so it must give time to the browser.
  # FORCE_FILESYSTEM: the device state file is copied to the browser storage.
  target_link_options(simulator PRIVATE
    -sUSE_SDL=2
    -sALLOW_MEMORY_GROWTH=1
    -sASYNCIFY=1
    -sFORCE_FILESYSTEM=1
    --shell-file ${CMAKE_BINARY_DIR}/shell.html
  )
endif()
