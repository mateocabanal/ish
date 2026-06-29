# x86_64 Support

This document tracks the experimental x86_64 userland emulation support for iSH.

## Current Status

iSH currently emulates **i386 (32-bit x86)** Linux userland. x86_64 support is planned but not yet implemented.

## Goals

1. **Coexistence**: Support both i386 and x86_64 programs in the same iSH build.
2. **Static binaries first**: Run static x86_64 ELF binaries before attempting dynamic linking.
3. **Dynamic musl**: Support x86_64 Alpine/musl dynamic userland.
4. **No JIT requirement**: x86_64 support must work with the Asbestos threaded-code engine (iOS-compatible).

## Non-Goals

- JIT compilation for performance (see `docs/jit-engine-plan.md` for platform constraints).
- Full x86_64 instruction coverage on day one.
- Kernel-mode emulation.

## Implementation Strategy

The port is structured as a series of milestones:

1. **Milestone 0**: Safety harness and inventory of 32-bit assumptions.
2. **Milestone 1**: Guest ABI and type separation (split `addr_t` into `guest32_addr_t` / `guest64_addr_t`).
3. **Milestone 2**: Memory model for 64-bit guest addresses (sparse page tables).
4. **Milestone 3**: ELF64 loader and x86_64 process startup.
5. **Milestone 4**: x86_64 CPU state (registers, FS/GS base, instruction pointer).
6. **Milestone 5**: Long-mode decode (REX prefixes, ModRM64, RIP-relative addressing).
7. **Milestone 6**: x86_64 syscall ABI (separate syscall table, argument registers).
8. **Milestone 7**: TLS, threads, signals for x86_64.
9. **Milestone 8**: Tooling (extend `unicornomatic` for 64-bit mode).
10. **Milestone 9**: End-to-end bring-up (static hello → static C → dynamic musl → Alpine rootfs).

## Testing

Each milestone includes TDD-style tests. Key test targets:

- `tests/e2e/x86_64/hello/test.sh` - Static assembly hello
- `tests/e2e/x86_64/static-c/test.sh` - Static C hello
- `tests/e2e/x86_64/dynamic-hello/test.sh` - Dynamic musl hello
- `tests/e2e/x86_64/alpine-smoke/test.sh` - Minimal Alpine shell

## ABI Differences

x86_64 Linux syscall ABI differs from i386:

| Aspect | i386 | x86_64 |
|--------|------|--------|
| Syscall number | `eax` | `rax` |
| Arg 1 | `ebx` | `rdi` |
| Arg 2 | `ecx` | `rsi` |
| Arg 3 | `edx` | `rdx` |
| Arg 4 | `esi` | `r10` |
| Arg 5 | `edi` | `r8` |
| Arg 6 | `ebp` | `r9` |
| Return | `eax` | `rax` |
| Instruction | `int 0x80` | `syscall` |

## Key Files

See `docs/architecture.md` for the overall iSH architecture.

x86_64-specific changes will touch:

- `kernel/abi.h` - Guest ABI enum
- `kernel/exec.c` - ELF loader (split ELF32/ELF64)
- `emu/cpu.h` - CPU state (64-bit registers)
- `emu/modrm.h` - ModRM64 decoding
- `kernel/calls_x86_64.c` - x86_64 syscall table

## References

- Linux x86_64 syscall table: https://blog.rchapman.org/posts/Linux_System_Call_Table_for_x86_64/
- System V AMD64 ABI: https://refspecs.linuxbase.org/elf/x86_64-abi-0.99.pdf
- iSH architecture overview: `docs/architecture.md`
