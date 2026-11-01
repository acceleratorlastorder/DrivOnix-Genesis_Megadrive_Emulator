#include <iostream>
#include <string>
#include <map>

#include "../m68k/m68k.hpp"

#ifdef DRIVONIX_CPU_TESTS
#include "cputestmemory.hpp"
#else
#include "../genesis/genesis.hpp"
#endif

#include "opcodecoverage.hpp"
