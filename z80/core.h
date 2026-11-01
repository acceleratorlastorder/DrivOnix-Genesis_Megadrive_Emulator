#ifndef DRIVONIX_Z80_CORE_H
#define DRIVONIX_Z80_CORE_H
#include "../thirdparty/chips/z80.h"

#ifdef __cplusplus
extern "C" {
#endif
// This wrapper supplies the NMOS Q/P latches alongside the cycle-stepped core.
// It is shared by the production bus integration and the conformance worker.
typedef struct {
    z80_t cpu;
    uint8_t q, p, ei;
    uint8_t start_q, start_f, prefix;
    uint16_t opcode_pc;
    bool active;
} md_z80_t;

typedef struct {
    uint16_t pc, sp, ix, iy, af_, bc_, de_, hl_, wz;
    uint8_t a,f,b,c,d,e,h,l,i,r,q,p,ei,iff1,iff2,im;
} md_z80_vector;

uint64_t md_z80_init(md_z80_t* state);
uint64_t md_z80_reset(md_z80_t* state);
uint64_t md_z80_tick(md_z80_t* state, uint64_t pins);
bool md_z80_opdone(md_z80_t* state);
uint64_t md_z80_load(md_z80_t* state, const md_z80_vector* input);
void md_z80_save(const md_z80_t* state, md_z80_vector* output);
#ifdef __cplusplus
}
#endif
#endif
