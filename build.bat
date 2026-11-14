@echo off
if not exist bin mkdir bin

where cl.exe >nul 2>nul
if %ERRORLEVEL% equ 0 goto compile_msvc

if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" 10.0.22621.0 -vcvars_ver=14.44.35207 >nul
    goto compile_msvc
)

where gcc.exe >nul 2>nul
if %ERRORLEVEL% equ 0 goto compile_gcc

where clang.exe >nul 2>nul
if %ERRORLEVEL% equ 0 goto compile_clang

echo [error] No C17 compiler found.
exit /b 1

:compile_msvc
echo Compiling with cl.exe (MSVC C17)...
cl /nologo /std:c17 /O2 /W4 /Iinclude src\common.c src\platform.c src\json.c src\workspace.c src\analyzer.c src\main.c /Fe:bin\strict-error-analyser.exe urlmon.lib
goto finish

:compile_gcc
echo Compiling with gcc (C17)...
gcc -std=c17 -O2 -Wall -Iinclude src/common.c src/platform.c src/json.c src/workspace.c src/analyzer.c src/main.c -o bin/strict-error-analyser.exe -lurlmon
goto finish

:compile_clang
echo Compiling with clang (C17)...
clang -std=c17 -O2 -Wall -Iinclude src/common.c src/platform.c src/json.c src/workspace.c src/analyzer.c src/main.c -o bin/strict-error-analyser.exe -lurlmon
goto finish

:finish
if exist *.obj del *.obj >nul 2>nul
if exist bin\strict-error-analyser.exe (
    echo Build succeeded: bin\strict-error-analyser.exe
    exit /b 0
) else (
    echo Build failed.
    exit /b 1
)
