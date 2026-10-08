#!/bin/sh
# build_switch.sh - builds A2RacerRE for a Nintendo Switch with homebrew (custom firmware such as Atmosphere):
#   ./build_switch.sh -> a2racerre.nro; copy it to /switch/a2racerre/ on the SD card and start it from the Homebrew Menu.
# Needs devkitPro's devkitA64 with switch-sdl2 and switch-mesa (devkitpro.org). Without $DEVKITPRO it builds in the
# official devkitpro/devkita64 Docker image instead, which has all of that. The game files come from your own CD; see
# README.md. The source list is the one of build.sh.
set -e
cd "$(dirname "$0")"
if [ -z "$DEVKITPRO" ]; then
    echo "DEVKITPRO is not set: building in the devkitpro/devkita64 Docker image ..."
    exec docker run --rm -v "$PWD":/src -w /src devkitpro/devkita64 sh ./build_switch.sh "$@"
fi
OUT=${1:-a2racerre}
VERSION=$(sed -n 's/^FILEVERSION *\([0-9]*\),\([0-9]*\),\([0-9]*\).*/\1.\2.\3/p' res/a2racerre.rc)
SRC=$(sed -n 's/^SRC="\(.*\)"$/\1/p' build.sh)
PORT=$DEVKITPRO/portlibs/switch
PATH=$DEVKITPRO/devkitA64/bin:$DEVKITPRO/tools/bin:$PATH
ARCH="-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE"
# OpenGL ES 2.0 as on Android (gfx_gl.c); char is unsigned on ARM, the engine is written for x86, where it is signed; no
# fused multiply-adds, so the physics rounds as on the other platforms
CFLAGS="-std=gnu99 -O2 -ffunction-sections -fsigned-char -ffp-contract=off -Wno-format-truncation $ARCH -D__SWITCH__
        -DA2_GUI -D_FILE_OFFSET_BITS=64 -Isrc -I$PORT/include -I$PORT/include/SDL2 -I$DEVKITPRO/libnx/include"
LIBS="-L$PORT/lib -L$DEVKITPRO/libnx/lib -lSDL2 -lGLESv2 -lEGL -lglapi -ldrm_nouveau -lstdc++ -lnx -lm"
mkdir -p build/switch
echo "Building $OUT.nro (A2RacerRE $VERSION) ..."
# shellcheck disable=SC2086
aarch64-none-elf-gcc $CFLAGS -specs="$DEVKITPRO/libnx/switch.specs" -Wl,-Map,build/switch/$OUT.map \
    -o build/switch/$OUT.elf $SRC $LIBS
nacptool --create "A2RacerRE" "jjmhalew" "$VERSION" build/switch/$OUT.nacp
elf2nro build/switch/$OUT.elf "$OUT.nro" --icon=res/switch_icon.jpg --nacp=build/switch/$OUT.nacp
echo "Done: $OUT.nro"
