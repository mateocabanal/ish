# x86_64 Porting Inventory

This document catalogs all 32-bit i386 assumptions in the iSH codebase that must be addressed to support x86_64.

## How This Was Generated

```bash
git grep -n "dword_t\|addr_t\|ELF_32BIT\|ELF_X86\|eip\|esp\|eax\|syscall_table\|set_thread_area\|arch_prctl\|i686"
```

## 1. Type Definitions (misc.h, emu/mmu.h)

### Critical typedefs to split:
- `addr_t` = `dword_t` (32-bit guest address)
- `page_t` = `dword_t` (32-bit page number)
- `MEM_PAGES` = `1 << 20` (4GB / 4KB pages)

### Files affected:
- `misc.h`: `typedef dword_t addr_t;`
- `emu/mmu.h`: `typedef dword_t page_t;`
- `kernel/mm.h`: uses `addr_t` extensively
- `kernel/memory.h`: uses `page_t` and `addr_t`

### Migration strategy:
1. Introduce `guest32_addr_t`, `guest64_addr_t`, `guest_addr_t`
2. Keep `addr_t` as temporary alias to `guest32_addr_t`
3. Gradually convert APIs to use `guest_addr_t`
4. Update call sites based on `current->mm->abi`

## 2. ELF Structures (kernel/elf.h)

### 32-bit structures:
- `struct elf_header` (52 bytes)
- `struct prg_header` (32 bytes)
- `struct aux_ent` (8 bytes)
- `struct dyn_ent` (8 bytes)
- `struct elf_sym` (16 bytes)

### 64-bit equivalents needed:
- `struct elf64_header` (64 bytes)
- `struct elf64_prg_header` (56 bytes)
- `struct elf64_aux_ent` (16 bytes)
- `struct elf64_dyn_ent` (16 bytes)
- `struct elf64_sym` (24 bytes)

### Files affected:
- `kernel/elf.h`: define 64-bit variants
- `kernel/exec.c`: split `read_header()`, `read_prg_headers()`
- `kernel/exec.c`: split `elf_exec()` into `elf32_exec()` / `elf64_exec()`

## 3. ELF Loader (kernel/exec.c)

### Current i386-only behavior:
```c
if (header->bitness != ELF_32BIT || header->machine != ELF_X86)
    return _ENOEXEC;
```

### Hard-coded i386 constants:
- Stack starts at `0xffffe000` (near top of 32-bit space)
- `bias = 0x56555000` for PIE executables
- `platform = "i686"`
- ELF entry sets `cpu->eip` and `cpu->esp`

### Changes needed:
1. Detect `ELF_64BIT` + `ELF_X86_64` and route to `elf64_exec()`
2. Use `guest_abi_layout()` to get stack top, bias, platform string
3. Build 64-bit stack (8-byte pointers, 16-byte alignment)
4. Set `cpu->rip` and `cpu->rsp` instead of `eip`/`esp`
5. Initialize 64-bit registers (`rax`-`r15`)

## 4. CPU State (emu/cpu.h)

### Current i386 registers:
```c
struct cpu_state {
    union { dword_t eax, ecx, edx, ebx, esp, ebp, esi, edi; };
    dword_t eip;
    dword_t eflags;
    // ...
};
```

### 64-bit additions:
- `rax`-`r15` (16 general registers, 64-bit)
- `rip` (64-bit instruction pointer)
- `rflags` (64-bit flags register)
- `fs_base`, `gs_base` (64-bit TLS bases)

### Migration strategy:
1. Add `qword_t regs64[16]` union with 32-bit aliases
2. Add `rip`, `rflags`, `fs_base`, `gs_base` fields
3. Create accessor functions: `cpu_ip()`, `cpu_set_ip()`, `cpu_sp()`, `cpu_set_sp()`
4. Update call sites to use accessors with `current->mm->abi`

### Zero-extension rule:
- Writing 32-bit register in 64-bit mode zero-extends to 64 bits
- Writing 16-bit or 8-bit register does NOT zero-extend

## 5. Decoder (emu/decode.h, emu/modrm.h)

### Current i386 decoding:
- `modrm_decode32()` handles 32-bit addressing modes
- `decode.h` macro-generated for `OP_SIZE` (16/32-bit)
- No REX prefix support
- No RIP-relative addressing

### 64-bit additions:
- **REX prefix** (`0x40`-`0x4f`): extends register fields
  - `REX.W`: 64-bit operand size
  - `REX.R`: extends ModRM reg field
  - `REX.X`: extends SIB index field
  - `REX.B`: extends ModRM r/m or SIB base/opcode
- **modrm_decode64()**: handles 64-bit addressing
  - RIP-relative (`[rip + disp32]`)
  - 64-bit base/index registers (`r8`-`r15`)
