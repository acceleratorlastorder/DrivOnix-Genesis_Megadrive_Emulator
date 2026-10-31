/* Compile the vendor's register-union implementation as ISO C, its native language. */
#define CHIPS_IMPL
#include "core.h"

static bool writes_flags(uint8_t prefix, uint8_t op)
{
    if (prefix == 0xcb) return (op >> 6) < 2;
    if (prefix == 0xed)
        return (op & 0xc7) == 0x40 || (op & 0xc7) == 0x42 ||
               (op & 0xc7) == 0x44 || op == 0x4a || op == 0x5a || op == 0x6a || op == 0x7a ||
               op == 0x57 || op == 0x5f || op == 0x67 || op == 0x6f ||
               (op & 0xe4) == 0xa0;
    return (op >= 0x80 && op <= 0xbf) || (op & 0xc7) == 4 || (op & 0xc7) == 5 ||
           (op & 0xcf) == 9 || (op & 0xc7) == 0xc6 ||
           op == 7 || op == 0x0f || op == 0x17 || op == 0x1f ||
           op == 0x27 || op == 0x2f || op == 0x37 || op == 0x3f;
}

uint64_t md_z80_init(md_z80_t* s)
{
    memset(s,0,sizeof(*s));
    return z80_init(&s->cpu);
}
uint64_t md_z80_reset(md_z80_t* s) { return md_z80_init(s); }
bool md_z80_opdone(md_z80_t* s) { return z80_opdone(&s->cpu); }
uint64_t md_z80_tick(md_z80_t* s, uint64_t pins)
{
    if ((pins & (Z80_M1|Z80_RD)) == (Z80_M1|Z80_RD))
    {
        if (!s->active)
        {
            s->active = true;
            s->start_q = s->q;
            s->start_f = s->cpu.f;
            s->prefix = 0;
        }
        const uint8_t value = Z80_GET_DATA(pins);
        if (s->prefix == 0)
        {
            // NMOS DD/FD before SCF/CCF clears the preceding ALU-Q context.
            if (value == 0xdd || value == 0xfd) s->start_q = 0;
            if (value == 0xcb || value == 0xed)
            {
                s->prefix = value;
                s->opcode_pc = Z80_GET_ADDR(pins);
            }
            // DD/FD prefixes do not change which ALU instructions write flags.
        }
    }
    pins = z80_tick(&s->cpu,pins);
    if (s->active && z80_opdone(&s->cpu))
    {
        const uint8_t opcode = s->cpu.opcode;
        if (s->prefix == 0 && (opcode == 0x37 || opcode == 0x3f))
        {
            // NMOS SCF/CCF undocumented X/Y flags are affected by the prior Q latch.
            s->cpu.f = (s->cpu.f & ~0x28) | (((s->start_q ^ s->start_f) | s->cpu.a) & 0x28);
        }
        // A taken repeat performs an extra PC adjustment whose high byte
        // drives undocumented X/Y flags on NMOS block instructions.
        if (s->prefix == 0xed && (opcode & 0xf4) == 0xb0 &&
            (uint16_t)(s->cpu.pc - 1) == s->opcode_pc)
        {
            s->cpu.f = (s->cpu.f & ~0x28) | ((s->opcode_pc >> 8) & 0x28);
            if (opcode & 2)
            {
                // David Banks' NMOS interrupted/repeating block-I/O flags:
                // https://github.com/hoglet67/Z80Decoder/wiki/Undocumented-Flags
                unsigned parityInput = s->cpu.b;
                if (s->cpu.f & Z80_CF)
                {
                    const bool negative = (s->cpu.f & Z80_NF) != 0;
                    const bool half = (s->cpu.b & 15) == (negative ? 0 : 15);
                    s->cpu.f = (s->cpu.f & ~Z80_HF) | (half ? Z80_HF : 0);
                    parityInput += negative ? -1 : 1;
                }
                parityInput &= 7;
                // Toggle parity when the three-bit correction has odd parity.
                if (((parityInput >> 2) ^ (parityInput >> 1) ^ parityInput) & 1)
                    s->cpu.f ^= Z80_PF;
            }
        }
        s->q = writes_flags(s->prefix,opcode) ? s->cpu.f : 0;
        s->p = s->prefix == 0xed && (opcode == 0x57 || opcode == 0x5f);
        s->ei = s->prefix == 0 && opcode == 0xfb;
        s->active = false;
    }
    return pins;
}
uint64_t md_z80_load(md_z80_t* s, const md_z80_vector* v)
{
    md_z80_init(s);
    s->cpu.pc=v->pc; s->cpu.sp=v->sp; s->cpu.ix=v->ix; s->cpu.iy=v->iy;
    s->cpu.af2=v->af_;s->cpu.bc2=v->bc_;s->cpu.de2=v->de_;s->cpu.hl2=v->hl_;s->cpu.wz=v->wz;
    s->cpu.a=v->a;s->cpu.f=v->f;s->cpu.b=v->b;s->cpu.c=v->c;s->cpu.d=v->d;
    s->cpu.e=v->e;s->cpu.h=v->h;s->cpu.l=v->l;s->cpu.i=v->i;s->cpu.r=v->r;
    s->cpu.im=v->im;s->cpu.iff1=v->iff1;s->cpu.iff2=v->iff2;s->q=v->q;s->p=v->p;s->ei=v->ei;
    return z80_prefetch(&s->cpu,v->pc);
}
void md_z80_save(const md_z80_t* s, md_z80_vector* v)
{
    // The overlapped core has already initiated the next opcode fetch at M1/T2.
    v->pc=(uint16_t)(s->cpu.pc - ((s->cpu.pins & Z80_HALT) ? 0 : 1));v->sp=s->cpu.sp;v->ix=s->cpu.ix;v->iy=s->cpu.iy;
    v->af_=s->cpu.af2;v->bc_=s->cpu.bc2;v->de_=s->cpu.de2;v->hl_=s->cpu.hl2;v->wz=s->cpu.wz;
    v->a=s->cpu.a;v->f=s->cpu.f;v->b=s->cpu.b;v->c=s->cpu.c;v->d=s->cpu.d;
    v->e=s->cpu.e;v->h=s->cpu.h;v->l=s->cpu.l;v->i=s->cpu.i;v->r=s->cpu.r;
    v->im=s->cpu.im;v->iff1=s->cpu.iff1;v->iff2=s->cpu.iff2;v->q=s->q;v->p=s->p;v->ei=s->ei;
}
