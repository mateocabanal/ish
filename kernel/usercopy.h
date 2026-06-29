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

#endif /* KERNEL_USERCOPY_H */
