@echo off
setlocal

:: ArmSwing uninstaller — removes the SteamVR driver registration.
:: Close SteamVR before running.

set "DRIVER=%~dp0armswing"

:: Try the standard Steam install path first.
set "REG=%ProgramFiles(x86)%\Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe"
if exist "%REG%" goto :have_reg

set "REG=%ProgramFiles%\Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe"
if exist "%REG%" goto :have_reg

:: Fall back to parsing openvrpaths.vrpath with PowerShell.
set "VRPATH=%LOCALAPPDATA%\openvr\openvrpaths.vrpath"
if not exist "%VRPATH%" (
    echo ERROR: vrpathreg.exe not found in standard Steam paths.
    pause
    exit /b 1
)
for /f "usebackq delims=" %%R in (`powershell -NoProfile -Command ^
    "(Get-Content '%VRPATH%' -Raw | ConvertFrom-Json).runtime[0].Replace('/','\')"`) do (
    set "REG=%%R\bin\win64\vrpathreg.exe"
)
if not exist "%REG%" (
    echo ERROR: vrpathreg.exe not found at %REG%
    echo Please run manually:  vrpathreg removedriver "%DRIVER%"
    pause
    exit /b 1
)

:have_reg
echo Found vrpathreg at: %REG%
echo Removing driver at: %DRIVER%
echo.
"%REG%" removedriver "%DRIVER%"

echo.
echo Done. The armswing folder is still on disk if you want to reinstall later.
pause
