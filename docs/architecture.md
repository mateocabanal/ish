# Architecture overview

This document is a contributor-oriented map of how iSH fits together. It is
not a full design specification; it is meant to point new readers at the files
that explain each subsystem in code.

## What iSH runs

iSH runs a 32-bit x86 Linux userland on top of iOS/macOS/Linux host APIs. The
main pieces are:

1. **An x86 userspace emulator** (`emu/`, `asbestos/`) that executes guest
   instructions.
2. **A Linux-like kernel layer** (`kernel/`) that owns tasks, address spaces,
   signals, ELF loading, and syscall implementations.
3. **A virtual filesystem layer** (`fs/`) that presents Linux filesystem
   semantics while storing data on the host filesystem.
4. **Host integration** (`app/`, `platform/`, `main.c`, `xX_main_Xx.h`) that
   boots either the iOS app or the command-line test binary and connects stdio,
   terminals, pasteboard/location devices, DNS, and platform-specific behavior.

At runtime, guest code runs until the emulator reports an interrupt. The kernel
layer interprets that interrupt as a syscall, page fault, timer, signal, illegal
instruction, or debug event and updates the guest CPU/task state before the
emulator resumes.

## Build surfaces

The repository has two main ways to exercise the shared core:

- **iOS app / Xcode build**: `AppDelegate.m` mounts the app's selected root,
  registers iOS-only filesystems/devices, creates the first Linux task, executes
  the configured boot command, and starts that task on a pthread.
- **Command-line test binary / Meson build**: `main.c` and `xX_main_Xx.h` parse
  CLI options, mount a root filesystem, execute the requested guest program, set
  up stdio, and run the current task in the foreground.

The Meson graph in `meson.build` builds reusable static libraries for the
emulator (`libish_emu`), fake filesystem metadata (`libfakefs`), and the iSH
kernel/filesystem layer (`libish`). It also builds support tools such as
`fakefsify` and `ptraceomatic`.

## Startup paths

### Command-line path

1. `main.c` preserves `TERM` in the NUL-separated environment format expected by
   `do_execve`.
2. `xX_main_Xx()` parses options:
   - `-r <dir>` mounts a host directory through `realfs`.
   - `-f <dir>` mounts a fakefs root; the real data directory is `<dir>/data`.
   - `-d <dir>` chooses the guest working directory.
   - `-c <dev>` chooses the console device.
3. `mount_root()` mounts the root filesystem.
4. `become_first_process()` creates PID 1, its thread group, address space,
   signal handlers, file descriptor table, and root/pwd file handles.
5. `do_execve()` loads the requested 32-bit x86 ELF into the task's memory.
6. stdio is connected either to the chosen tty device or to host pipe-backed
   file descriptors.
7. `main.c` creates common device nodes, mounts `/proc` and `/dev/pts`, then
   calls `task_run_current()`.

### iOS app path

`-[AppDelegate boot]` performs the same kernel-level boot but replaces CLI
plumbing with iOS integration:

1. The selected root from `Roots` is mounted as `fakefs`.
2. iOS filesystem providers (`iosfs`, `iosfs_unsafe`) and dynamic devices such
   as `/dev/clipboard` and `/dev/location` are registered.
3. PID 1 is created with `become_first_process()`.
4. Standard device nodes, `/proc`, and `/dev/pts` are created/mounted.
5. DNS information is written into `/etc/resolv.conf`.
6. stdio is wired to the iOS console driver.
7. The user-configured boot command is executed and `task_start(current)` moves
   the guest task onto its own pthread.

## Guest execution loop

`task_run_current()` in `kernel/task.c` is the central loop:

1. Refresh a TLB for the task's current address space.
2. Take a read lock on guest memory.
3. Call `cpu_run_to_interrupt()` to execute guest x86 code.
4. Release the read lock.
5. Dispatch the reported interrupt with `handle_interrupt()`.
6. Repeat forever, unless the guest exits through the normal process-exit path.

The `current` task is stored in thread-local storage (`__thread struct task
*current`) so code implementing syscalls can find the Linux task associated with
the host pthread that is currently executing guest code.

## Syscall dispatch

`handle_interrupt()` in `kernel/calls.c` handles `INT_SYSCALL` by treating guest
registers as the i386 Linux syscall ABI:

