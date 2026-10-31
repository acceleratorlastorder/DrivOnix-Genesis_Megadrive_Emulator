@echo off
setlocal
cmake -S "%~dp0" -B "%~dp0build\windows-mingw" -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DCSFML_graphics_LIBRARY="%~dp0sfml/libcsfml-graphics.a" -DCSFML_window_LIBRARY="%~dp0sfml/libcsfml-window.a" -DCSFML_system_LIBRARY="%~dp0sfml/libcsfml-system.a" -DCSFML_audio_LIBRARY="%~dp0sfml/libcsfml-audio.a"
if errorlevel 1 exit /b 1
cmake --build "%~dp0build\windows-mingw" --parallel 2
if errorlevel 1 exit /b 1
copy /Y "%~dp0*.dll" "%~dp0build\windows-mingw\" >nul
