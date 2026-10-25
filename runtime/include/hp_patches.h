/* Game patches keyed by the guest call site (the return address a guest call
 * pushes). A host import asks here before it does its work. Table and reasons:
 * runtime/src/hp_patches.c. */
#ifndef HP_PATCHES_H
#define HP_PATCHES_H

#include <stdint.h>

/* The milliseconds a Sleep(ms) called from guest_ret sleeps: ms, or less
 * for a patched call site. */
uint32_t hp_patch_sleep_ms(uint32_t guest_ret, uint32_t ms);

#endif
