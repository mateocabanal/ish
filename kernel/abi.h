#ifndef KERNEL_ABI_H
#define KERNEL_ABI_H

#include <stddef.h>
#include <stdbool.h>

/*
 * Guest ABI enumeration for iSH userland emulation.
 *
 * Each task and address space has an ABI that determines:
 * - Register width and calling convention
 * - Pointer size and struct layouts
 * - Syscall numbers and argument passing
 * - ELF format (32-bit vs 64-bit)
 */

enum guest_abi {
    GUEST_ABI_I386,    /* 32-bit x86 Linux */
    GUEST_ABI_X86_64,  /* 64-bit x86_64 Linux */
};

/*
 * Return human-readable name for ABI (for logging/debugging).
 */
static inline const char *guest_abi_name(enum guest_abi abi) {
    switch (abi) {
        case GUEST_ABI_I386: return "i386";
        case GUEST_ABI_X86_64: return "x86_64";
    }
    return "unknown";
}

/*
 * Return pointer size in bytes for given ABI.
 */
static inline size_t guest_abi_pointer_size(enum guest_abi abi) {
    switch (abi) {
        case GUEST_ABI_I386: return 4;
        case GUEST_ABI_X86_64: return 8;
    }
    return 0;
}

/*
 * Check if ABI is 64-bit.
 */
static inline bool guest_abi_is_64bit(enum guest_abi abi) {
    return abi == GUEST_ABI_X86_64;
}

#endif /* KERNEL_ABI_H */
