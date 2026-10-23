@echo off
rem Plays engine matches with fastchess on Windows (scripts/match.sh is the Linux, WSL and macOS version).
rem
rem     scripts\match.bat sprt <new engine> <base engine>
rem         SPRT: plays the new build against the base build until it's clear whether the new one is
rem         stronger, e.g. a feature branch's tiki_app.exe against main's.
rem     scripts\match.bat gauntlet <tiki> <engine>...
rem         Plays tiki against each engine for a fixed number of games and reports the Elo difference.
rem
rem fastchess (the latest release) and the opening book are downloaded into tools\ the first time.
rem Games and results go to matches\. Settings, from the environment (set NAME=value first):
rem     TC=8+0.08            time control, seconds+increment
rem     CONCURRENCY=N        games at once, default physical cores - 1
rem     HASH=16 THREADS=1    UCI options for every engine
rem     BOOK=<file>          openings (.epd or .pgn), default tools\books\8moves_v3.pgn
rem     GAMES=200            gauntlet: games against each engine
rem     ELO0=0 ELO1=5        SPRT bounds (normalized Elo)
rem     MAX_GAMES=20000      SPRT: stop here if it's still undecided
rem     TIKI_EVALFILE        network for every tiki, default the last in models\ by name
rem     NEW_EVALFILE BASE_EVALFILE  SPRT: a different network for one side (to test networks)
setlocal

for %%d in ("%~dp0..") do set "root=%%~fd"
set "tools=%root%\tools"
set "curl=%SystemRoot%\System32\curl.exe"
set "tar=%SystemRoot%\System32\tar.exe"

if not defined TC set TC=8+0.08
if not defined HASH set HASH=16
if not defined THREADS set THREADS=1
if not defined GAMES set GAMES=200
if not defined ELO0 set ELO0=0
if not defined ELO1 set ELO1=5
if not defined MAX_GAMES set MAX_GAMES=20000

set "mode=%~1"
if "%~3"=="" goto usage
if not "%mode%"=="sprt" if not "%mode%"=="gauntlet" goto usage

rem fastchess, the latest release.
set platform=windows-x86-64
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" set platform=windows-arm64
set "fastchess=%tools%\fastchess-%platform%\fastchess.exe"
if not exist "%fastchess%" (
    if not exist "%tools%" mkdir "%tools%"
    echo Downloading fastchess ^(%platform%^)
    "%curl%" -fsSL -o "%tools%\fastchess.zip" "https://github.com/Disservin/fastchess/releases/latest/download/fastchess-%platform%.zip" || exit /b 1
    "%tar%" -xf "%tools%\fastchess.zip" -C "%tools%" "fastchess-%platform%/fastchess.exe" "fastchess-%platform%/LICENSE" || exit /b 1
    del "%tools%\fastchess.zip"
)

rem The opening book.
if defined BOOK (
    set "book=%BOOK%"
) else (
    set "book=%tools%\books\8moves_v3.pgn"
)
if not defined BOOK if not exist "%book%" (
    if not exist "%tools%\books" mkdir "%tools%\books"
    echo Downloading the opening book
    "%curl%" -fsSL -o "%tools%\books\book.zip" "https://github.com/official-stockfish/books/raw/master/8moves_v3.pgn.zip" || exit /b 1
    "%tar%" -xf "%tools%\books\book.zip" -C "%tools%\books" || exit /b 1
    del "%tools%\books\book.zip"
)
if not exist "%book%" (
    echo No opening book at %book% 1>&2
    exit /b 1
)
set book_format=pgn
if /i "%book:~-4%"==".epd" set book_format=epd

rem tiki loads TIKI_EVALFILE at startup, fastchess passes the environment on to the engines.
if not defined TIKI_EVALFILE for %%f in ("%root%\models\*.nnue") do set "TIKI_EVALFILE=%%~ff"

if not defined CONCURRENCY (
    for /f %%c in ('powershell -NoProfile -Command "(Get-CimInstance Win32_Processor | Measure-Object NumberOfCores -Sum).Sum"') do set /a CONCURRENCY=%%c - 1
)
if not defined CONCURRENCY set /a CONCURRENCY=%NUMBER_OF_PROCESSORS% / 2 - 1
if %CONCURRENCY% lss 1 set CONCURRENCY=1

if not exist "%root%\matches" mkdir "%root%\matches"
for /f %%t in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd-HHmmss"') do set "name=%mode%-%%t"

set engines=
if "%mode%"=="sprt" goto sprt

rem Gauntlet: tiki, then each opponent.
call :engine_path "%~2" || exit /b 1
set "tiki=%path_found%"
set engines=-tournament gauntlet -seeds 1 -engine "cmd=%tiki%" name=tiki
set /a rounds=GAMES / 2
set "summary=Gauntlet: %tiki%, %GAMES% games against each engine"
shift
shift
:next_opponent
if "%~1"=="" goto run
call :engine_path "%~1" || exit /b 1
call :add_opponent "%path_found%"
shift
goto next_opponent

:sprt
if not "%~4"=="" goto usage
call :engine_path "%~2" || exit /b 1
set "new=%path_found%"
call :engine_path "%~3" || exit /b 1
set "base=%path_found%"
set engines=-engine "cmd=%new%" name=new
if defined NEW_EVALFILE set engines=%engines% "option.EvalFile=%NEW_EVALFILE%"
set engines=%engines% -engine "cmd=%base%" name=base
if defined BASE_EVALFILE set engines=%engines% "option.EvalFile=%BASE_EVALFILE%"
set /a rounds=MAX_GAMES / 2
set engines=%engines% -sprt elo0=%ELO0% elo1=%ELO1% alpha=0.05 beta=0.05 model=normalized
set "summary=SPRT [%ELO0%, %ELO1%]: %new% vs %base%"

:run
echo %summary%, %CONCURRENCY% at once, network %TIKI_EVALFILE%
"%fastchess%" %engines% -rounds %rounds% ^
    -openings "file=%book%" format=%book_format% order=random -repeat ^
    -each tc=%TC% option.Hash=%HASH% option.Threads=%THREADS% ^
    -concurrency %CONCURRENCY% -recover ^
    -draw movenumber=40 movecount=8 score=10 -resign movecount=3 score=600 twosided=true ^
    -pgnout "file=%root%\matches\%name%.pgn" -config "outname=%root%\matches\%name%.json"
exit /b %errorlevel%

rem Sets path_found to the full path of engine %1, which must exist.
:engine_path
if not exist "%~f1" (
    echo No engine at %~1 1>&2
    exit /b 1
)
set "path_found=%~f1"
exit /b 0

rem Adds an opponent at path %1, named after its file.
:add_opponent
set engines=%engines% -engine "cmd=%~1" "name=%~n1"
exit /b 0

:usage
for /f "tokens=1* delims= " %%a in ('findstr /b /c:"rem     scripts" "%~f0"') do echo   %%b
exit /b 1
