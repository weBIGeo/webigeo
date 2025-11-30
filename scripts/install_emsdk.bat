@echo off
REM Install/Setup Emscripten SDK - run from scripts folder
REM emsdk will be cloned/installed in ..\..\emsdk (sibling to webigeo)
REM Output is logged to scripts\build_emsdk.log

REM Get the directory where this script is located (scripts/)
set SCRIPT_DIR=%~dp0
REM Webigeo root directory
set WEBIGEO_DIR=%SCRIPT_DIR%..
REM External dependencies directory (sibling to webigeo)
set EXTERN_DIR=%SCRIPT_DIR%..\..
set EMSDK_DIR=%EXTERN_DIR%\emsdk
set LOG_FILE=%SCRIPT_DIR%build_emsdk.log

REM Start logging
echo Build started at %date% %time% > "%LOG_FILE%"

REM Clone emsdk if not exists
if not exist "%EMSDK_DIR%" (
    echo === Cloning emsdk === >> "%LOG_FILE%" 2>&1
    echo === Cloning emsdk ===
    cd /d "%EXTERN_DIR%"
    git clone https://github.com/emscripten-core/emsdk.git >> "%LOG_FILE%" 2>&1
    if %ERRORLEVEL% NEQ 0 (
        echo ERROR: emsdk clone failed >> "%LOG_FILE%"
        exit /b %ERRORLEVEL%
    )
)

cd /d "%EMSDK_DIR%"

set EMSDK_VERSION=4.0.14

echo === Installing Emscripten %EMSDK_VERSION% === >> "%LOG_FILE%" 2>&1
echo === Installing Emscripten %EMSDK_VERSION% ===
call emsdk install %EMSDK_VERSION% >> "%LOG_FILE%" 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: emsdk install failed >> "%LOG_FILE%"
    exit /b %ERRORLEVEL%
)

echo === Activating Emscripten %EMSDK_VERSION% === >> "%LOG_FILE%" 2>&1
echo === Activating Emscripten %EMSDK_VERSION% ===
call emsdk activate %EMSDK_VERSION% >> "%LOG_FILE%" 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: emsdk activate failed >> "%LOG_FILE%"
    exit /b %ERRORLEVEL%
)

echo === emsdk setup complete! === >> "%LOG_FILE%" 2>&1
echo === emsdk setup complete! ===
echo Installed to: %EMSDK_DIR% >> "%LOG_FILE%" 2>&1
echo Installed to: %EMSDK_DIR%
echo Build finished at %date% %time% >> "%LOG_FILE%"

echo.
echo To use in a new terminal, run:
echo   call "%EMSDK_DIR%\emsdk_env.bat"
