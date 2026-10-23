@echo off
rem Starts the engine inside WSL, so a Windows GUI (Fritz, Cute Chess, ...) can load it as a UCI engine.
rem By default it runs the CLion release build with the last network in models\ by name (the newest with
rem dated names). Set TIKI_APP or TIKI_EVALFILE in the environment to override them, either may be a
rem Windows path (C:\...) or a WSL path (/mnt/c/...).

rem The repository, the parent of this script's folder.
for %%d in ("%~dp0..") do set "root=%%~fd"

if "%TIKI_APP%"=="" set "TIKI_APP=%root%\cmake-build-release-wsl-clang\engine\app\tiki_app"
if "%TIKI_EVALFILE%"=="" for %%f in ("%root%\models\*.nnue") do set "TIKI_EVALFILE=%%~ff"
if "%TIKI_EVALFILE%"=="" (
    echo No network in %root%\models, set TIKI_EVALFILE to one. 1>&2
    exit /b 1
)

call :to_wsl TIKI_APP
call :to_wsl TIKI_EVALFILE

rem Windows environment variables don't reach WSL by default, so pass the network's path explicitly.
wsl env TIKI_EVALFILE="%TIKI_EVALFILE%" "%TIKI_APP%"
exit /b %errorlevel%

rem Converts the variable named %1 from a Windows path to a WSL one (C:\x -> /mnt/c/x). A path without
rem a drive letter is left as it is. Backslashes become slashes first, WSL's shell would drop them.
:to_wsl
setlocal enabledelayedexpansion
set "value=!%1!"
if not "!value:~1,1!"==":" endlocal & exit /b 0
set "value=!value:\=/!"
for /f "delims=" %%p in ('wsl wslpath -u "!value!" ^< nul') do set "value=%%p"
endlocal & set "%1=%value%"
exit /b 0
