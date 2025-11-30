@echo off
REM Build SDL2 - run from scripts folder
REM SDL2 will be cloned/built in ..\..\SDL (sibling to webigeo)
REM Output is logged to scripts\build_sdl.log

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64

REM Get the directory where this script is located (scripts/)
set SCRIPT_DIR=%~dp0
REM Webigeo root directory
set WEBIGEO_DIR=%SCRIPT_DIR%..
REM External dependencies directory (sibling to webigeo)
set EXTERN_DIR=%SCRIPT_DIR%..\..
set SDL_INSTALL=%EXTERN_DIR%\SDL
set LOG_FILE=%SCRIPT_DIR%build_sdl.log

REM Start logging
echo Build started at %date% %time% > "%LOG_FILE%"

echo === Cloning SDL2 === >> "%LOG_FILE%" 2>&1
echo === Cloning SDL2 ===
cd /d "%EXTERN_DIR%"
if exist SDL_source rmdir /s /q SDL_source >> "%LOG_FILE%" 2>&1
git clone https://github.com/libsdl-org/SDL.git SDL_source >> "%LOG_FILE%" 2>&1
cd SDL_source
git checkout SDL2 >> "%LOG_FILE%" 2>&1

echo === Creating build directory === >> "%LOG_FILE%" 2>&1
echo === Creating build directory ===
mkdir build
cd build

echo === Configuring SDL2 === >> "%LOG_FILE%" 2>&1
echo === Configuring SDL2 ===
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="%SDL_INSTALL%" >> "%LOG_FILE%" 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: SDL2 configure failed >> "%LOG_FILE%"
    exit /b %ERRORLEVEL%
)

echo === Building SDL2 === >> "%LOG_FILE%" 2>&1
echo === Building SDL2 ===
ninja >> "%LOG_FILE%" 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: SDL2 build failed >> "%LOG_FILE%"
    exit /b %ERRORLEVEL%
)

echo === Installing SDL2 === >> "%LOG_FILE%" 2>&1
echo === Installing SDL2 ===
ninja install >> "%LOG_FILE%" 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: SDL2 install failed >> "%LOG_FILE%"
    exit /b %ERRORLEVEL%
)

echo === Cleanup === >> "%LOG_FILE%" 2>&1
echo === Cleanup ===
cd /d "%EXTERN_DIR%"
rmdir /s /q SDL_source >> "%LOG_FILE%" 2>&1

echo === SDL2 build complete! === >> "%LOG_FILE%" 2>&1
echo === SDL2 build complete! ===
echo Installed to: %SDL_INSTALL% >> "%LOG_FILE%" 2>&1
echo Installed to: %SDL_INSTALL%
echo Build finished at %date% %time% >> "%LOG_FILE%"
