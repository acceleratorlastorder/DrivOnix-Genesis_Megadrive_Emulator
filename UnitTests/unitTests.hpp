#include <iostream>
#include <string>
#include <map>

#include "../M68k/m68k.hpp"

#ifdef DRIVONIX_CPU_TESTS
#include "cpuTestMemory.hpp"
#else
#include "../Genesis/genesis.hpp"
#endif

#include "opcodeCoverage.hpp"
