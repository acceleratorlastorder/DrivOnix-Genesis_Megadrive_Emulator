/* Read stop state through the pinned oracle's context, never the tested CPU. */
#include "m68kcpu.h"
int drivonix_reference_stopped(void) { return (m68ki_cpu.stopped & STOP_LEVEL_STOP) != 0; }
