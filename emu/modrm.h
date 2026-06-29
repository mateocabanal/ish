#ifndef MODRM_H
#define MODRM_H

#include "debug.h"
#include "misc.h"
#include "emu/cpu.h"
#include "emu/tlb.h"

#undef DEFAULT_CHANNEL
#define DEFAULT_CHANNEL instr

struct modrm {
    union {
        enum reg32 reg;
        unsigned opcode;
    };
    enum {
        modrm_reg, modrm_mem, modrm_mem_si
    } type;
    union {
        enum reg32 base;
        unsigned rm_opcode;
    };
    int32_t offset;
    enum reg32 index;
    enum {
        times_1 = 0,
        times_2 = 1,
        times_4 = 2,
    } shift;
};

static const unsigned rm_sib = reg_esp;
static const unsigned rm_none = reg_esp;
static const unsigned rm_disp32 = reg_ebp;
#define MOD(byte) ((byte & 0b11000000) >> 6)
#define REG(byte) ((byte & 0b00111000) >> 3)
#define RM(byte)  ((byte & 0b00000111) >> 0)

// read modrm and maybe sib, output information into *modrm, return false for segfault
static inline bool modrm_decode32(addr_t *ip, struct tlb *tlb, struct modrm *modrm) {
#define READ(thing) \
    *ip += sizeof(thing); \
    if (!tlb_read(tlb, *ip - sizeof(thing), &(thing), sizeof(thing))) \
        return false

    byte_t modrm_byte;
    READ(modrm_byte);

    enum {
        mode_disp0,
        mode_disp8,
        mode_disp32,
        mode_reg,
    } mode = MOD(modrm_byte);
    modrm->type = modrm_mem;
    modrm->reg = REG(modrm_byte);
    modrm->rm_opcode = RM(modrm_byte);
    if (mode == mode_reg) {
        modrm->type = modrm_reg;
    } else if (modrm->rm_opcode == rm_disp32 && mode == mode_disp0) {
        modrm->base = reg_none;
        mode = mode_disp32;
    } else if (modrm->rm_opcode == rm_sib && mode != mode_reg) {
        byte_t sib_byte;
        READ(sib_byte);
        modrm->base = RM(sib_byte);
        // wtf intel
        if (modrm->rm_opcode == rm_disp32) {
            if (mode == mode_disp0) {
                modrm->base = reg_none;
                mode = mode_disp32;
            } else {
                modrm->base = reg_ebp;
            }
        }
        modrm->index = REG(sib_byte);
        modrm->shift = MOD(sib_byte);
        if (modrm->index != rm_none)
            modrm->type = modrm_mem_si;
    }

    if (mode == mode_disp0) {
        modrm->offset = 0;
    } else if (mode == mode_disp8) {
        int8_t offset;
        READ(offset);
        modrm->offset = offset;
    } else if (mode == mode_disp32) {
        int32_t offset;
        READ(offset);
        modrm->offset = offset;
    }
#undef READ

    TRACE("reg=%s opcode=%d ", reg32_name(modrm->reg), modrm->opcode);
    TRACE("base=%s ", reg32_name(modrm->base));
    if (modrm->type != modrm_reg)
        TRACE("offset=%s0x%x ", modrm->offset < 0 ? "-" : "", modrm->offset);
    if (modrm->type == modrm_mem_si)
        TRACE("index=%s<<%d ", reg32_name(modrm->index), modrm->shift);

    return true;
}

// 64-bit ModRM structure for x86-64
struct modrm64 {
    union {
        enum reg64 reg;
        unsigned opcode;
    };
    enum {
        modrm64_reg, modrm64_mem, modrm64_mem_si, modrm64_rip
    } type;
    union {
        enum reg64 base;
        unsigned rm_opcode;
    };
    int32_t offset;
    enum reg64 index;
    enum {
        times64_1 = 0,
        times64_2 = 1,
        times64_4 = 2,
        times64_8 = 3,
    } shift;
};

