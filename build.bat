@echo off
rem build.bat - builds A2RacerRE from source on Windows 10/11; nothing to install first.
rem   build.bat         A2RacerRE.exe, the game (start it, it asks for the CD the first time)
rem   build.bat dev     out\a2racer.exe, the developer build (console output, same engine)
rem   build.bat test    the developer tests in out\ (format parsers, no window)
rem The C compiler is Zig (ziglang.org): a zig on PATH or "pip install ziglang" is used when there, otherwise the official
rem Windows build is downloaded once into tools\zig and checked against its SHA-256. SDL2 (zlib license) is downloaded once
rem into tools\sdl the same way and linked statically, so A2RacerRE.exe is a single file.
setlocal
cd /d "%~dp0"

set "ZIG_VER=0.16.0"
set "ZIG_SHA=68659eb5f1e4eb1437a722f1dd889c5a322c9954607f5edcf337bc3684a75a7e"
set "ZIG_NAME=zig-x86_64-windows-%ZIG_VER%"
set "SDL_VER=2.32.10"
set "SDL_SHA=f15cff5fca62ec9381a016ef1d42a95c638cd72d2f226ba5781c76fe43dbd1ac"
set "SDL=tools\sdl\SDL2-%SDL_VER%\x86_64-w64-mingw32"
set "SRC=src\main.c src\plat_sdl.c src\pad.c src\touch.c src\gfx_gl.c src\res.c src\image.c src\ini.c src\audio.c src\smk.c src\film.c src\texture.c src\track.c src\datasetup.c src\exetab.c src\model.c src\objects.c src\race.c src\hud.c src\paths.c src\effects.c src\sfx.c src\rules.c src\font3d.c src\flag.c src\car_old.c src\wheels.c src\shadow.c src\lights.c src\fog.c src\ai.c src\collide.c src\menu.c src\menu_ui.c src\menu_screens.c src\game.c src\txcopy.c src\radar.c"
set "LIBS=%SDL%\lib\libSDL2.a -lopengl32 -lgdi32 -luser32 -lwinmm -limm32 -lole32 -loleaut32 -lversion -lsetupapi -lshell32 -ladvapi32 -lcfgmgr32 -lcomdlg32"
rem zig cc compiles for the CPU of the build machine unless told otherwise; plain x86-64 runs everywhere.
set "CPU=-target x86_64-windows-gnu -mcpu=baseline"
set "INC=-Isrc -I%SDL%\include\SDL2"

call :find_zig || goto :fail
call :find_sdl || goto :fail

if /i "%~1"=="dev" (
    if not exist out mkdir out
    echo Building out\a2racer.exe ^(developer build^)...
    %ZIG% cc -std=c99 -O2 -g %CPU% %INC% -o out\a2racer.exe %SRC% res\a2racerre.rc %LIBS% || goto :fail
    echo Done: out\a2racer.exe
    exit /b 0
)

echo Building A2RacerRE.exe...
%ZIG% cc -std=c99 -O2 -s %CPU% %INC% -DA2_GUI -Wl,--subsystem,windows -o A2RacerRE.exe %SRC% res\a2racerre.rc %LIBS% || goto :fail
echo Done: A2RacerRE.exe
exit /b 0

:find_zig
if exist "tools\zig\%ZIG_NAME%\zig.exe" (set ZIG="%CD%\tools\zig\%ZIG_NAME%\zig.exe" & exit /b 0)
where zig >nul 2>nul && (set "ZIG=zig" & exit /b 0)
if exist ".venv\Scripts\python.exe" .venv\Scripts\python -m ziglang version >nul 2>nul && (set "ZIG=.venv\Scripts\python -m ziglang" & exit /b 0)
python -m ziglang version >nul 2>nul && (set "ZIG=python -m ziglang" & exit /b 0)
echo Zig %ZIG_VER% (the C compiler, about 95 MB) is not here yet: downloading it from ziglang.org into tools\zig ...
if not exist tools\zig mkdir tools\zig
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; $ProgressPreference='SilentlyContinue'; [Net.ServicePointManager]::SecurityProtocol='Tls12'; Invoke-WebRequest -Uri 'https://ziglang.org/download/%ZIG_VER%/%ZIG_NAME%.zip' -OutFile 'tools\zig\zig.zip'; if ((Get-FileHash 'tools\zig\zig.zip' -Algorithm SHA256).Hash -ne '%ZIG_SHA%') { Remove-Item 'tools\zig\zig.zip'; throw 'SHA-256 of the download does not match' }" || exit /b 1
tar -xf tools\zig\zig.zip -C tools\zig || exit /b 1
del tools\zig\zig.zip
if not exist "tools\zig\%ZIG_NAME%\zig.exe" (echo The Zig archive did not contain %ZIG_NAME%\zig.exe & exit /b 1)
set ZIG="%CD%\tools\zig\%ZIG_NAME%\zig.exe"
exit /b 0

:find_sdl
if exist "%SDL%\lib\libSDL2.a" exit /b 0
echo SDL %SDL_VER% (about 14 MB) is not here yet: downloading it from github.com/libsdl-org into tools\sdl ...
if not exist tools\sdl mkdir tools\sdl
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; $ProgressPreference='SilentlyContinue'; [Net.ServicePointManager]::SecurityProtocol='Tls12'; Invoke-WebRequest -Uri 'https://github.com/libsdl-org/SDL/releases/download/release-%SDL_VER%/SDL2-devel-%SDL_VER%-mingw.zip' -OutFile 'tools\sdl\sdl.zip'; if ((Get-FileHash 'tools\sdl\sdl.zip' -Algorithm SHA256).Hash -ne '%SDL_SHA%') { Remove-Item 'tools\sdl\sdl.zip'; throw 'SHA-256 of the download does not match' }" || exit /b 1
tar -xf tools\sdl\sdl.zip -C tools\sdl || exit /b 1
del tools\sdl\sdl.zip
if not exist "%SDL%\lib\libSDL2.a" (echo The SDL archive did not contain %SDL%\lib\libSDL2.a & exit /b 1)
exit /b 0

:fail
echo.
echo Build FAILED.
exit /b 1
