#ifndef KERNEL_USERCOPY_H
#define KERNEL_USERCOPY_H

#include "misc.h"
#include "kernel/calls.h"

/*
 * ABI-specific user memory copy helpers.
 *
 * These functions handle the difference between 32-bit and 64-bit guest
 * pointers when copying data between guest and host memory. Use these
 * instead of user_put/user_get when the pointer size depends on the ABI.
 */

/*
 * Write a guest32 pointer to user memory.
 * @dst: destination address in guest memory
 * @value: 32-bit pointer value to write
 * Returns: 0 on success, negative error on failure
 */
static inline int user_put_guest32_ptr(addr_t dst, guest32_addr_t value) {
    return user_put(dst, value);
}

/*
 * Write a guest64 pointer to user memory.
 * @dst: destination address in guest memory
 * @value: 64-bit pointer value to write
 * Returns: 0 on success, negative error on failure
 */
static inline int user_put_guest64_ptr(addr_t dst, guest64_addr_t value) {
    return user_put(dst, value);
}

/*
 * Read a guest32 pointer from user memory.
 * @src: source address in guest memory
 * @value: output location for 32-bit pointer value
 * Returns: 0 on success, negative error on failure
 */
static inline int user_get_guest32_ptr(addr_t src, guest32_addr_t *value) {
    return user_get(src, *value);
}

/*
 * Read a guest64 pointer from user memory.
 * @src: source address in guest memory
 * @value: output location for 64-bit pointer value
 * Returns: 0 on success, negative error on failure
 */
static inline int user_get_guest64_ptr(addr_t src, guest64_addr_t *value) {
    return user_get(src, *value);
}

/*
 * 64-bit user memory access functions for x86-64 support.
 * These handle guest addresses that may be above 4GB.
 * 
 * NOTE: These are stub implementations. Full support requires
 * a sparse memory model (see docs/x86_64-support.md Milestone 2).
 * For now, these only work for addresses in the lower 32-bit range.
 */

static inline int user_put64(guest64_addr_t addr, byte_t val) {
    if (addr > 0x7fffffff) return -1;  // TODO: implement full 64-bit support
    return user_put((addr_t)addr, val);
}

static inline int user_get64(guest64_addr_t addr, byte_t *val) {
    if (addr > 0x7fffffff) return -1;
    return user_get((addr_t)addr, *val);
}

static inline int user_write64(guest64_addr_t addr, const void *data, size_t len) {
    if (addr > 0x7fffffff || addr + len > 0x7fffffff) return -1;
    return user_write((addr_t)addr, data, len);
}

static inline int user_read64(guest64_addr_t addr, void *out, size_t len) {
    if (addr > 0x7fffffff || addr + len > 0x7fffffff) return -1;
    return user_read((addr_t)addr, out, len);
}

#endif /* KERNEL_USERCOPY_H */
