@echo off
setlocal
rem Run from a Visual Studio 2022 x64 developer command prompt.
python "%~dp0.github\scripts\build_windows_csfml.py" --prefix "%~dp0build\deps"
if errorlevel 1 exit /b 1
cmake -S "%~dp0" -B "%~dp0build\windows" -A x64 -DCSFML_ROOT="%~dp0build\deps" -DBUILD_TESTING=OFF
if errorlevel 1 exit /b 1
cmake --build "%~dp0build\windows" --config Release --parallel 2
if errorlevel 1 exit /b 1
copy /Y "%~dp0build\deps\bin\*.dll" "%~dp0build\windows\Release\" >nul