- `eax`: syscall number
- `ebx`, `ecx`, `edx`, `esi`, `edi`, `ebp`: up to six syscall arguments
- `eax`: return value after the syscall handler returns

`syscall_table[]` maps i386 syscall numbers to `sys_*` functions implemented
throughout `kernel/` and `fs/`. Missing or intentionally unsupported syscalls
return `_ENOSYS`; frequently-probed optional syscalls can use
`syscall_silent_stub()` to avoid noisy logs.

## Emulator and threaded-code engine

The default engine is Asbestos (`asbestos/`). It is not a machine-code JIT.
Instead, it decodes guest x86 instructions into a `fiber_block`: an array of
function pointers and inline operands. Each function pointer names a small
"gadget" implemented mostly in assembly under `asbestos/gadgets-*`. Gadgets
execute one small part of an instruction and tail-call the next gadget in the
array.

Important files:

- `asbestos/gen.c`: decodes guest instructions and emits gadget pointers plus
  operands into a `fiber_block`.
- `asbestos/asbestos.c`: caches, looks up, invalidates, and enters
  `fiber_block`s.
- `asbestos/gadgets-*/`: architecture-specific gadget implementations.
- `emu/tlb.c`: translates guest addresses to host pointers and invalidates cached
  translations when the memory map changes.
- `emu/cpu.h`: guest CPU state, including registers, lazy flags, FPU/SIMD state,
  TLS, and fault bookkeeping.

Asbestos caches blocks by starting guest instruction pointer. When a block ends
with a branch/call target encoded as a guest IP, `asbestos.c` may patch that
slot to point directly at another block's gadget array. Invalidating guest
memory disconnects affected blocks and puts them on a deferred free list so
another thread cannot jump through freed gadget code.

## Memory model

`kernel/memory.c`, `kernel/mm.h`, and `emu/tlb.c` implement the guest address
space:

- `struct mm` owns one `struct mem`, ELF metadata, brk bounds, and procfs-visible
  ranges.
- `struct mem` stores guest pages in a two-level page table.
- Page-table changes increment `mem->mmu.changes`; TLBs compare this counter to
  know when cached translations are stale.
- Copy-on-write is represented by shared `struct data` objects and page flags.
- `mem_ptr()` handles page faults that can be resolved internally, such as stack
  growth or copy-on-write; unresolved faults become guest `SIGSEGV`s.

## Filesystem model

The `fs/` directory implements Linux-style virtual filesystems and file
descriptor behavior. The two root-facing filesystems are:

- **`realfs`** (`fs/real.c`): forwards operations to host files under a mounted
  root directory, translating flags, `stat`, errors, polling, and directory
  iteration into iSH's Linux-like types.
- **`fakefs`** (`fs/fake.c`, `fs/fake-db.c`): stores file contents on the host but
  keeps Linux metadata in SQLite. This lets iSH preserve guest-visible mode,
  owner, group, device, inode, and symlink semantics even when the host
  filesystem cannot represent them directly.

Other filesystem modules provide `/proc`, devices, pipes, ptys, sockets, tmpfs,
path resolution, mounts, and file descriptor tables.

## ELF loading and process state

`kernel/exec.c` implements the core of `execve`:

1. Validate a 32-bit little-endian x86 ELF header.
2. Read program headers and optional interpreter (`PT_INTERP`).
3. Replace the task's address space.
4. Map loadable segments, zero BSS, set brk bounds, and map the interpreter if
   needed.
5. Build the guest stack with argv, envp, auxv, and vDSO information.
6. Set CPU registers so the next emulator entry starts at the ELF entry point.

Process and thread-group state lives in `kernel/task.h` and `kernel/task.c`.
`task_start()` creates a detached host pthread for a guest task; `task_run_current()`
runs the task associated with the current host thread.

## Debugging orientation

Useful switches and tools from the existing README:

- `strace` logging shows syscall parameters and return values.
- `instr` logging shows each emulated instruction and is very slow.
- `tools/ptraceomatic` can compare execution against a real Linux process on a
  supported Linux host.
- `tests/e2e/` contains shell-driven end-to-end tests for guest behavior.

When changing the emulator or memory model, also review cache invalidation paths
(`mem_changed`, `tlb_refresh`, `asbestos_invalidate_*`) because stale decoded
blocks or stale address translations can turn a local-looking change into a
cross-subsystem bug.
