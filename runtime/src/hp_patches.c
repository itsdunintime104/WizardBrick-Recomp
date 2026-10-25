/* Game patches keyed by guest call site. Contract: runtime/include/hp_patches.h. */
#include <stddef.h>

#include "hp_patches.h"

struct hp_sleep_clamp {
    uint32_t guest_ret;
    uint32_t max_ms;
};

static const struct hp_sleep_clamp g_sleep_clamps[] = {
    /* sub_00966E70, the game's load limiter: while a background load job is
     * queued or running it sleeps the game thread toward a 1/60 s frame, and
     * loads advance once per frame (notes/slow_frames.md, "Answer"). 2 ms, as
     * the LEGO Harry Potter Years 5-7 fix does for its limiter
     * (github.com/illusion-pasture-program/lhp2-performance-fix,
     * src/lhp2_d3d9_fix.c). */
    {0x00966F3Cu, 2u},
};

uint32_t hp_patch_sleep_ms(uint32_t guest_ret, uint32_t ms)
{
    for (size_t i = 0; i < sizeof g_sleep_clamps / sizeof g_sleep_clamps[0]; i++) {
        const struct hp_sleep_clamp *c = &g_sleep_clamps[i];
        if (c->guest_ret == guest_ret && ms > c->max_ms)
            return c->max_ms;
    }
    return ms;
}
