# GameBoo
```bash


 ______             _            ___              _           
|_   _ `.          (_)         .'   `.           (_)          
  | | `. \ _ .--.  __  _   __ /  .-.  \ _ .--.   __   _   __  
  | |  | |[ `/'`\][  |[ \ [  ]| |   | |[ `.-. | [  | [ \ [  ] 
 _| |_.' / | |     | | \ \/ / \  `-'  / | | | |  | |  > '  <  
|______.' [___]   [___] \__/   `.___.' [___||__][___][__]`\_] 
                                                              


  Emulator and Bios Programmer : Julien Magnin (MS-DOS1999)
  Unit Test Programmer : Okba Hamidi (acceleratorlastorder)
 ```

DrivOnix is a SEGA Megadrive/Genesis emulator for Linux and Windows, written in C++/CSFML.
The emulator can boot the majority of Genesis games but most games have in-game bugs.

<img src="Images/sonic.PNG" width="320"><img src="Images/jim.PNG" width="320"><img src="Images/rage.PNG" width="320"><img src="Images/vector.PNG" width="320">

## Installation

Build from source using the instructions below. GitHub Actions also produces
Linux and Windows build artifacts. Extract the Linux tar.gz archive to preserve
its executable permissions; it needs the native CSFML 2 and GTK 3 runtime
libraries installed.

### Building from source

Clone this repository:

```sh
git clone https://github.com/acceleratorlastorder/DrivOnix-Genesis_Megadrive_Emulator.git
cd DrivOnix-Genesis_Megadrive_Emulator
```

#### Linux (Debian / Ubuntu)

```sh
sudo apt-get update
sudo apt-get install build-essential cmake pkg-config python3 libcsfml-dev libgtk-3-dev
cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux --parallel 2
./build/linux/DrivOnix
```

Run without arguments to select a ROM in the GTK file picker, or pass its path:

```sh
./build/linux/DrivOnix "/path/to/My Game.bin"
```

This is a native Linux build of the full graphical emulator. CSFML **2.x** is
required; CSFML 3 uses a different API. On Wayland desktops, XWayland must be
available because CSFML 2 uses X11. Other distributions need a C++11 compiler,
CMake 3.16 or newer, CSFML 2 development files, GTK 3 development files and
pkg-config. Python 3.9 or newer is needed to run tests; pass
`-DBUILD_TESTING=OFF` when building without Python.

The build copies `Bios/DrivOnix.bios` and `Config/config.txt` alongside the
executable, so it can be launched from another working directory or via `PATH`.
Keep those directories with the executable when moving the build. An optional
installation uses the usual `bin/` and `share/drivonix/` layout:

```sh
cmake --install build/linux --prefix "$HOME/.local"
"$HOME/.local/bin/DrivOnix" "/path/to/game.bin"
```

#### Windows

The original 32-bit MinGW build is still available:

```bat
compile.bat
```

It uses MinGW and the bundled 32-bit CSFML libraries and DLLs. For a native
64-bit MSVC build, build matching dependencies and use CMake (Visual Studio
2022 C++ tools, CMake and Python required):

```powershell
python .github/scripts/build_windows_csfml.py --prefix build/deps
cmake -S . -B build/windows -A x64 -DCSFML_ROOT="$PWD/build/deps"
cmake --build build/windows --config Release --parallel 2
Copy-Item build/deps/bin/*.dll build/windows/Release/
./build/windows/Release/DrivOnix.exe
```

The Windows dependency helper verifies pinned SFML 2.6.2 and CSFML 2.6.1 source
archives before building them. The native CMake builds do not use the bundled
32-bit Windows libraries.

#### Tests

Run frontend and ROM-loader checks with `ctest --test-dir build/linux
--output-on-failure` (on Windows add `-C Release` and use `build/windows`). The
Linux GUI smoke test generates an original test ROM and checks the picker,
BIOS handoff, rendered red/green frames, controller input and clean exit:

```sh
sudo apt-get install xvfb xdotool
xvfb-run -a -s "-screen 0 1280x1024x24" python3 UnitTests/frontend/test_linux_gui.py --emulator build/linux/DrivOnix
```

The separate CPU conformance suite remains documented in
[UnitTests/README.txt](UnitTests/README.txt). Platform support does not resolve
the CPU accuracy failures recorded there or add the missing Z80 and sound cores.

## Usage

Launch `DrivOnix` (Linux) or `DrivOnix.exe` (Windows) and select a ROM, or pass a
ROM path as the first argument. Cancelling the picker exits normally; invalid
or unreadable files report an error. BIN, MD, GEN and SMD extensions are accepted
without case sensitivity. PAL or NTSC mode is automatically set by the BIOS.

<img src="Images/bios.PNG" width="320">

Other options:

  Change `Config/config.txt` to select the screen size. Linux paths are case
  sensitive. A `Config/config.txt` in the current working directory overrides
  the copy alongside the executable or in the installation's data directory.

## Joypad/Input

-Player 1<br/>
<kbd>Z</kbd> - Up<br/>
<kbd>Q</kbd> - Left<br/>
<kbd>S</kbd> - Down<br/>
<kbd>D</kbd> - Right<br/>
<kbd>A</kbd> - Start<br/>
<kbd>K</kbd> - A<br/>
<kbd>L</kbd> - B<br/>
<kbd>M</kbd> - C<br/><br/>

-Player 2<br/>
<kbd>↑</kbd> - Up<br/>
<kbd>←</kbd> - Left<br/>
<kbd>↓</kbd> - Down<br/>
<kbd>→</kbd> - Right<br/>
<kbd>Numpad 4</kbd> - Start<br/>
<kbd>Numpad 1</kbd> - A<br/>
<kbd>Numpad 2</kbd> - B<br/>
<kbd>Numpad 3</kbd> - C<br/><br/>

-Emulator<br/>
<kbd>Esc</kbd> - Quit Emulator<br/><br/>

### Saving 
Actually not working

## Resources
Resources were used to understand and test the Genesis hardware. Some of these include:
* <https://github.com/Emu-Docs/Emu-Docs/tree/master/Genesis>
* <https://www.nxp.com/docs/en/reference-manual/M68000PRM.pdf>
* <https://wiki.megadrive.org/index.php?title=Main_68k_memory_map>
* <https://wiki.megadrive.org/index.php?title=VDP_Ports>
* <https://wiki.megadrive.org/index.php?title=IO_Registers>
* <https://segaretro.org/images/1/18/GenesisTechnicalOverview.pdf>
* <https://segaretro.org/Category:Sega_Mega_Drive>
* <https://wiki.megadrive.org/index.php?title=MD_Rom_Header>
* <https://wiki.neogeodev.org/index.php?title=68k_instructions_timings&t=20181125164410>
* <http://www.scarpaz.com/Attic/Didattica/Scarpazza-2005-68k-1-addressing.pdf>
