@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cmake --build D:\BigheroGameEngine\out\build\x64-Release --config Release --target BigHeroGameEngine BigHeroTests 2>&1
