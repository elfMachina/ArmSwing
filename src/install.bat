@echo off
setlocal

:: ArmSwing installer - registers the "armswing" folder as a SteamVR driver.

tasklist /fi "imagename eq vrserver.exe" 2>nul | find /i "vrserver.exe" >nul
if not errorlevel 1 (
    echo SteamVR is running. Close SteamVR, then run this again.
    pause
    exit /b 1
)

:: The driver folder must be named "armswing" and sit next to this script.
set "DRIVER=%~dp0armswing"

:: Files extracted loose next to this script: put them where they belong.
if not exist "%DRIVER%\bin\win64\driver_armswing.dll" if exist "%~dp0bin\win64\driver_armswing.dll" (
    echo Files were extracted loose - moving them into the "armswing" folder.
    if not exist "%DRIVER%" mkdir "%DRIVER%"
    move "%~dp0bin" "%DRIVER%\bin" >nul
    for %%F in ("%~dp0*.json" "%~dp0*.txt" "%~dp0*.exe" "%~dp0*.dll" "%~dp0*.log" "%~dp0*.vrmanifest" "%~dp0*.vrdrivermanifest") do move "%%~fF" "%DRIVER%\" >nul
)

if not exist "%DRIVER%\bin\win64\driver_armswing.dll" (
    echo ERROR: driver DLL not found at %DRIVER%\bin\win64\driver_armswing.dll
    echo This script must sit next to the "armswing" folder from the zip.
    pause
    exit /b 1
)

:: Find vrpathreg.exe: standard Steam paths first.
set "VRPATH=%LOCALAPPDATA%\openvr\openvrpaths.vrpath"
set "REG=%ProgramFiles(x86)%\Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe"
if exist "%REG%" goto :have_reg
set "REG=%ProgramFiles%\Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe"
if exist "%REG%" goto :have_reg

:: Steam library on another drive: ask SteamVR's own path file (read only).
if not exist "%VRPATH%" (
    echo ERROR: vrpathreg.exe not found in standard Steam paths and
    echo        %VRPATH% does not exist.
    echo Has SteamVR been installed and run at least once?
    pause
    exit /b 1
)
for /f "usebackq delims=" %%R in (`powershell -NoProfile -Command "(Get-Content '%VRPATH%' -Raw | ConvertFrom-Json).runtime[0].Replace('/','\')"`) do set "REG=%%R\bin\win64\vrpathreg.exe"
if not exist "%REG%" (
    echo ERROR: vrpathreg.exe not found at %REG%
    echo Find vrpathreg.exe in your SteamVR folder and run:
    echo   vrpathreg adddriver "%DRIVER%"
    pause
    exit /b 1
)

:have_reg
echo Found vrpathreg at: %REG%

:: Unregister older copies (other folders, or the old "handswing" name) so two
:: copies never load at once. Uses vrpathreg; the path file is only read.
if exist "%VRPATH%" for /f "usebackq delims=" %%D in (`powershell -NoProfile -Command "(Get-Content '%VRPATH%' -Raw | ConvertFrom-Json).external_drivers | Where-Object { $_ -and ('armswing','handswing' -contains (Split-Path $_ -Leaf)) }"`) do (
    if /i not "%%D"=="%DRIVER%" (
        echo Removing older registration: %%D
        "%REG%" removedriver "%%D"
    )
)

echo Registering driver at: %DRIVER%
echo.
"%REG%" adddriver "%DRIVER%"
if errorlevel 1 (
    echo.
    echo Registration failed.
    pause
    exit /b 1
)

echo.
echo Installed. Start SteamVR, then run armswing.exe inside the armswing folder.
echo If you move this folder later, run install.bat again from the new place.
echo To remove: run uninstall.bat (with SteamVR closed).
pause
