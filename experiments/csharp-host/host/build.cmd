@echo off
rem host\build.cmd -- build host.exe with MSVC cl (standalone spike, not wired
rem into the engine root build). Locates latest Visual Studio via vswhere,
rem loads vcvars64, then compiles.
setlocal enabledelayedexpansion

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [build.cmd] vswhere.exe not found: "%VSWHERE%"
    exit /b 1
)

set "VSDIR="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo [build.cmd] Visual Studio with VC tools not found
    exit /b 1
)
echo [build.cmd] Using VS: %VSDIR%

call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (
    echo [build.cmd] vcvars64.bat failed
    exit /b 1
)

cd /d "%~dp0"
cl /nologo /std:c++20 /O2 /EHsc /W4 /Fe:host.exe host.cpp
if errorlevel 1 (
    echo [build.cmd] cl compile failed
    exit /b 1
)
echo [build.cmd] Build OK: host\host.exe
exit /b 0
