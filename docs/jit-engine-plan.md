# JIT engine rewrite plan

This document captures what it would take to replace iSH's current Asbestos
threaded-code interpreter with a real machine-code JIT.

## Feasibility summary

A production JIT is **not a small engine switch** in the current tree.
The iSH kernel build currently enforces Asbestos:

```meson
if get_option('kernel') == 'ish'
    if get_option('engine') != 'asbestos'
        error('Only asbestos is supported with ish kernel')
    endif
```

The existing `unicorn` engine path is only wired for `-Dkernel=linux`, while the
normal iSH app uses the userspace iSH kernel and the Asbestos `cpu_run_to_interrupt`
entry point.

For the App Store/TestFlight iOS target, a true JIT is also a platform/distribution
problem. The iSH FAQ says dynamic code generation requires entitlements that cannot
be used for App Store Connect distribution, and the current app entitlements do not
include any JIT entitlement. Apple documents `MAP_JIT` and
`com.apple.security.cs.allow-jit` for Apple-silicon/hardened-runtime JITs, but that
is not the same as ordinary third-party iOS App Store distribution.

**Recommended scope:** keep Asbestos as the default and only supported iOS/App Store
engine. If a JIT is pursued, build it as an experimental non-App-Store engine first
for macOS/Linux command-line and development-signed iOS builds.

## Goals

- Add an engine abstraction so Asbestos is no longer hard-coded through `struct mmu`.
- Preserve Asbestos as the fallback/default engine.
- Add an experimental JIT backend that can run a small, validated subset first.
- Fall back to Asbestos for unimplemented instructions until the JIT reaches parity.
- Keep memory-map invalidation, signal/page-fault behavior, and syscall interrupt
  behavior identical to the current engine.

## Non-goals

- Do not claim App Store-compatible iOS JIT support.
- Do not remove Asbestos until the JIT passes the existing e2e suite and compatibility
  tests.
- Do not add a dependency on a runtime code-generation library to the iOS app target
  unless the distribution story is explicitly changed.

## Current engine seams

Important files today:

- `emu/cpu.h`: public engine entry points:
  - `int cpu_run_to_interrupt(struct cpu_state *cpu, struct tlb *tlb);`
  - `void cpu_poke(struct cpu_state *cpu);`
- `emu/mmu.h`: stores `struct asbestos *asbestos` directly in `struct mmu`.
- `kernel/memory.c`: constructs/frees/invalidates `mem->mmu.asbestos` directly.
- `kernel/task.c`: calls `cpu_run_to_interrupt()` in the guest execution loop.
- `asbestos/asbestos.c`: owns the current `cpu_run_to_interrupt()` implementation.
- `asbestos/gen.c`: decodes x86 instructions and emits threaded-code gadget blocks.

The first step is to make those seams backend-neutral.

## Proposed architecture

### 1. Engine interface

Create `emu/engine.h`:

```c
#ifndef EMU_ENGINE_H
#define EMU_ENGINE_H

#include "misc.h"
#include "emu/tlb.h"

struct cpu_state;
struct mmu;

struct emu_engine_ops {
    const char *name;
    void *(*new)(struct mmu *mmu);
    void (*free)(void *engine);
    int (*run_to_interrupt)(void *engine, struct cpu_state *cpu, struct tlb *tlb);
    void (*poke)(void *engine, struct cpu_state *cpu);
    void (*invalidate_range)(void *engine, page_t start, page_t end);
    void (*invalidate_all)(void *engine);
};

struct emu_engine {
    const struct emu_engine_ops *ops;
    void *state;
};

const struct emu_engine_ops *emu_engine_default_ops(void);

#endif
```

Change `struct mmu` in `emu/mmu.h` from:

```c
struct asbestos *asbestos;
```

to:

```c
struct emu_engine engine;
```

Then update `kernel/memory.c` to construct/free/invalidate through the engine ops.

### 2. Asbestos adapter

Move the public Asbestos functions behind adapter ops:

- `asbestos_engine_new(struct mmu *)`
- `asbestos_engine_free(void *)`
- `asbestos_engine_run_to_interrupt(void *, struct cpu_state *, struct tlb *)`
- `asbestos_engine_poke(void *, struct cpu_state *)`
- `asbestos_engine_invalidate_range(void *, page_t, page_t)`
- `asbestos_engine_invalidate_all(void *)`

Keep the current implementation unchanged internally. This is the compatibility
baseline; all tests should still pass with `-Dengine=asbestos` before any JIT work
starts.

