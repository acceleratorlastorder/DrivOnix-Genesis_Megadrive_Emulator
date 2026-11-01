#ifndef DRIVONIX_M68000_OPCODE_LEGALITY_HPP
#define DRIVONIX_M68000_OPCODE_LEGALITY_HPP

// MC68000 instruction encodings and EA restrictions. Later-family additions
// (scaled indexes, RTD, MOVEC, bitfields, etc.) are deliberately not selected.
inline bool M68000OpcodeValid(unsigned op)
{
    const unsigned top = op >> 12;
    const unsigned mode = (op >> 3) & 7, reg = op & 7;
    const unsigned size = (op >> 6) & 3;
    const unsigned operation = (op >> 6) & 7;
    const bool memory = (mode >= 2 && mode <= 6) || (mode == 7 && reg <= 1);
    const bool alterable = mode == 0 || memory;
    const bool control = mode == 2 || mode == 5 || mode == 6 || (mode == 7 && reg <= 3);
    const bool data = mode == 0 || (mode >= 2 && mode <= 6) || (mode == 7 && reg <= 4);
    const bool source = mode <= 6 || (mode == 7 && reg <= 4);
    switch (top)
    {
        case 0:
            if ((op & 0xF138) == 0x0108) return true; // MOVEP
            if ((op & 0xF100) == 0x0100 || (op & 0xFF00) == 0x0800)
                return ((op >> 6) & 3) == 0 ? (data && (!(mode == 7 && reg == 4) || (op & 0x0100))) : alterable;
            if ((op & 0xFF00) == 0x0000 || (op & 0xFF00) == 0x0200 || (op & 0xFF00) == 0x0A00)
                if ((op & 0x3F) == 0x3C) return size < 2; // immediate CCR/SR
            if ((op & 0xFF00) == 0x0000 || (op & 0xFF00) == 0x0200 ||
                (op & 0xFF00) == 0x0400 || (op & 0xFF00) == 0x0600 ||
                (op & 0xFF00) == 0x0A00 || (op & 0xFF00) == 0x0C00)
                return size < 3 && alterable;
            return false;
        case 1: case 2: case 3:
        {
            const unsigned destination = (op >> 6) & 7, destinationReg = (op >> 9) & 7;
            if (!source || (top == 1 && mode == 1)) return false;
            if (destination == 1) return top != 1; // MOVEA
            return destination == 0 || (destination >= 2 && destination <= 6) ||
                   (destination == 7 && destinationReg <= 1);
        }
        case 4:
            if ((op & 0xF1C0) == 0x4180) return data; // CHK
            if ((op & 0xF1C0) == 0x41C0) return control; // LEA
            if ((op & 0xFFC0) == 0x40C0) return alterable; // MOVE from SR
            if ((op & 0xFFC0) == 0x44C0 || (op & 0xFFC0) == 0x46C0) return data;
            if ((op & 0xFF00) == 0x4000 || (op & 0xFF00) == 0x4200 ||
                (op & 0xFF00) == 0x4400 || (op & 0xFF00) == 0x4600)
                return size < 3 && alterable;
            if ((op & 0xFFC0) == 0x4800) return alterable; // NBCD
            if ((op & 0xFFF8) == 0x4840 || (op & 0xFFF8) == 0x4880 || (op & 0xFFF8) == 0x48C0) return true;
            if ((op & 0xFFC0) == 0x4840) return control; // PEA
            if ((op & 0xFB80) == 0x4880)
                return op & 0x0400 ? (control || mode == 3) : (memory && mode != 3);
            if (op == 0x4AFC) return true; // explicit ILLEGAL instruction
            if ((op & 0xFFC0) == 0x4AC0) return alterable;
            if ((op & 0xFF00) == 0x4A00) return size < 3 && alterable;
            if ((op & 0xFFF0) == 0x4E40 || (op & 0xFFF0) == 0x4E60 ||
                (op & 0xFFF8) == 0x4E50 || (op & 0xFFF8) == 0x4E58) return true;
            if (op >= 0x4E70 && op <= 0x4E77) return op != 0x4E74;
            if ((op & 0xFFC0) == 0x4E80 || (op & 0xFFC0) == 0x4EC0) return control;
            return false;
        case 5:
            if (size == 3) return alterable || mode == 1; // Scc / DBcc
            return alterable || (mode == 1 && size != 0);
        case 6: return true;
        case 7: return (op & 0x0100) == 0;
        case 8: case 12:
            if (top == 8 && (op & 0xF1F0) == 0x8100) return true; // SBCD
            if (top == 12 && (op & 0xF1F0) == 0xC100) return true; // ABCD
            if (top == 12 && ((op & 0xF1F8) == 0xC140 || (op & 0xF1F8) == 0xC148 ||
                              (op & 0xF1F8) == 0xC188)) return true; // EXG
            if (operation <= 3 || operation == 7) return data;
            return memory;
        case 9: case 13:
            if ((op & 0xF130) == (top == 9 ? 0x9100 : 0xD100) && size < 3) return true;
            if (operation == 3 || operation == 7) return source; // SUBA/ADDA
            if (operation < 3) return data || (mode == 1 && size != 0);
            return memory;
        case 11:
            if ((op & 0xF138) == 0xB108 && size < 3) return true; // CMPM
            if (operation == 3 || operation == 7) return source; // CMPA
            if (operation < 3) return data || (mode == 1 && size != 0);
            return alterable;
        case 14:
            return size != 3 || ((op & 0xF800) == 0xE000 && memory);
        default: return false;
    }
}
#endif
