@echo off
setlocal enabledelayedexpansion

echo ========================================================
echo   LiteProcManager (C++ Native) Build Script
echo ========================================================

set "ROOT=%~dp0"
set ISCC="C:\Program Files\Inno Setup 7\ISCC.exe"

cd /d "%~dp0"

:: 1. Configuration Selection (Default: Release)
set "CONFIG=Release"

echo [*] Target Configuration: %CONFIG%
echo [*] Platform: x64

:: 2. Find and setup Visual Studio Developer Environment
where msbuild >nul 2>nul
if %errorlevel% neq 0 (
    echo [*] Locating Visual Studio Developer Command Prompt...
    
    set "VS_DEV_CMD="
    
    :: Check VS2026 (v18)
    if exist "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" (
        set "VS_DEV_CMD=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat" (
        set "VS_DEV_CMD=C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\Tools\VsDevCmd.bat" (
        set "VS_DEV_CMD=C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\Tools\VsDevCmd.bat"
    )
    
    if "!VS_DEV_CMD!"=="" (
        echo [ERROR] Visual Studio Developer Command Prompt could not be found.
        echo Please ensure Visual Studio 2026 is installed.
        exit /b 1
    )

    echo [*] Initializing environment with: "!VS_DEV_CMD!"
    call "!VS_DEV_CMD!" -arch=x64 >nul 2>&1
    
    where msbuild >nul 2>nul
    if !errorlevel! neq 0 (
        echo [ERROR] Failed to initialize MSBuild environment.
        exit /b 1
    )
)

:: 3. Stop running LiteProcManager instance if any (to avoid file lock)
powershell -Command "Get-Process -Name 'LiteProcManager*','ProcessManager*' -ErrorAction SilentlyContinue | Stop-Process -Force" >nul 2>&1

:: 4. Build Solution via MSBuild
echo [*] Building LiteProcManager.sln [%CONFIG% ^| x64]...
msbuild "LiteProcManager.sln" /p:Configuration=%CONFIG% /p:Platform=x64 /nologo /verbosity:minimal /m
if %errorlevel% neq 0 (
    echo.
    echo [ERROR] Build FAILED with error code %errorlevel%.
    exit /b %errorlevel%
)

echo.
echo [SUCCESS] Build completed successfully!
echo [EXE Output]: %~dp0bin\%CONFIG%\LiteProcManager.exe

:: 5. Run Unit Tests via vstest.console.exe (CppUnitTestFramework)
echo.
echo ========================================================
echo   Running Unit Tests (%CONFIG%) via vstest.console.exe
echo ========================================================
set "TEST_DLL=%~dp0bin\%CONFIG%\LiteProcManagerTests.dll"
if exist "!TEST_DLL!" (
    where vstest.console.exe >nul 2>nul
    if !errorlevel! equ 0 (
        vstest.console.exe "!TEST_DLL!" --logger:"Console;verbosity=normal"
        if !errorlevel! neq 0 (
            echo.
            echo [ERROR] Unit Tests FAILED!
            exit /b !errorlevel!
        )
    ) else (
        echo [WARN] vstest.console.exe not found in PATH. Visual Studio Test Explorer can run this DLL directly.
    )
) else if exist "bin\%CONFIG%\ProcessManagerTests.exe" (
    "bin\%CONFIG%\ProcessManagerTests.exe"
    if !errorlevel! neq 0 (
        echo.
        echo [ERROR] Unit Tests FAILED!
        exit /b !errorlevel!
    )
) else (
    echo [WARN] Test binary not found at !TEST_DLL!
)

echo ========================================================
echo   Building Inno Setup installer
echo ========================================================
%ISCC% "%ROOT%installer\installer.iss"
if errorlevel 1 (
    echo [ERROR] Inno Setup compilation failed!
    exit /b 1
)

if /I "%1"=="all" (
	pushd "%ROOT%dist"
		.\LiteProcManagerSetup.exe /SILENT
	popd
)

echo.
echo ===================================================
echo  SUCCESS ^(%DATE% %TIME%^)
echo ===================================================

exit /b 0