### 3. JIT memory allocator

Create a platform-aware allocator, likely under `emu/jit/`:

- `emu/jit/jit_memory.h`
- `emu/jit/jit_memory.c`
- `platform/jit_memory_darwin.c` or conditional sections in one file

Required behavior:

- allocate executable memory only for targets that explicitly permit it;
- enforce W^X: pages are writable during code emission and executable during run;
- invalidate instruction cache after writing machine code;
- fail closed on iOS App Store builds.

Possible API:

```c
struct jit_region;

struct jit_region *jit_region_new(size_t capacity);
int jit_region_begin_write(struct jit_region *region);
int jit_region_end_write(struct jit_region *region, void *start, size_t size);
void *jit_region_alloc_code(struct jit_region *region, size_t size, size_t alignment);
void jit_region_free(struct jit_region *region);
```

Darwin/macOS details:

- use `MAP_JIT` only when supported and entitled;
- use `pthread_jit_write_with_callback_np()` or `pthread_jit_write_protect_np()`
  where available;
- call `sys_icache_invalidate()` after code emission.

Linux details:

- `mmap(PROT_READ | PROT_WRITE)` for generation;
- `mprotect(PROT_READ | PROT_EXEC)` before execution;
- `__builtin___clear_cache()` after code emission.

### 4. JIT block model

Create `emu/jit/jit_engine.h` and `emu/jit/jit_engine.c` with a block cache similar
to Asbestos:

```c
struct jit_block {
    addr_t addr;
    addr_t end_addr;
    void *code;
    size_t code_size;
    struct list chain;
    struct list page[2];
    bool invalidated;
};
```

The JIT engine should preserve the same lifecycle rules as Asbestos:

- lookup by guest `eip`;
- compile missing block;
- execute until an interrupt or block exit;
- invalidate blocks when guest pages are unmapped, made writable, or changed;
- defer freeing executable blocks until no thread can be executing them.

### 5. Code generation strategy

A direct x86-to-host JIT is the real work. Start with a tiny subset and compare
against Asbestos at every step.

Initial subset:

- register-to-register and immediate `mov`;
- `add`, `sub`, `and`, `or`, `xor`, `cmp`, `test` for 32-bit registers;
- unconditional direct relative jumps;
- simple conditional jumps using materialized flags;
- `int 0x80` / syscall interrupt exit;
- block exit for unsupported instructions.

The first milestone does **not** need memory operands. Add memory operands only
after the register-only path is reliable.

Host targets:

- AArch64 first for Apple silicon/iOS device relevance.
- x86_64 later if macOS Intel/Linux testing still matters.

Generated-code ABI:

```c
typedef int (*jit_block_entry_t)(struct cpu_state *cpu, struct tlb *tlb);
```

Return values should match existing interrupt constants:

- `INT_NONE` if the block falls through and dispatch should continue;
- `INT_SYSCALL`, `INT_GPF`, `INT_UNDEFINED`, `INT_TIMER`, etc. when the kernel
  should handle an event.

### 6. Decoder extraction

`asbestos/gen.c` currently couples x86 decoding directly to gadget emission. For a
maintainable JIT, split decoding from emission:

- create `emu/decode_ir.h` with a small instruction IR;
- create `emu/decode_ir.c` that reuses `modrm_decode32()` and TLB reads to parse
  one instruction;
- make Asbestos either keep its current decoder initially or move to the shared IR
  later;
- make the JIT consume the IR.

Example IR shape:

```c
enum x86_ir_op {
    X86_IR_MOV,
    X86_IR_ALU,
    X86_IR_JMP,
    X86_IR_JCC,
    X86_IR_INT,
    X86_IR_UNSUPPORTED,
};

struct x86_ir_instr {
    enum x86_ir_op op;
    addr_t orig_ip;
    addr_t next_ip;
    /* operands, sizes, condition code, immediates */
};
```

### 7. Memory operands and faults

Generated code must use the existing TLB/MMU path for guest memory:

- fast path can inline TLB hit checks;
- slow path calls helpers that ultimately use `mmu_translate()`;
- failed translation sets `tlb->segfault_addr` and exits with `INT_GPF`;
- writes must preserve COW and stack-growth behavior by using the same miss path
  that Asbestos uses through `tlb_handle_miss()`.

Do **not** duplicate the memory model in the JIT.

### 8. Flags strategy

