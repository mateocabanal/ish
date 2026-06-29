/*
 * Compile-time assertions for i386 ABI assumptions.
 *
 * These assertions verify the current 32-bit assumptions before we begin
 * refactoring to support x86_64. After the ABI split is complete, these
 * will be updated to assert explicit guest32/guest64 sizes.
 */

#include "misc.h"
#include "emu/mmu.h"
#include "kernel/elf.h"
#include <stddef.h>

/* Static assertions - checked at compile time */
_Static_assert(sizeof(addr_t) == 4,
    "i386 addr_t must be 32-bit until ABI split");

_Static_assert(sizeof(page_t) == 4,
    "i386 page_t must be 32-bit until ABI split");

_Static_assert(sizeof(dword_t) == 4,
    "dword_t must be 32-bit");

_Static_assert(sizeof(qword_t) == 8,
    "qword_t must be 64-bit");

_Static_assert(sizeof(struct elf_header) == 52,
    "ELF32 header must be 52 bytes");

_Static_assert(sizeof(struct prg_header) == 32,
    "ELF32 program header must be 32 bytes");

_Static_assert(sizeof(struct aux_ent) == 8,
    "ELF32 aux vector entry must be 8 bytes");

_Static_assert(BAD_PAGE == 0x10000,
    "BAD_PAGE sentinel must be 0x10000 for 32-bit page space");

_Static_assert(MEM_PAGES == (1 << 20),
    "MEM_PAGES must be 1M pages for 32-bit address space");

/* Runtime test - just returns success if assertions pass */
int main(void) {
    /* If we got here, all static assertions passed */
    return 0;
}