- **Legacy high-byte registers** (`ah`, `ch`, `dh`, `bh`): unavailable with REX prefix

### Files affected:
- `emu/modrm.h`: add `modrm_decode64()`
- `emu/decode.h`: add REX prefix handling, `modrm_decode64()` calls
- `asbestos/gen.c`: emit 64-bit gadgets

## 6. Asbestos Engine (asbestos/)

### Current behavior:
- `asbestos.c`: caches decoded blocks by guest `eip`
- `gen.c`: generates gadgets for i386 instructions
- `gadgets-x86_64/`: x86_64 host assembly (NOT guest instructions)

### Changes needed:
- Cache blocks by guest `rip` (64-bit) for x86_64 mode
- Add 64-bit operand gadgets (64-bit `mov`, `add`, `sub`, etc.)
- Add REX prefix handling in `gen.c`
- Add RIP-relative memory gadgets

### Files affected:
- `asbestos/asbestos.c`: use `cpu_ip()` instead of `cpu->eip`
- `asbestos/gen.c`: add 64-bit decode path gated by `abi == GUEST_ABI_X86_64`
- `asbestos/gadgets-x86_64/`: add 64-bit guest instruction gadgets

## 7. Syscall Dispatch (kernel/calls.c)

### Current i386 ABI:
```c
int syscall_num = cpu->eax;
syscall_table[syscall_num](cpu->ebx, cpu->ecx, cpu->edx, cpu->esi, cpu->edi, cpu->ebp);
```

### x86_64 ABI:
```c
qword_t syscall_num = cpu->rax;
syscall64_table[syscall_num](cpu->rdi, cpu->rsi, cpu->rdx, cpu->r10, cpu->r8, cpu->r9);
```

### Changes needed:
1. Add `syscall64_table[]` in `kernel/calls_x86_64.c`
2. Map x86_64 syscall numbers (different from i386)
3. Handle 64-bit argument/return values
4. Sign-extend negative error returns

### Initial syscall table:
| x86_64 # | Syscall | i386 # | Notes |
|----------|---------|--------|-------|
| 0 | read | 3 | 64-bit args |
| 1 | write | 4 | 64-bit args |
| 60 | exit | 1 | 64-bit args |
| 231 | exit_group | 252 | 64-bit args |
| 9 | mmap | 192 | 64-bit, different arg order |
| 10 | mprotect | 125 | 64-bit args |
| 11 | munmap | 91 | 64-bit args |
| 12 | brk | 45 | 64-bit args |
| 158 | arch_prctl | 243 | x86_64-specific |
| 202 | futex | 240 | 64-bit args |
| 218 | set_tid_address | 258 | 64-bit args |
| 257 | openat | 295 | 64-bit args |
| 262 | newfstatat | 300 | 64-bit args |
| 273 | set_robust_list | 311 | 64-bit args |
| 302 | prlimit64 | 340 | 64-bit args |
| 318 | getrandom | 355 | 64-bit args |

## 8. Signal Handling (kernel/signal.c)

### Current i386 signal frame:
- Saves `eip`, `esp`, `eflags`, 8 general registers
- `sigreturn` restores 32-bit state

### x86_64 additions:
- Save `rip`, `rsp`, `rflags`, 16 general registers
- Save `fs_base`, `gs_base`
- Save XMM registers (already partially done)
- `rt_sigreturn` restores 64-bit state

### Files affected:
- `kernel/signal.c`: add `setup_frame64()`, `restore_frame64()`
- `kernel/signal.h`: add `struct sigframe64`
- `kernel/calls_x86_64.c`: add `sys_rt_sigreturn64()`

## 9. TLS (Thread-Local Storage)

### Current i386 TLS:
- `sys_set_thread_area()` (syscall 243)
- Uses segment registers `fs`/`gs`

### x86_64 TLS:
- `arch_prctl(ARCH_SET_FS, addr)` (syscall 158)
- Uses MSR-based FS/GS base (no segment registers)

### Files affected:
- `kernel/misc.c`: add `sys_arch_prctl64()`
- `emu/cpu.h`: add `fs_base`, `gs_base` fields
- `asbestos/gadgets-x86_64/`: add FS/GS-relative memory access

## 10. Threading (kernel/fork.c)

### Current i386 clone:
```c
sys_clone(flags, stack_ptr, parent_tid, child_tid, tls)
```

### x86_64 clone:
```c
sys_clone(flags, stack_ptr, parent_tid, child_tid, tls)
```
Same signature, but `stack_ptr` and `tls` are 64-bit.

### Changes needed:
- Update `struct clone_args` to use `guest_addr_t`
- Handle 64-bit stack pointer in child thread
- Pass 64-bit TLS pointer to `arch_prctl`

