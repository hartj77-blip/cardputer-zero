cmake_minimum_required(VERSION 3.16)
project(zeroshell VERSION 0.1.0 LANGUAGES C CXX)

# Backends:
#   sim      SDL2 window on a desktop PC (development)
#   device   Linux framebuffer + evdev keyboard (Cardputer Zero)
#   headless no window; renders screens to image files (CI screenshots, tests)
#   web      WebAssembly for a browser demo (build with emcmake; see web/)
set(ZS_BACKEND "sim" CACHE STRING "sim | device | headless | web")
set_property(CACHE ZS_BACKEND PROPERTY STRINGS sim device headless web)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# LVGL is pinned. Set FETCHCONTENT_SOURCE_DIR_LVGL to use a local checkout.
include(FetchContent)
FetchContent_Declare(lvgl
  GIT_REPOSITORY https://github.com/lvgl/lvgl.git
  GIT_TAG v9.2.2
  GIT_SHALLOW TRUE)

set(LV_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h CACHE STRING "" FORCE)
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(lvgl)

set(ZS_SDL 0)
set(ZS_DEVICE 0)
if(ZS_BACKEND STREQUAL "sim")
  set(ZS_SDL 1)
elseif(ZS_BACKEND STREQUAL "device")
  set(ZS_DEVICE 1)
endif()
target_compile_definitions(lvgl PUBLIC
  ZS_BACKEND_SDL=${ZS_SDL} ZS_BACKEND_DEVICE=${ZS_DEVICE})

if(ZS_SDL)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2 REQUIRED sdl2)
  target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})
  target_link_libraries(lvgl PUBLIC ${SDL2_LIBRARIES})
endif()

file(GLOB_RECURSE ZS_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_executable(zeroshell ${ZS_SOURCES})
target_include_directories(zeroshell PRIVATE src)
target_compile_definitions(zeroshell PRIVATE
  ZS_VERSION="${PROJECT_VERSION}"
  ZS_BACKEND_NAME="${ZS_BACKEND}")
target_compile_options(zeroshell PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(zeroshell PRIVATE lvgl pthread m)

if(ZS_BACKEND STREQUAL "web")
  if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "ZS_BACKEND=web needs Emscripten: emcmake cmake ...")
  endif()
  set_target_properties(zeroshell PROPERTIES SUFFIX ".js")
  target_link_options(zeroshell PRIVATE
    -sMODULARIZE=1 -sEXPORT_NAME=createZeroShell -sENVIRONMENT=web
    -sSINGLE_FILE=1 -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=0
    -sEXPORTED_FUNCTIONS=_main,_zs_web_key,_zs_web_open,_zs_web_frame,_zs_web_take_dirty,_zs_web_hdmi
    -sEXPORTED_RUNTIME_METHODS=HEAPU8)
endif()

install(TARGETS zeroshell RUNTIME DESTINATION bin)
