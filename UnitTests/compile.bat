g++ -g -Wall unitTests.cpp ../Genesis/genesis.cpp ../Bits/bitsUtils.cpp ../Genesis/RomLoader/romLoader.cpp ../Genesis/GamePad/gamePad.cpp ../M68k/m68k.cpp ../YM7101/ym7101.cpp ../CathodeRayTube/crt.cpp ..\sfml\*.a ..\*.dll -o UnitTests.exe -lmingw32 -lcomdlg32 -static-libgcc -static-libstdc++ -Wno-switch
pause