## 11. File System Structures

### Structures with pointer-width fields:
- `struct stat` / `struct stat64`
- `struct dirent` / `struct dirent64`
- `struct timespec`, `struct timeval`
- `struct rlimit`, `struct rusage`
- Socket address structures (`struct sockaddr_*`)

### Migration strategy:
1. Create `struct stat64` (already exists for i386 large file support)
2. Add `struct stat_x86_64` for x86_64 syscall returns
3. Add conversion functions `stat_to_stat64()`, `stat64_to_stat_x86_64()`
4. Update syscall wrappers to use correct struct based on ABI

## 12. vDSO (Virtual Dynamic Shared Object)

### Current i386 vDSO:
- 32-bit ELF shared library
- Provides `__vdso_clock_gettime()`, `__vdso_gettimeofday()`
- Mapped at `0xffffe000`

### x86_64 vDSO:
- 64-bit ELF shared library
- Provides same functions with 64-bit calling convention
- Mapped at high address (above stack)

### Migration strategy:
1. Build separate `vdso64.so` from `vdso/vdso.c`
2. Load appropriate vDSO based on `current->mm->abi`
3. Update `AT_SYSINFO_EHDR` auxv entry to point to correct vDSO

## 13. Procfs (/proc)

### Files with architecture-dependent content:
- `/proc/cpuinfo`: should report x86_64 features
- `/proc/self/maps`: 64-bit address format
- `/proc/self/stat`: 64-bit fields

### Files affected:
- `fs/proc.c`: update `proc_read_cpuinfo()`, `proc_read_maps()`

## 14. Unicorn Comparator (tools/unicornomatic.c)

### Current behavior:
- Uses `UC_MODE_32` (32-bit mode)
- Compares i386 registers

### 64-bit additions:
- Detect `current->mm->abi` and use `UC_MODE_64`
- Map `rax`-`r15`, `rip`, `rflags`
- Compare 64-bit memory operands

## 15. Testing Strategy

### Unit tests:
- `tests/unit/abi_sizes.c`: verify struct sizes
- `tests/unit/cpu_regs_test.c`: verify register accessors
- `tests/unit/decode64_test.c`: verify REX/ModRM64 decoding

### End-to-end tests:
1. **Static assembly hello**: minimal x86_64 program
2. **Static C hello**: compiler-generated startup
3. **Dynamic musl hello**: interpreter loading, TLS, mmap
4. **Alpine smoke**: real shell, package manager

### Fixture builder:
```bash
# tests/fixtures/x86_64/build-fixtures.sh
x86_64-linux-musl-gcc -nostdlib -static hello.S -o hello
x86_64-linux-musl-gcc -static static-hello.c -o static-hello
x86_64-linux-musl-gcc dynamic-hello.c -o dynamic-hello
```

## 16. Open Questions

1. **Mixed ABI support**: Can a single iSH session run both i386 and x86_64 programs?
   - **Initial answer**: No. Choose ABI at boot time via rootfs.
   - **Future**: Allow `execve()` to switch ABI (like Linux does).

2. **Memory layout**: Should x86_64 use full canonical address space (128 TB)?
   - **Initial answer**: No. Use conservative subset (e.g., 48-bit canonical).
   - **Future**: Full support if needed.

3. **Syscall compatibility**: Should we reuse `sys_*` handlers or create `sys64_*` variants?
   - **Initial answer**: Reuse where safe, create wrappers for ABI-specific cases.
   - **Example**: `sys_write()` is ABI-neutral, but `sys_mmap()` needs `sys64_mmap()`.

4. **Testing without cross-compiler**: How to test x86_64 without `x86_64-linux-musl-gcc`?
   - **Answer**: Use pre-built fixtures in `tests/fixtures/x86_64/` or skip tests.

## 17. Migration Checklist

- [ ] Milestone 0: Safety harness (THIS DOCUMENT)
- [ ] Milestone 1: Guest ABI enum and type split
- [ ] Milestone 2: Sparse memory model
- [ ] Milestone 3: ELF64 loader
- [ ] Milestone 4: x86_64 CPU state
- [ ] Milestone 5: Long-mode decode
- [ ] Milestone 6: x86_64 syscall table
- [ ] Milestone 7: TLS, threads, signals
- [ ] Milestone 8: Unicorn comparator
- [ ] Milestone 9: End-to-end tests

## References

- Plan document: `.hermes/plans/2026-06-28_220018-upgrade-ish-to-x86-64.md`
- Architecture: `docs/architecture.md`
- JIT constraints: `docs/jit-engine-plan.md`
- Linux syscall table: https://blog.rchapman.org/posts/Linux_System_Call_Table_for_x86_64/
