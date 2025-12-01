@echo off
REM Build weBIGeo Web (Emscripten/WASM) - run from scripts folder
REM Expects emsdk as sibling to webigeo (..\..\emsdk)
REM Output is logged to scripts\build_webigeo_web.log

REM Get the directory where this script is located (scripts/)
set SCRIPT_DIR=%~dp0
REM Webigeo root directory
set WEBIGEO_DIR=%SCRIPT_DIR%..
REM External dependencies directory (sibling to webigeo)
set EXTERN_DIR=%SCRIPT_DIR%..\..
set LOG_FILE=%SCRIPT_DIR%build_webigeo_web.log

REM Qt 6.10.1 WASM path (adjust if needed)
set Qt6_DIR=C:\Qt\6.10.1\wasm_singlethread

REM Qt host path (native Qt for cross-compilation tools)
set Qt6_HOST_DIR=C:\Qt\6.10.1\msvc2022_64

REM Emscripten SDK path (sibling to webigeo)
set EMSDK=%EXTERN_DIR%\emsdk

REM Activate Emscripten
call "%EMSDK%\emsdk_env.bat"

cd /d "%WEBIGEO_DIR%"

REM Start logging
echo Build started at %date% %time% > "%LOG_FILE%"
echo Using Qt: %Qt6_DIR% >> "%LOG_FILE%"
echo Using Emscripten: %EMSDK% >> "%LOG_FILE%"

echo === Configuring weBIGeo Web === >> "%LOG_FILE%" 2>&1
echo === Configuring weBIGeo Web ===
REM Use Qt's toolchain file - it chainloads Emscripten and sets up Qt paths correctly
cmake -B build_web -G Ninja ^
    -DCMAKE_TOOLCHAIN_FILE="%Qt6_DIR%/lib/cmake/Qt6/qt.toolchain.cmake" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DQT_HOST_PATH="%Qt6_HOST_DIR%" ^
    -DALP_ENABLE_POSITIONING=OFF ^
    -DALP_ENABLE_GL_ENGINE=OFF ^
    -DALP_GL_ENGINE=OFF ^
    -DALP_PLAIN_RENDERER=OFF ^
    -DALP_QML_APP=OFF ^
    -DALP_ENABLE_THREADING=ON ^
    -DALP_UNITTESTS=OFF >> "%LOG_FILE%" 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: weBIGeo Web configure failed >> "%LOG_FILE%"
    echo ERROR: weBIGeo Web configure failed - check %LOG_FILE%
    exit /b %ERRORLEVEL%
)

echo === Building weBIGeo Web === >> "%LOG_FILE%" 2>&1
echo === Building weBIGeo Web ===
REM Use -j1 (single job) to avoid Windows permission issues during parallel compilation
cmake --build build_web -j1 >> "%LOG_FILE%" 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: weBIGeo Web build failed >> "%LOG_FILE%"
    echo ERROR: weBIGeo Web build failed - check %LOG_FILE%
    exit /b %ERRORLEVEL%
)

echo === weBIGeo Web build complete! === >> "%LOG_FILE%" 2>&1
echo === weBIGeo Web build complete! ===
echo Output: %WEBIGEO_DIR%\build_web >> "%LOG_FILE%" 2>&1
echo Output: %WEBIGEO_DIR%\build_web
echo Build finished at %date% %time% >> "%LOG_FILE%"

echo.
echo To serve locally, run:
echo   cd %WEBIGEO_DIR%\build_web
echo   python -m http.server 8000
echo Then open: http://localhost:8000/webgpu_app.html