// Decode 64-bit ModRM with REX prefix support
// rex: REX prefix byte (0x40-0x4F), 0 if no REX
static inline bool modrm_decode64(addr_t *ip, struct tlb *tlb, struct modrm64 *modrm, byte_t rex) {
#define READ64(thing) \
    *ip += sizeof(thing); \
    if (!tlb_read(tlb, *ip - sizeof(thing), &(thing), sizeof(thing))) \
        return false

    // Extract REX bits
    bool rex_r = (rex & 0x4) != 0;  // REX.R extends reg field
    bool rex_x = (rex & 0x2) != 0;  // REX.X extends SIB index
    bool rex_b = (rex & 0x1) != 0;  // REX.B extends rm/base field

    byte_t modrm_byte;
    READ64(modrm_byte);

    enum {
        mode64_disp0,
        mode64_disp8,
        mode64_disp32,
        mode64_reg,
    } mode = MOD(modrm_byte);

    unsigned reg_field = REG(modrm_byte);
    unsigned rm_field = RM(modrm_byte);

    // Apply REX.R to reg field
    if (rex_r)
        reg_field |= 8;

    // Apply REX.B to rm field (for register mode or base)
    if (rex_b && mode != mode64_reg && rm_field != 4)  // Don't apply to SIB case yet
        rm_field |= 8;

    modrm->reg = (enum reg64)reg_field;
    modrm->rm_opcode = rm_field;

    if (mode == mode64_reg) {
        // Register-to-register
        modrm->type = modrm64_reg;
        modrm->base = (enum reg64)rm_field;
        modrm->index = reg64_none;
        modrm->offset = 0;
    } else if (rm_field == 5 && mode == mode64_disp0) {
        // RIP-relative addressing (only in 64-bit mode)
        modrm->type = modrm64_rip;
        modrm->base = reg64_rip;
        modrm->index = reg64_none;
        int32_t offset;
        READ64(offset);
        modrm->offset = offset;
    } else if (rm_field == 4 && mode != mode64_reg) {
        // SIB byte present (rm = 4 in 64-bit mode)
        modrm->type = modrm64_mem_si;
        byte_t sib_byte;
        READ64(sib_byte);

        unsigned index_field = REG(sib_byte);
        unsigned base_field = RM(sib_byte);

        // Apply REX.X to index
        if (rex_x)
            index_field |= 8;

        // Apply REX.B to base
        if (rex_b)
            base_field |= 8;

        // Handle special base cases
        if (base_field == 5 && mode == mode64_disp0) {
            modrm->base = reg64_none;
            mode = mode64_disp32;
        } else {
            modrm->base = (enum reg64)base_field;
        }

        // Handle index (index = 4 means no index, regardless of REX.X)
        if (REG(sib_byte) == 4 && !rex_x) {
            modrm->index = reg64_none;
        } else {
            modrm->index = (enum reg64)index_field;
        }

        modrm->shift = (enum { times64_1 = 0, times64_2 = 1, times64_4 = 2, times64_8 = 3 })MOD(sib_byte);

        if (mode == mode64_disp0) {
            modrm->offset = 0;
        } else if (mode == mode64_disp8) {
            int8_t offset;
            READ64(offset);
            modrm->offset = offset;
        } else if (mode == mode64_disp32) {
            int32_t offset;
            READ64(offset);
            modrm->offset = offset;
        }
    } else {
        // Simple memory addressing
        modrm->type = modrm64_mem;
        modrm->base = (enum reg64)rm_field;
        modrm->index = reg64_none;

        if (mode == mode64_disp0) {
            modrm->offset = 0;
        } else if (mode == mode64_disp8) {
            int8_t offset;
            READ64(offset);
            modrm->offset = offset;
        } else if (mode == mode64_disp32) {
            int32_t offset;
            READ64(offset);
            modrm->offset = offset;
        }
    }
#undef READ64

    TRACE("reg=%s opcode=%d ", reg64_name(modrm->reg), modrm->opcode);
    if (modrm->type == modrm64_rip)
        TRACE("rip-relative ");
    else
        TRACE("base=%s ", reg64_name(modrm->base));
    if (modrm->type != modrm64_reg)
        TRACE("offset=%s0x%x ", modrm->offset < 0 ? "-" : "", modrm->offset);
    if (modrm->type == modrm64_mem_si)
        TRACE("index=%s<<%d ", reg64_name(modrm->index), modrm->shift);

    return true;
}

#endif
