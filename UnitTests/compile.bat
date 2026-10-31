@echo off
setlocal
cmake -S "%~dp0" -B "%~dp0..\build\cpu-mingw" -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
if errorlevel 1 exit /b 1
cmake --build "%~dp0..\build\cpu-mingw" --parallel 2
if errorlevel 1 exit /b 1
ctest --test-dir "%~dp0..\build\cpu-mingw" --output-on-failure
