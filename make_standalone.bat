@echo off
rem make_standalone.bat - one exe with everything inside (A2RacerRE-standalone.exe): A2RacerRE.exe + YOUR copy of the game
rem files. It unpacks itself to %LOCALAPPDATA%\A2RacerRE at its first start. Because it contains the game's files it is for
rem your own use only: never share or upload it. Start A2RacerRE.exe once before, so the game files have been copied.
call "%~dp0build.bat" standalone
if errorlevel 1 pause
