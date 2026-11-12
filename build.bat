@echo off
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`call "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (echo Visual Studio with C++ tools not found & exit /b 1)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul || exit /b 1

cd /d "%~dp0"
if not exist build mkdir build
set CFLAGS=/nologo /std:c++17 /O2 /EHsc /W3 /MT /utf-8 /DUNICODE /D_UNICODE /Isrc

cl %CFLAGS% /Fobuild\ /Fe:build\test_dan.exe tests\test_dan.cpp src\core\dan.cpp || exit /b 1
build\test_dan.exe || exit /b 1

cl %CFLAGS% /Fobuild\ /Fe:build\DanTracker.exe src\main.cpp src\app\overlay_app.cpp src\app\settings.cpp src\app\shared.cpp src\ui\canvas.cpp src\ui\overlay_view.cpp src\net\tosu_client.cpp src\obs\obs_server.cpp src\obs\obs_state.cpp src\core\dan.cpp /link /SUBSYSTEM:WINDOWS || exit /b 1
if not exist build\dans.json copy config\dans.json build\dans.json >nul
echo.
echo Done: build\DanTracker.exe