`struct cpu_state` stores lazy x86 flags using `res`, `op1`, `op2`, `flags_res`,
`cf`, and `of`. The JIT must either:

1. preserve the existing lazy-flag representation exactly, or
2. materialize flags before leaving a block and before every operation that needs
   them.

For the first milestone, prefer materializing flags at block boundaries and before
conditional branches. Optimize lazy flags later.

### 9. Fallback strategy

A safe incremental JIT needs fallback:

- if decoding sees an unsupported instruction, compile a block exit to Asbestos;
- execute one Asbestos block or one Asbestos single-step;
- return to JIT dispatch after the fallback updates `cpu->eip`.

This allows progressive instruction coverage without breaking the whole emulator.

### 10. Build integration

Update `meson_options.txt`:

```meson
option('engine', type: 'combo', choices: ['asbestos', 'jit', 'unicorn'], value: 'asbestos')
```

Update `meson.build`:

- keep `engine=asbestos` as default;
- allow `engine=jit` only for supported host/platform combinations;
- explicitly error for iOS/App Store builds unless a development-only flag is set;
- keep `engine=unicorn` scoped to the existing Linux-kernel experiment unless a
  separate iSH-kernel adapter is written.

### 11. Testing strategy

Before JIT work:

- make the engine abstraction land with zero behavior change;
- run existing syntax/build/e2e tests with Asbestos.

For each JIT milestone:

- add instruction-level comparison tests against Asbestos;
- run `tests/e2e/e2e.bash` where toolchain/rootfs are available;
- use `tools/unicornomatic.c` or a similar comparator for register/memory checks;
- add focused tests for page faults, COW, stack growth, and syscalls;
- benchmark simple loops only after correctness tests pass.

Suggested commands once the local VDSO toolchain is fixed:

```bash
meson setup build-asbestos -Dengine=asbestos
ninja -C build-asbestos
meson test -C build-asbestos

meson setup build-jit -Dengine=jit
ninja -C build-jit
meson test -C build-jit
```

## Task breakdown

### Phase 1: backend-neutral engine interface

1. Create `emu/engine.h`.
2. Replace `struct mmu.asbestos` with `struct emu_engine engine`.
3. Add Asbestos engine ops adapter.
4. Route `cpu_run_to_interrupt()`, `cpu_poke()`, and invalidation through ops.
5. Verify Asbestos behavior is unchanged.

### Phase 2: JIT memory/platform support

1. Add `emu/jit/jit_memory.*`.
2. Implement Linux/macOS W^X allocation.
3. Add explicit unsupported status for ordinary iOS builds.
4. Add a tiny executable-code smoke test for supported host builds.

### Phase 3: minimal JIT block runtime

1. Add `emu/jit/jit_engine.*`.
2. Implement block cache, lookup, invalidation, and deferred free.
3. Emit one hard-coded test block in a test binary to validate the ABI.
4. Integrate `engine=jit` as an experimental build option.

### Phase 4: decoder and first instruction subset

1. Add shared instruction IR for a tiny subset.
2. Generate AArch64 code for register-only arithmetic/data movement.
3. Add direct branch and syscall exits.
4. Add fallback to Asbestos for unsupported instructions.
5. Compare CPU state after each block against Asbestos.

### Phase 5: memory operands and compatibility

1. Inline or call TLB read/write helpers.
2. Handle page faults and write faults exactly like Asbestos.
3. Add COW and stack-growth tests.
4. Expand instruction coverage using existing e2e failures as the queue.

### Phase 6: performance and production decision

1. Benchmark against Asbestos on representative workloads.
2. Optimize hot paths only after correctness parity.
3. Decide whether the JIT remains dev-only or if the project changes distribution
   assumptions.
4. Document the unsupported iOS/App Store status prominently.

## Open questions

- Is a non-App-Store/dev-signed iOS build an acceptable target, or must every engine
  be shippable through TestFlight/App Store?
- Should the first backend be a native AArch64 emitter or an integration with an
  existing engine such as Unicorn/QEMU TCG?
- Is maintaining Asbestos fallback acceptable long-term, or should the JIT aim for
  full replacement before merge?
- What minimum benchmark justifies the added complexity?

## Recommendation

Do not replace Asbestos in the shipping iOS app. Start with Phase 1 so the emulator
backend is abstracted cleanly, then prototype a macOS/Linux/dev-only JIT behind
`-Dengine=jit`. Treat App Store-compatible iOS JIT support as blocked unless Apple
or the project's distribution model changes.
