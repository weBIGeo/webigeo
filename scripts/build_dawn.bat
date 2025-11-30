@echo off
REM Build Dawn (WebGPU) - run from scripts folder
REM Dawn will be cloned/built in ..\..\dawn (sibling to webigeo)
REM Output is logged to scripts\build_dawn.log

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64

REM Get the directory where this script is located (scripts/)
set SCRIPT_DIR=%~dp0
REM Webigeo root directory
set WEBIGEO_DIR=%SCRIPT_DIR%..
REM External dependencies directory (sibling to webigeo)
set EXTERN_DIR=%SCRIPT_DIR%..\..
set DAWN_DIR=%EXTERN_DIR%\dawn
set LOG_FILE=%SCRIPT_DIR%build_dawn.log

REM Start logging
echo Build started at %date% %time% > "%LOG_FILE%"

REM Clone Dawn if not exists
if not exist "%DAWN_DIR%" (
    echo === Cloning Dawn === >> "%LOG_FILE%" 2>&1
    echo === Cloning Dawn ===
    cd /d "%EXTERN_DIR%"
    git clone -b chromium/7548 https://dawn.googlesource.com/dawn >> "%LOG_FILE%" 2>&1
)

cd /d "%DAWN_DIR%"

echo === Configuring Dawn Debug Build === >> "%LOG_FILE%" 2>&1
echo === Configuring Dawn Debug Build ===
cmake -GNinja -S . -B out/Debug -DDAWN_BUILD_MONOLITHIC_LIBRARY=STATIC -DDAWN_FETCH_DEPENDENCIES=ON -DDAWN_ENABLE_INSTALL=ON -DCMAKE_BUILD_TYPE=Debug -DTINT_BUILD_SPV_READER=OFF -DTINT_BUILD_TESTS=OFF -DTINT_BUILD_FUZZERS=OFF -DTINT_BUILD_BENCHMARKS=OFF -DTINT_BUILD_AS_OTHER_OS=OFF -DDAWN_BUILD_SAMPLES=OFF -DDAWN_ENABLE_D3D11=OFF -DDAWN_ENABLE_D3D12=OFF -DDAWN_ENABLE_METAL=OFF -DDAWN_ENABLE_NULL=OFF -DDAWN_ENABLE_DESKTOP_GL=OFF -DDAWN_ENABLE_OPENGLES=OFF -DDAWN_ENABLE_VULKAN=ON -DDAWN_USE_WINDOWS_UI=OFF -DDAWN_USE_GLFW=OFF -DDAWN_FORCE_SYSTEM_COMPONENT_LOAD=ON >> "%LOG_FILE%" 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Debug configure failed >> "%LOG_FILE%"
    exit /b %ERRORLEVEL%
)

echo === Building Dawn Debug === >> "%LOG_FILE%" 2>&1
echo === Building Dawn Debug ===
cmake --build out/Debug >> "%LOG_FILE%" 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Debug build failed >> "%LOG_FILE%"
    exit /b %ERRORLEVEL%
)

echo === Installing Dawn Debug === >> "%LOG_FILE%" 2>&1
echo === Installing Dawn Debug ===
cmake --install out/Debug --prefix install/Debug >> "%LOG_FILE%" 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Debug install failed >> "%LOG_FILE%"
    exit /b %ERRORLEVEL%
)

echo === Configuring Dawn Release Build === >> "%LOG_FILE%" 2>&1
echo === Configuring Dawn Release Build ===
cmake -GNinja -S . -B out/Release -DDAWN_BUILD_MONOLITHIC_LIBRARY=STATIC -DDAWN_FETCH_DEPENDENCIES=ON -DDAWN_ENABLE_INSTALL=ON -DCMAKE_BUILD_TYPE=Release -DTINT_BUILD_SPV_READER=OFF -DTINT_BUILD_TESTS=OFF -DTINT_BUILD_FUZZERS=OFF -DTINT_BUILD_BENCHMARKS=OFF -DTINT_BUILD_AS_OTHER_OS=OFF -DDAWN_BUILD_SAMPLES=OFF -DDAWN_ENABLE_D3D11=OFF -DDAWN_ENABLE_D3D12=OFF -DDAWN_ENABLE_METAL=OFF -DDAWN_ENABLE_NULL=OFF -DDAWN_ENABLE_DESKTOP_GL=OFF -DDAWN_ENABLE_OPENGLES=OFF -DDAWN_ENABLE_VULKAN=ON -DDAWN_USE_WINDOWS_UI=OFF -DDAWN_USE_GLFW=OFF -DDAWN_FORCE_SYSTEM_COMPONENT_LOAD=ON >> "%LOG_FILE%" 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Release configure failed >> "%LOG_FILE%"
    exit /b %ERRORLEVEL%
)

echo === Building Dawn Release === >> "%LOG_FILE%" 2>&1
echo === Building Dawn Release ===
cmake --build out/Release >> "%LOG_FILE%" 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Release build failed >> "%LOG_FILE%"
    exit /b %ERRORLEVEL%
)

echo === Installing Dawn Release === >> "%LOG_FILE%" 2>&1
echo === Installing Dawn Release ===
cmake --install out/Release --prefix install/Release >> "%LOG_FILE%" 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Release install failed >> "%LOG_FILE%"
    exit /b %ERRORLEVEL%
)

echo === Dawn build complete! === >> "%LOG_FILE%" 2>&1
echo === Dawn build complete! ===
echo Installed to: %DAWN_DIR%\install >> "%LOG_FILE%" 2>&1
echo Installed to: %DAWN_DIR%\install
echo Build finished at %date% %time% >> "%LOG_FILE%"
