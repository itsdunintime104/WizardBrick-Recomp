/* hp_patches: the load limiter's Sleep (return address 0x00966F3C) is
 * clamped to 2 ms; other call sites keep their time. */
#include <stdio.h>

#include "hp_patches.h"

static int g_fail;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fail = 1;                                                  \
        }                                                                \
    } while (0)

int main(void)
{
    CHECK(hp_patch_sleep_ms(0x00966F3Cu, 16u) == 2u);
    CHECK(hp_patch_sleep_ms(0x00966F3Cu, 2u) == 2u);
    CHECK(hp_patch_sleep_ms(0x00966F3Cu, 1u) == 1u);
    CHECK(hp_patch_sleep_ms(0x00966F3Cu, 0u) == 0u);
    CHECK(hp_patch_sleep_ms(0x00966F38u, 16u) == 16u);
    CHECK(hp_patch_sleep_ms(0x00401000u, 1000u) == 1000u);
    if (g_fail)
        return 1;
    puts("test_hp_patches: OK");
    return 0;
}
