#!/bin/sh
# build.sh - builds A2RacerRE for Linux (and the Steam Deck) with SDL2: ./build.sh -> ./a2racerre
# Needs a C compiler and SDL2, e.g. on Debian / Ubuntu: sudo apt install build-essential libsdl2-dev
# (Windows: build.bat. Android: android/.) The game files come from your own CD; see README.md.
# The source list is the same in build.bat and android/app/CMakeLists.txt.
set -e
cd "$(dirname "$0")"
CC=${CC:-cc}
OUT=${1:-a2racerre}
SRC="src/main.c src/plat_sdl.c src/gfx_gl.c src/res.c src/image.c src/ini.c src/audio.c src/smk.c src/film.c src/texture.c src/track.c src/datasetup.c src/exetab.c src/model.c src/objects.c src/race.c src/hud.c src/paths.c src/effects.c src/sfx.c src/rules.c src/font3d.c src/car_old.c src/ai.c src/collide.c src/menu.c src/menu_ui.c src/menu_screens.c src/game.c"
echo "Building $OUT ..."
# shellcheck disable=SC2086
$CC -std=gnu99 -O2 -Isrc -D_FILE_OFFSET_BITS=64 -o "$OUT" $SRC $(sdl2-config --cflags) $(sdl2-config --libs) -lm
echo "Done: $OUT"
