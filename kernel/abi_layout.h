#ifndef KERNEL_ABI_LAYOUT_H
#define KERNEL_ABI_LAYOUT_H

#include "kernel/abi.h"
#include "misc.h"

/*
 * ABI-specific address space layout constants.
 *
 * These constants define where different memory regions are placed in the
 * guest address space. They differ between i386 and x86_64 due to different
 * address space sizes and conventions.
 */

struct guest_abi_layout {
    const char *name;           // "i386" or "x86_64"
    const char *platform;       // "i686" or "x86_64" for AT_PLATFORM
    size_t pointer_size;        // 4 or 8 bytes
    size_t stack_alignment;     // 16 bytes for both, but enforced differently
    
    // Stack layout
    guest_addr_t stack_top;     // Initial stack pointer (grows down)
    guest_addr_t stack_limit;   // Maximum stack size (soft limit)
    
    // ELF loading
    guest_addr_t exec_base;     // Base address for non-PIE executables
    guest_addr_t interp_base;   // Base address for dynamic linker
    guest_addr_t vdso_base;     // Base address for vDSO (if used)
    
    // Memory limits
    guest_addr_t mmap_base;     // Base address for mmap allocations
    guest_addr_t brk_base;      // Base address for brk heap
};

/*
 * Get the layout constants for a given ABI.
 */
static inline const struct guest_abi_layout *guest_abi_layout(enum guest_abi abi) {
    static const struct guest_abi_layout i386_layout = {
        .name = "i386",
        .platform = "i686",
        .pointer_size = 4,
        .stack_alignment = 16,
        .stack_top = 0xffffe000,
        .stack_limit = 8 * 1024 * 1024,  // 8 MB default stack
        .exec_base = 0x08048000,
        .interp_base = 0x40000000,
        .vdso_base = 0xffffe000,
        .mmap_base = 0x40000000,
        .brk_base = 0,  // Set after exec
    };
    
    static const struct guest_abi_layout x86_64_layout = {
        .name = "x86_64",
        .platform = "x86_64",
        .pointer_size = 8,
        .stack_alignment = 16,
        .stack_top = 0x7fffffffe000,  // Near top of user space
        .stack_limit = 8 * 1024 * 1024,  // 8 MB default stack
        .exec_base = 0x400000,  // Standard x86_64 base
        .interp_base = 0x7ffff7dd7000,  // Typical ld-linux base
        .vdso_base = 0x7ffff7ffa000,
        .mmap_base = 0x7f0000000000,  // High mmap region
        .brk_base = 0,  // Set after exec
    };
    
    switch (abi) {
        case GUEST_ABI_I386:
            return &i386_layout;
        case GUEST_ABI_X86_64:
            return &x86_64_layout;
    }
    return &i386_layout;  // Fallback
}

#endif /* KERNEL_ABI_LAYOUT_H */
