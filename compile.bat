g++ -g -Wall main.cpp Genesis/genesis.cpp Bits/bitsUtils.cpp Genesis/RomLoader/romLoader.cpp M68k/m68k.cpp -o DrivOnix -lmingw32 -lcomdlg32 -static-libgcc -static-libstdc++ -Wno-unused-variable -Wno-unused-but-set-variable
pause
