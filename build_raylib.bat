@echo off
rem ============================================================
rem  Build script for snake_raylib.c
rem  Edit RAYLIB below if your raylib is installed elsewhere.
rem ============================================================
setlocal

set RAYLIB=D:\raylib-6.2_win64_mingw-w64

if not exist "%RAYLIB%\include\raylib.h" (
    echo [ERROR] raylib not found at: %RAYLIB%
    echo         Please edit RAYLIB in this file.
    pause
    exit /b 1
)

echo [1/2] compiling...
gcc snake_raylib.c -o snake_raylib.exe -O2 -std=c99 -Wall -Wextra ^
    -I"%RAYLIB%\include" -L"%RAYLIB%\lib" ^
    -lraylibdll -lopengl32 -lgdi32 -lwinmm

if errorlevel 1 (
    echo [ERROR] compile failed
    pause
    exit /b 1
)

echo [2/2] copying raylib.dll...
copy /Y "%RAYLIB%\lib\raylib.dll" . >nul

echo [OK] snake_raylib.exe is ready. Double click it to play.
pause
