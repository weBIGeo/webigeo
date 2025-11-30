@echo off
REM Build weBIGeo Native - run from scripts folder
REM Expects Dawn and SDL2 as siblings to webigeo (..\..\dawn, ..\..\SDL)
REM Output is logged to scripts\build_webigeo_native.log

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64

REM Get the directory where this script is located (scripts/)
set SCRIPT_DIR=%~dp0
REM Webigeo root directory
set WEBIGEO_DIR=%SCRIPT_DIR%..
REM External dependencies directory (sibling to webigeo)
set EXTERN_DIR=%SCRIPT_DIR%..\..
set LOG_FILE=%SCRIPT_DIR%build_webigeo_native.log

set ALP_DAWN_DIR=%EXTERN_DIR%\dawn
set SDL2_DIR=%EXTERN_DIR%\SDL\cmake
set Qt6_DIR=C:\Qt\6.10.1\msvc2022_64

cd /d "%WEBIGEO_DIR%"

REM Start logging
echo Build started at %date% %time% > "%LOG_FILE%"

echo === Configuring weBIGeo === >> "%LOG_FILE%" 2>&1
echo === Configuring weBIGeo ===
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=%Qt6_DIR% -DALP_DAWN_DIR=%ALP_DAWN_DIR% -DSDL2_DIR=%SDL2_DIR% -DALP_ENABLE_POSITIONING=OFF -DALP_GL_ENGINE=OFF >> "%LOG_FILE%" 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: weBIGeo configure failed >> "%LOG_FILE%"
    exit /b %ERRORLEVEL%
)

echo === Building weBIGeo === >> "%LOG_FILE%" 2>&1
echo === Building weBIGeo ===
cmake --build build >> "%LOG_FILE%" 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: weBIGeo build failed >> "%LOG_FILE%"
    exit /b %ERRORLEVEL%
)

echo === weBIGeo build complete! === >> "%LOG_FILE%" 2>&1
echo === weBIGeo build complete! ===
echo Output: %WEBIGEO_DIR%\build >> "%LOG_FILE%" 2>&1
echo Output: %WEBIGEO_DIR%\build
echo Build finished at %date% %time% >> "%LOG_FILE%"
