/* HP's shader cache warm-up, the game_boot_hook (engine
 * windows/runtime/include/game_hooks.h). Moved from the boot driver. */
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "d3dx_guest.h"
#include "game_hooks.h"
#include "guest_call.h"
#include "guest_mem.h"
#include "host_funcs.h"
#include "recomp_types.h"
#include "win32_fs.h"
#include "win32_thunk.h"

static char g_warm_save_dir[1024];

/* ---- Shader cache warm-up (HPRECOMP_WARM_SHADERS=1) ---------------------
 * Runs the game's own shader dump routine, sub_00A3C660, which the retail game
 * never calls (notes/shader_oracle.md, "Why -DumpShaders does not work"). It
 * reads C:\Game\filetable.txt (one quoted game path per line), loads each
 * _nxg.gsc/_nxg.ghg scene and renders one frame of it, so the game builds and
 * caches those shaders. HPRECOMP_WARM_FILETABLE names the host file
 * (tools/shaders/scene_list.py writes it); it is copied to <save>/Game, which
 * win32_fs reads before the game dir, and removed after the run.
 * The routine runs from the message pump at the end of a frame (PeekMessageA
 * in sub_009D42E0, called by sub_00A3B5A0, the last call of the frame function
 * sub_00A3B930 that the main loop sub_00A3BF40 calls; stack from
 * /usr/bin/sample) after HPRECOMP_WARM_AFTER frames (default 300), so the
 * device and the scene system are up. The routine itself renders each scene
 * with sub_00A36270 and sub_00A3B930. Then the process exits with status 0.
 *
 * The routine frees each scene after its frame (0x00A3C807 calls
 * sub_00A3ACD0), so a scene that uses another scene's shared texture finds
 * none: the lookup sub_009169F0 at 0x009E1E1A returns 0, and with the byte at
 * WARM_PLACEHOLDER_FLAG clear sub_009E1AE0 goes on with an empty texture,
 * then reads [0 + 0x20] at 0x009E218F. With the byte set, the game's own
 * fallback (0x009E1E96) gives that texture a 32x32 placeholder instead. No
 * game code writes the byte; besides 0x009E1E8D it is read only by
 * sub_00A3CA00 (set-up of the object at 0x01043358) and by the CachedShaders
 * writer sub_009F5D80, where a set byte allows writing. It is set only for
 * the warm-up. */
#define WARM_DUMP_VA 0x00A3C660u
#define WARM_PLACEHOLDER_FLAG 0x010D4C3Eu
#define WARM_PEEK_RET 0x009D42F8u  /* first PeekMessageA in sub_009D42E0 */
/* sub_00A3B5A0's two frame-path calls of sub_009D42E0 (0x00A3B661, 0x00A3B750) */
#define WARM_LOOP_RET1 0x00A3B666u
#define WARM_LOOP_RET2 0x00A3B755u
static recomp_func_t g_warm_peek;
static long g_warm_after, g_warm_frames;
static char g_warm_table[1200];

static void warm_peek(void)
{
    /* At entry: [esp] = return, 5 args, then sub_009D42E0's pushes of ebx,
     * esi, edi and its own return address. */
    uint32_t ret = MEM32(g_esp), caller = MEM32(g_esp + 36u);
    g_warm_peek();
    if (!pthread_main_np() || ret != WARM_PEEK_RET ||
        (caller != WARM_LOOP_RET1 && caller != WARM_LOOP_RET2) ||
        ++g_warm_frames != g_warm_after)
        return;
    fprintf(stderr, "hp_warm: frame %ld, calling sub_%08X\n", g_warm_frames, WARM_DUMP_VA);
    MEM8(WARM_PLACEHOLDER_FLAG) = 1;
    fprintf(stderr, "hp_warm: missing shared textures use the game's placeholder\n");
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    guest_call_cdecl(WARM_DUMP_VA, 0, NULL);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    fprintf(stderr, "hp_warm: done in %.1f s\n",
            (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9);
    unlink(g_warm_table);
    d3dx_game_cache_stamp(g_warm_save_dir);
    fflush(NULL);
    /* The render and audio threads are still running; exit without the
     * atexit teardown. The cache files are already closed (WriteFile and
     * CloseHandle go straight to the host). */
    _exit(0);
}

static uint32_t resolve_warm(const char *dll, const char *name, uint16_t ordinal, int by_ordinal)
{
    uint32_t va = win32_resolve_import(dll, name, ordinal, by_ordinal);
    if (by_ordinal || strcmp(name, "PeekMessageA") != 0 || va == 0)
        return va;
    g_warm_peek = guest_resolve(va);
    return g_warm_peek != NULL ? host_func_va(warm_peek) : va;
}

/* Copy the filetable into <save>/Game. 0 on success. */
static int warm_setup(const char *save_dir)
{
    const char *src = getenv("HPRECOMP_WARM_FILETABLE");
    const char *after = getenv("HPRECOMP_WARM_AFTER");
    g_warm_after = after != NULL ? atol(after) : 300;
    if (src == NULL || g_warm_after < 1) {
        fprintf(stderr, "hp_warm: set HPRECOMP_WARM_FILETABLE (and HPRECOMP_WARM_AFTER >= 1)\n");
        return -1;
    }
    char dir[1200];
    snprintf(dir, sizeof dir, "%s/Game", save_dir);
    snprintf(g_warm_table, sizeof g_warm_table, "%s/filetable.txt", dir);
    FILE *in = fopen(src, "rb"), *out = NULL;
    if (in == NULL || w32_fs_mkdir_p(dir) != 0 || (out = fopen(g_warm_table, "wb")) == NULL) {
        fprintf(stderr, "hp_warm: copy %s to %s: %s\n", src, g_warm_table, strerror(errno));
        if (in != NULL)
            fclose(in);
        return -1;
    }
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0)
        fwrite(buf, 1, n, out);
    fclose(in);
    if (fclose(out) != 0)
        return -1;
    return 0;
}

int game_boot_hook(const char *save_dir, pe_import_resolver_t *resolve)
{
    const char *warm = getenv("HPRECOMP_WARM_SHADERS");
    if (warm == NULL || strcmp(warm, "1") != 0)
        return 0;
    snprintf(g_warm_save_dir, sizeof g_warm_save_dir, "%s", save_dir);
    if (warm_setup(save_dir) != 0)
        return -1;
    *resolve = resolve_warm;
    return 0;
}
