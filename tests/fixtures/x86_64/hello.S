/*
 * x86-64 static hello world test fixture
 * 
 * Assembles a minimal x86-64 Linux program that:
 * 1. Writes "hi\n" to stdout using write(1, buf, 3)
 * 2. Exits with code 0 using exit(0)
 *
 * Assembly (Intel syntax):
 *   mov rax, 1        ; syscall number for write
 *   mov rdi, 1        ; fd = stdout
 *   lea rsi, [rip + msg]  ; buf = address of msg
 *   mov rdx, 3        ; count = 3
 *   syscall
 *   mov rax, 60       ; syscall number for exit
 *   xor rdi, rdi      ; status = 0
 *   syscall
 * msg:
 *   .ascii "hi\n"
 */

.global _start
.text
_start:
    /* write(1, msg, 3) */
    movq    $1, %rax            /* syscall number: write (x86-64: 1) */
    movq    $1, %rdi            /* arg1: fd = 1 (stdout) */
    leaq    msg(%rip), %rsi     /* arg2: buf = msg (RIP-relative addressing) */
    movq    $3, %rdx            /* arg3: count = 3 */
    syscall                     /* make syscall */

    /* exit(0) */
    movq    $60, %rax           /* syscall number: exit (x86-64: 60) */
    xorq    %rdi, %rdi          /* arg1: status = 0 */
    syscall                     /* make syscall */

msg:
    .ascii  "hi\n"
