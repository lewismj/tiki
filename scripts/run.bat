@echo off
rem Starts tiki from a Windows release package (tiki-<version>\scripts\run.bat), with the network
rem shipped in tiki-<version>\models. Arguments are passed on, e.g. 'run.bat bench'. Set
rem TIKI_EVALFILE to use another network, or TIKI_APP to use another binary.
setlocal
set "here=%~dp0.."

if not defined TIKI_EVALFILE (
    rem The last network by name, i.e. the newest with dated names (tiki-2026-11-05.nnue over
    rem tiki-2026-10-23.nnue).
    for /f "delims=" %%f in ('dir /b /on "%here%\models\*.nnue" 2^>nul') do set "TIKI_EVALFILE=%here%\models\%%f"
)
if not defined TIKI_EVALFILE (
    echo No network in %here%\models, set TIKI_EVALFILE to one. 1>&2
    exit /b 1
)

if not defined TIKI_APP set "TIKI_APP=%here%\bin\tiki.exe"
"%TIKI_APP%" %*
exit /b %errorlevel%
