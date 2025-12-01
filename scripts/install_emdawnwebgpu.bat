@echo off
REM Install emdawnwebgpu package - run from scripts folder
REM Package will be extracted to ..\..\emdawnwebgpu (sibling to webigeo)
REM Source: https://github.com/google/dawn/releases

REM Get the directory where this script is located (scripts/)
set SCRIPT_DIR=%~dp0
REM External dependencies directory (sibling to webigeo)
set EXTERN_DIR=%SCRIPT_DIR%..\..
set EMDAWN_DIR=%EXTERN_DIR%\emdawnwebgpu
set EMDAWN_VERSION=v20251001.122215
set EMDAWN_URL=https://github.com/google/dawn/releases/download/%EMDAWN_VERSION%/emdawnwebgpu_pkg-%EMDAWN_VERSION%.zip
set EMDAWN_ZIP=%EXTERN_DIR%\emdawnwebgpu_pkg-%EMDAWN_VERSION%.zip

echo === Installing emdawnwebgpu %EMDAWN_VERSION% ===

REM Check if already extracted
if exist "%EMDAWN_DIR%\emdawnwebgpu_pkg\emdawnwebgpu.port.py" (
    echo emdawnwebgpu already installed at %EMDAWN_DIR%
    echo To reinstall, delete %EMDAWN_DIR% and run this script again.
    goto :done
)

REM Create directory
if not exist "%EMDAWN_DIR%" mkdir "%EMDAWN_DIR%"

REM Download if zip doesn't exist
if not exist "%EMDAWN_ZIP%" (
    echo Downloading from GitHub...
    curl -L -o "%EMDAWN_ZIP%" "%EMDAWN_URL%"
    if %ERRORLEVEL% NEQ 0 (
        echo ERROR: Download failed
        exit /b %ERRORLEVEL%
    )
)

REM Extract
echo Extracting to %EMDAWN_DIR%...
cd /d "%EXTERN_DIR%"
tar -xf "%EMDAWN_ZIP%" -C "%EMDAWN_DIR%"
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Extraction failed
    exit /b %ERRORLEVEL%
)

REM Verify
if exist "%EMDAWN_DIR%\emdawnwebgpu_pkg\emdawnwebgpu.port.py" (
    echo.
    echo === emdawnwebgpu installed successfully! ===
    echo Location: %EMDAWN_DIR%\emdawnwebgpu_pkg\emdawnwebgpu.port.py
) else (
    echo ERROR: Installation verification failed
    exit /b 1
)

:done
echo.
echo To use, add to CMake/Emscripten:
echo   --use-port=%EMDAWN_DIR%\emdawnwebgpu_pkg\emdawnwebgpu.port.py
