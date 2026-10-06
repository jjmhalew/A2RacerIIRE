# The native part of the Android build: SDL2 (fetched; its Java glue is in src/main/java/org/libsdl) and the engine of
# ../../src as libmain.so on OpenGL ES 2.0. The source list is the same as in build.bat and build.sh.
cmake_minimum_required(VERSION 3.18)
project(A2RacerRE C)

include(FetchContent)
FetchContent_Declare(SDL2
    URL https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz
    URL_HASH SHA256=5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL2_DISABLE_SDL2MAIN ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL2)

set(S ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
add_library(main SHARED
    ${S}/main.c ${S}/plat_sdl.c ${S}/pad.c ${S}/touch.c ${S}/gfx_gl.c ${S}/res.c ${S}/image.c ${S}/ini.c ${S}/audio.c ${S}/smk.c ${S}/film.c
    ${S}/texture.c ${S}/track.c ${S}/datasetup.c ${S}/exetab.c ${S}/model.c ${S}/objects.c ${S}/race.c ${S}/hud.c ${S}/paths.c ${S}/effects.c ${S}/sfx.c ${S}/rules.c ${S}/font3d.c ${S}/flag.c ${S}/car_old.c ${S}/wheels.c ${S}/shadow.c ${S}/lights.c ${S}/fog.c ${S}/ai.c ${S}/collide.c
    ${S}/menu.c ${S}/menu_ui.c ${S}/menu_screens.c ${S}/game.c ${S}/txcopy.c ${S}/radar.c)
target_include_directories(main PRIVATE ${S})
target_compile_definitions(main PRIVATE A2_GUI)
# char is unsigned on ARM; the engine is written for x86, where it is signed
target_compile_options(main PRIVATE -std=gnu99 -O2 -fsigned-char)
set_source_files_properties(${S}/main.c PROPERTIES COMPILE_DEFINITIONS main=SDL_main)   # SDLActivity calls SDL_main
target_link_libraries(main PRIVATE SDL2::SDL2 GLESv2 log android m)
