@echo off
REM Compile le projet (Windows, Visual Studio / MSVC ou MinGW)
cmake -S . -B build
if errorlevel 1 exit /b 1
cmake --build build --config Release
if errorlevel 1 exit /b 1
echo.
echo OK -^> lancez :  build\Release\enchanted_forest.exe
