@echo off
rem Builds tiki (Release) on Windows with LLVM clang targeting the MSVC ABI (see 'Windows' in the
rem main README). Finds clang in Program Files\LLVM and CMake and Ninja in Visual Studio when they
rem aren't on the PATH.
rem     scripts\build-windows.bat [options] [-- cmake options...]
rem     scripts\build-windows.bat --embed --test
rem     scripts\build-windows.bat --variant avx2 -- -DTIKI_SANITIZE=address
rem Every run sets the options below, so a build directory doesn't keep them from an earlier run.
setlocal EnableExtensions EnableDelayedExpansion

for %%i in ("%~dp0..") do set "root=%%~fi"
set "dir=build-windows"
set "variant=native"
set "embed=OFF"
set "net="
set "benchmarks=OFF"
set "test=0"
set "clean=0"

rem CMake options after ' -- ', taken from the whole command line: cmd splits arguments at '=',
rem which would break -DNAME=VALUE.
set "args= %*"
set "extra="
if not "!args: -- =!"=="!args!" set "extra=!args:* -- =!"

:parse
if "%~1"=="" goto parsed
if "%~1"=="--" goto parsed
if /i "%~1"=="--embed" (set "embed=ON" & shift & goto parse)
if /i "%~1"=="--net" (
    if "%~2"=="" goto usage_error
    set "embed=ON"
    set "net=%~f2"
    shift & shift & goto parse
)
if /i "%~1"=="--variant" (
    if "%~2"=="" goto usage_error
    set "variant=%~2"
    shift & shift & goto parse
)
if /i "%~1"=="--benchmarks" (set "benchmarks=ON" & shift & goto parse)
if /i "%~1"=="--test" (set "test=1" & shift & goto parse)
if /i "%~1"=="--clean" (set "clean=1" & shift & goto parse)
if /i "%~1"=="--dir" (
    if "%~2"=="" goto usage_error
    set "dir=%~2"
    shift & shift & goto parse
)
if /i "%~1"=="-h" goto usage
if /i "%~1"=="--help" goto usage
echo Unknown option: %~1 1>&2
goto usage_error

:parsed
if /i "%variant%"=="native" (set "cpu=-UTIKI_USE_AVX2 -UTIKI_USE_PEXT") else ^
if /i "%variant%"=="bmi2" (set "cpu=-DTIKI_USE_AVX2=ON -DTIKI_USE_PEXT=ON") else ^
if /i "%variant%"=="avx2" (set "cpu=-DTIKI_USE_AVX2=ON -DTIKI_USE_PEXT=OFF") else ^
if /i "%variant%"=="x86-64" (set "cpu=-DTIKI_USE_AVX2=OFF -DTIKI_USE_PEXT=OFF") else (
    echo Unknown variant: %variant% 1>&2
    goto usage_error
)

rem The build directory, relative to the repository unless it's absolute.
set "build=%root%\%dir%"
if not "%dir:~1,1%"==":" goto build_set
set "build=%dir%"
:build_set
if "%clean%"=="1" if exist "%build%" rmdir /s /q "%build%"

rem Tools: clang from LLVM's installer, CMake and Ninja from Visual Studio, unless already on the PATH.
set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "llvm=%ProgramFiles%\LLVM\bin"
where clang >nul 2>&1 || if exist "%llvm%\clang.exe" set "PATH=%llvm%;%PATH%"
where clang >nul 2>&1 || (echo clang not found: install LLVM ^(winget install LLVM.LLVM^) 1>&2 & exit /b 1)
where cmake >nul 2>&1 || call :from_vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
where cmake >nul 2>&1 || (echo cmake not found: install CMake or Visual Studio's C++ CMake tools 1>&2 & exit /b 1)
where ninja >nul 2>&1 || call :from_vs "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
where ninja >nul 2>&1 || (echo ninja not found: install Ninja or Visual Studio's C++ CMake tools 1>&2 & exit /b 1)
rem The bench smoke test runs through sh, Git for Windows has one.
where sh >nul 2>&1 || if exist "%ProgramFiles%\Git\bin\sh.exe" set "PATH=%PATH%;%ProgramFiles%\Git\bin"

cmake -S "%root%" -B "%build%" -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ %cpu% ^
      -DTIKI_EMBED_NETWORK=%embed% "-DTIKI_EMBED_NETWORK_FILE=%net%" ^
      -DTIKI_BUILD_BENCHMARKS=%benchmarks% %extra% || exit /b 1
cmake --build "%build%" || exit /b 1
if "%test%"=="1" (
    pushd "%build%"
    ctest -LE slow --output-on-failure
    set "result=!errorlevel!"
    popd
    if not "!result!"=="0" exit /b !result!
)

echo.
echo Built %build%\engine\app\tiki.exe
if "%embed%"=="ON" (
    echo The network is built in, it runs on its own ^(TIKI_EVALFILE still overrides it^).
) else (
    echo Set TIKI_EVALFILE to a .nnue file to run it ^(or build with --embed^).
)
exit /b 0

rem Adds the directory of a file in the newest Visual Studio to the PATH, if it's there.
:from_vs
if not exist "%vswhere%" exit /b 0
rem The outer quotes are removed by cmd, the inner ones are kept. !vswhere! as its "(x86)" would end
rem the parenthesis expanded early.
for /f "usebackq delims=" %%i in (`""!vswhere!" -latest -products * -find "%~1""`) do set "PATH=%%~dpi;!PATH!"
exit /b 0

:usage_error
call :usage 1>&2
exit /b 2

:usage
echo Usage: scripts\build-windows.bat [options] [-- cmake options...]
echo   --embed           Build the newest network in models\ into the engine (runs without a .nnue file).
echo   --net ^<file^>      Build this network into the engine (implies --embed).
echo   --variant ^<v^>     native (default, what this machine supports), bmi2 (AVX2 and PEXT), avx2 (AVX2,
echo                     no PEXT, for AMD Zen 1/2) or x86-64 (neither), as the release packages.
echo   --benchmarks      Also build the Google Benchmark micro-benchmarks.
echo   --test            Run the unit tests and bench smoke test after building.
echo   --clean           Delete the build directory first.
echo   --dir ^<dir^>       Build directory (default build-windows, relative to the repository).
echo   -h, --help        This help.
exit /b 0
