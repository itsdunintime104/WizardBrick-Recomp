/* M1 entry point: map the original .exe into guest memory and report the
 * image. It does not call the entry point or any lifted function; the Win32
 * layer is not linked yet, so every import stays unbound. */
#include <stdint.h>
#include <stdio.h>

#include "guest_mem.h"
#include "pe_load.h"

static uint32_t resolve_none(const char *dll, const char *name, uint16_t ordinal,
                             int by_ordinal)
{
    (void)dll;
    (void)name;
    (void)ordinal;
    (void)by_ordinal;
    return 0;
}

int main(int argc, char **argv)
{
    pe_image_t img;
    if (argc < 2) {
        fprintf(stderr, "usage: %s <lhp_win32.exe>\n", argv[0]);
        return 2;
    }
    if (guest_mem_init() != 0)
        return 1;
    if (pe_load_image(argv[1], resolve_none, &img) != 0)
        return 1;
    printf("image 0x%08X-0x%08X entry 0x%08X unbound %u\n", img.image_base,
           img.image_end, img.entry_va, img.unbound_imports);
    return 0;
}
