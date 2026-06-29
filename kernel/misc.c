#include <string.h>
#include "kernel/calls.h"

#define PRCTL_SET_KEEPCAPS_ 8
#define PRCTL_SET_NAME_ 15

int_t sys_prctl(dword_t option, uint_t arg2, uint_t UNUSED(arg3), uint_t UNUSED(arg4), uint_t UNUSED(arg5)) {
    switch (option) {
        case PRCTL_SET_KEEPCAPS_:
            // stub
            return 0;
        case PRCTL_SET_NAME_: {
            char name[16];
            if (user_read_string(arg2, name, sizeof(name) - 1))
                return _EFAULT;
            name[sizeof(name) - 1] = '\0';
            STRACE("prctl(PRCTL_SET_NAME, \"%s\")", name);
            strcpy(current->comm, name);
            return 0;
        }
        default:
            STRACE("prctl(%#x)", option);
            return _EINVAL;
    }
}

int_t sys_arch_prctl(int_t code, addr_t addr) {
    STRACE("arch_prctl(%#x, %#x)", code, addr);
    
    // x86-64 arch_prctl codes (from Linux arch/x86/include/uapi/asm/prctl.h)
    // These are only valid when cpu->mode == 1 (x86-64 mode)
    switch (code) {
        case 0x1001:  // ARCH_SET_GS
            if (current->cpu.mode == 1) {
                current->cpu.gs_base = addr;
                return 0;
            }
            break;
        case 0x1002:  // ARCH_SET_FS
            if (current->cpu.mode == 1) {
                STRACE("  setting fs_base to %#lx", (unsigned long)addr);
                current->cpu.fs_base = addr;
                return 0;
            }
            break;
        case 0x1003:  // ARCH_GET_FS
            if (current->cpu.mode == 1) {
                // Write 64-bit fs_base to guest address
                qword_t val = current->cpu.fs_base;
                if (user_write(addr, &val, sizeof(val)))
                    return _EFAULT;
                return 0;
            }
            break;
        case 0x1004:  // ARCH_GET_GS
            if (current->cpu.mode == 1) {
                qword_t val = current->cpu.gs_base;
                if (user_write(addr, &val, sizeof(val)))
                    return _EFAULT;
                return 0;
            }
            break;
        case 0x1005:  // ARCH_GET_CPUID
        case 0x1006:  // ARCH_SET_CPUID
            return 0;
        case 0x1011:  // ARCH_GET_XCOMP_SUPP
        case 0x1012:  // ARCH_GET_XCOMP_PERM
        case 0x1021:  // ARCH_REQ_XCOMP_PERM
            return _EINVAL;
        default:
            break;
    }
    
    return _EINVAL;
}

// 64-bit version of arch_prctl for x86-64 syscall ABI
qword_t sys_arch_prctl64(qword_t code, guest64_addr_t addr) {
    int_t code32 = (int_t)code;
    
    switch (code32) {
        case 0x1001:  // ARCH_SET_GS
            STRACE("arch_prctl64(ARCH_SET_GS, %#lx)", (unsigned long)addr);
            current->cpu.gs_base = addr;
            return 0;
        case 0x1002:  // ARCH_SET_FS
            STRACE("arch_prctl64(ARCH_SET_FS, %#lx)", (unsigned long)addr);
            current->cpu.fs_base = addr;
            return 0;
        case 0x1003:  // ARCH_GET_FS
            STRACE("arch_prctl64(ARCH_GET_FS, %#lx)", (unsigned long)addr);
            if (user_write64(addr, &current->cpu.fs_base, sizeof(qword_t)))
                return (qword_t)-_EFAULT;
            return 0;
        case 0x1004:  // ARCH_GET_GS
            STRACE("arch_prctl64(ARCH_GET_GS, %#lx)", (unsigned long)addr);
            if (user_write64(addr, &current->cpu.gs_base, sizeof(qword_t)))
                return (qword_t)-_EFAULT;
            return 0;
        case 0x3001:  // ARCH_CET_STATUS
        case 0x3002:  // ARCH_CET_DISABLE
        case 0x3003:  // ARCH_CET_LOCK
            // CET (Control-flow Enforcement Technology) - not supported
            return (qword_t)-_EINVAL;
        default:
            STRACE("arch_prctl64(%#x, %#lx) - unknown code", code32, (unsigned long)addr);
            return (qword_t)-_EINVAL;
    }
}

#define REBOOT_MAGIC1 0xfee1dead
#define REBOOT_MAGIC2 672274793
#define REBOOT_MAGIC2A 85072278
#define REBOOT_MAGIC2B 369367448
#define REBOOT_MAGIC2C 537993216

#define REBOOT_CMD_CAD_OFF 0
#define REBOOT_CMD_CAD_ON 0x89abcdef

int_t sys_reboot(int_t magic, int_t magic2, int_t cmd) {
    STRACE("reboot(%#x, %d, %d)", magic, magic2, cmd);
    if (!superuser())
        return _EPERM;
    if (magic != (int) REBOOT_MAGIC1 ||
            (magic2 != REBOOT_MAGIC2 &&
             magic2 != REBOOT_MAGIC2A &&
             magic2 != REBOOT_MAGIC2B &&
             magic2 != REBOOT_MAGIC2C))
        return _EINVAL;

    switch (cmd) {
        case REBOOT_CMD_CAD_ON:
        case REBOOT_CMD_CAD_OFF:
            return 0;
        default:
            return _EPERM;
    }
}
