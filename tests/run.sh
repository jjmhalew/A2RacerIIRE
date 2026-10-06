#!/bin/sh
# tests/run.sh - the tests that need no game data (CI): builds tests/unit.c with the files it tests and runs it.
set -e
cd "$(dirname "$0")/.."
CC=${CC:-cc}
mkdir -p out
$CC -std=c99 -O1 -Wall -Isrc -o out/unit tests/unit.c src/ini.c src/image.c src/audio.c src/smk.c -lm
./out/unit
