/* Boot driver: does what the Windows loader does for lhp_win32.exe, then
 * runs the lifted PE entry point on the host main thread. The pattern follows
 * the engine's startup.c (guest memory, image, dispatch, main guest stack,
 * entry through recomp_lookup), with the PE loader and the Win32 slices in
 * place of the Mach-O loader and the POSIX thunks. */
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <SDL2/SDL.h>

#include "d3dx_guest.h"
#include "frame_trace.h"
#include "recomp_types.h"
#include "diag.h"
#include "guest_call.h"
#include "guest_heap.h"
#include "guest_mem.h"
#include "host_funcs.h"
#include "pe_load.h"
#include "win32_fs.h"
#include "win32_thunk.h"
#include "win32_ui.h"

int recomp_dispatch_init(void);
/* Generated with the import inventory (win32_unsupported.c). */
const char *win32_inventory_name(const char *dll, uint16_t ordinal);

/* Main guest stack: the engine's main-stack range (guest_call.h). */
#define MAIN_STACK_LO 0xFD000000u
#define MAIN_STACK_HI 0xFE000000u
/* Return address under the entry point. Never mapped. */
#define ENTRY_RET_SENTINEL 0xFFFFF000u

#define PE_SCN_MEM_EXECUTE 0x20000000u

/* ---- Import call trace (HP_TRACE_IMPORTS=N) ------------------------------
 * Format of notes/golden_trace.md "Logging spec": one line per call for the
 * first N calls,
 *   IMP <n> t=<tid> <DLL>!<Func>(<a0> ... <aK-1>) ret <RET> -> <EAX>
 * and `THR <tid> <name>` for each MSVC SetThreadName exception (0x406D1388).
 * n is taken at entry; tid is the TIB's thread id (what GetCurrentThreadId
 * returns); K is the number of argument slots the thunk popped, measured
 * from g_esp (0 for a cdecl import). Every IAT slot gets a trampoline, and so
 * does every non-zero address GetProcAddress returns, so calls through
 * function pointers are traced too. Each traced function needs its own
 * trampoline, because a thunk has no context argument. */
#define TRACE_SLOTS 512
#define TRACE_MAX_ARGS 16
#define TIB_TID 0x24u
#define MS_VC_EXCEPTION 0x406D1388u
static recomp_func_t g_trace_fn[TRACE_SLOTS];
static char g_trace_name[TRACE_SLOTS][80];
static unsigned g_trace_count;
static long g_trace_limit;
static _Atomic long g_trace_calls;
static pthread_mutex_t g_trace_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_slot_getprocaddress = -1, g_slot_getmodulehandle = -1, g_slot_loadlibrary = -1,
           g_slot_raiseexception = -1;

/* Module handle -> DLL name, from traced GetModuleHandleA/LoadLibraryA. */
#define TRACE_MODULES 32
static struct { uint32_t handle; char name[48]; } g_modules[TRACE_MODULES];
static unsigned g_module_count;

static uint32_t trace_tid(void)
{
    return g_fs_base != 0 ? MEM32(g_fs_base + TIB_TID) : 0;
}

static void guest_cstr(uint32_t a, char *out, size_t size)
{
    size_t i = 0;
    for (; a != 0 && i + 1 < size && MEM8(a + i) != 0; i++)
        out[i] = (char)MEM8(a + i);
    out[i] = '\0';
}

static uint32_t trace_wrap(recomp_func_t fn, const char *name, int *slot);

static void trace_call(unsigned k)
{
    long n = g_trace_calls++;
    if (n >= g_trace_limit) {
        g_trace_fn[k]();
        return;
    }
    uint32_t esp0 = g_esp, ret = MEM32(esp0), tid = trace_tid(), a[TRACE_MAX_ARGS];
    /* Read argument slots only below the thread's stack base (TIB +4): the
     * first calls run near the top of the stack, and above it is unmapped. */
    uint32_t base = g_fs_base != 0 ? MEM32(g_fs_base + 4u) : esp0 + 4u;
    for (unsigned i = 0; i < TRACE_MAX_ARGS; i++) {
        uint32_t slot = esp0 + 4u + 4u * i;
        a[i] = slot + 4u <= base ? MEM32(slot) : 0;
    }
    if ((int)k == g_slot_raiseexception && a[0] == MS_VC_EXCEPTION && a[2] >= 3 && a[3] != 0 &&
        MEM32(a[3]) == 0x1000u) {
        char name[64];
        uint32_t who = MEM32(a[3] + 8);
        guest_cstr(MEM32(a[3] + 4), name, sizeof name);
        fprintf(stderr, "THR %04X %s\n", who == 0xFFFFFFFFu ? tid : who, name);
    }
    g_trace_fn[k]();
    uint32_t eax = g_eax;
    if ((int)k == g_slot_getprocaddress && eax != 0 && (a[1] >> 16) != 0) {
        char dll[48] = "?", proc[64], full[120];
        pthread_mutex_lock(&g_trace_lock);
        for (unsigned i = 0; i < g_module_count; i++)
            if (g_modules[i].handle == a[0])
                snprintf(dll, sizeof dll, "%s", g_modules[i].name);
        pthread_mutex_unlock(&g_trace_lock);
        guest_cstr(a[1], proc, sizeof proc);
        snprintf(full, sizeof full, "%s!%s", dll, proc);
        recomp_func_t fn = guest_resolve(eax);
        if (fn != NULL)
            g_eax = eax = trace_wrap(fn, full, NULL);
    } else if (((int)k == g_slot_getmodulehandle || (int)k == g_slot_loadlibrary) && eax != 0 &&
               a[0] != 0) {
        pthread_mutex_lock(&g_trace_lock);
        if (g_module_count < TRACE_MODULES) {
            g_modules[g_module_count].handle = eax;
            guest_cstr(a[0], g_modules[g_module_count].name, sizeof g_modules[0].name);
            g_module_count++;
        }
        pthread_mutex_unlock(&g_trace_lock);
    }
    uint32_t popped = g_esp - esp0 >= 4u ? (g_esp - esp0 - 4u) / 4u : 0;
    unsigned nargs = popped < TRACE_MAX_ARGS ? popped : TRACE_MAX_ARGS;
    char args[TRACE_MAX_ARGS * 9 + 1];
    size_t off = 0;
    args[0] = '\0';
    for (unsigned i = 0; i < nargs; i++)
        off += (size_t)snprintf(args + off, sizeof args - off, i ? " %08X" : "%08X", a[i]);
    fprintf(stderr, "IMP %ld t=%04X %s(%s) ret %08X -> %08X\n", n, tid, g_trace_name[k], args, ret,
            eax);
}

/* k is three octal digits, so 0##k is the slot index 0..511. */
#define T1(k) static void trace_##k(void) { trace_call(0##k); }
#define T8(k) T1(k##0) T1(k##1) T1(k##2) T1(k##3) T1(k##4) T1(k##5) T1(k##6) T1(k##7)
#define T64(k) T8(k##0) T8(k##1) T8(k##2) T8(k##3) T8(k##4) T8(k##5) T8(k##6) T8(k##7)
T64(0) T64(1) T64(2) T64(3) T64(4) T64(5) T64(6) T64(7)
#define R1(k) trace_##k,
#define R8(k) R1(k##0) R1(k##1) R1(k##2) R1(k##3) R1(k##4) R1(k##5) R1(k##6) R1(k##7)
#define R64(k) R8(k##0) R8(k##1) R8(k##2) R8(k##3) R8(k##4) R8(k##5) R8(k##6) R8(k##7)
static const recomp_func_t g_trampolines[TRACE_SLOTS] = {
    R64(0) R64(1) R64(2) R64(3) R64(4) R64(5) R64(6) R64(7)};

/* The guest address of fn's trampoline, made on first use; fn's own
 * address when the pool is full. *slot (if given) gets the slot or -1. */
static uint32_t trace_wrap(recomp_func_t fn, const char *name, int *slot)
{
    if (slot != NULL)
        *slot = -1;
    pthread_mutex_lock(&g_trace_lock);
    unsigned k = 0;
    while (k < g_trace_count && g_trace_fn[k] != fn)
        k++;
    if (k == g_trace_count) {
        if (k == TRACE_SLOTS) {
            pthread_mutex_unlock(&g_trace_lock);
            fprintf(stderr, "hp_boot: trace pool full, %s not traced\n", name);
            return host_func_va(fn);
        }
        g_trace_fn[k] = fn;
        snprintf(g_trace_name[k], sizeof g_trace_name[k], "%s", name);
        g_trace_count++;
    }
    pthread_mutex_unlock(&g_trace_lock);
    if (slot != NULL)
        *slot = (int)k;
    return host_func_va(g_trampolines[k]);
}

/* HPRECOMP_FRAME_TRACE: every import through a timed trampoline. */
static uint32_t resolve_timed(const char *dll, const char *name, uint16_t ordinal, int by_ordinal)
{
    uint32_t va = win32_resolve_import(dll, name, ordinal, by_ordinal);
    recomp_func_t fn = va != 0 ? guest_resolve(va) : NULL;
    if (fn == NULL)
        return va;
    char full[120];
    if (by_ordinal)
        snprintf(full, sizeof full, "%s!#%u", dll, ordinal);
    else
        snprintf(full, sizeof full, "%s!%s", dll, name);
    return ft_wrap(fn, full);
}

static uint32_t resolve_traced(const char *dll, const char *name, uint16_t ordinal,
                               int by_ordinal)
{
    uint32_t va = win32_resolve_import(dll, name, ordinal, by_ordinal);
    recomp_func_t fn = va != 0 ? guest_resolve(va) : NULL;
    if (fn == NULL)
        return va;
    char full[120];
    const char *named = by_ordinal ? win32_inventory_name(dll, ordinal) : NULL;
    if (named != NULL) /* golden traces name ordinal imports too */
        snprintf(full, sizeof full, "%s!%s", dll, named);
    else if (by_ordinal)
        snprintf(full, sizeof full, "%s!#%u", dll, ordinal);
    else
        snprintf(full, sizeof full, "%s!%s", dll, name);
    int k;
    uint32_t traced = trace_wrap(fn, full, &k);
    if (k >= 0 && !by_ordinal) {
        if (strcmp(name, "GetProcAddress") == 0)
            g_slot_getprocaddress = k;
        else if (strcmp(name, "GetModuleHandleA") == 0)
            g_slot_getmodulehandle = k;
        else if (strcmp(name, "LoadLibraryA") == 0)
            g_slot_loadlibrary = k;
        else if (strcmp(name, "RaiseException") == 0)
            g_slot_raiseexception = k;
    }
    return traced;
}

/* The redirected save root (make_save_dir). */
static char g_save_dir[1024];

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
    d3dx_game_cache_stamp(g_save_dir);
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

/* Executable section bounds from the mapped PE headers, for
 * RECOMP_ICALL_IS_CODE. */
static void set_code_range(uint32_t image_base)
{
    uint32_t nt = image_base + MEM32(image_base + 0x3Cu);
    uint32_t nsect = MEM16(nt + 6u);
    uint32_t sect = nt + 24u + MEM16(nt + 20u);
    uint32_t lo = 0, hi = 0;
    for (uint32_t i = 0; i < nsect; i++) {
        uint32_t s = sect + 40u * i;
        if ((MEM32(s + 36u) & PE_SCN_MEM_EXECUTE) == 0)
            continue;
        uint32_t va = image_base + MEM32(s + 12u), end = va + MEM32(s + 8u);
        if (lo == 0 || va < lo)
            lo = va;
        if (end > hi)
            hi = end;
    }
    g_xbox_code_lo = lo;
    g_xbox_code_hi = hi;
    fprintf(stderr, "hp_boot: code 0x%08X-0x%08X\n", lo, hi);
}

/* atexit handler: runs when the game quits through ExitProcess or
 * TerminateProcess on itself (win32_mem.c, both call exit) or returns from
 * its entry point (main returns). The fatal-signal handler leaves with _exit,
 * so a crashed run writes no stamp. */
static void stamp_on_exit(void)
{
    d3dx_game_cache_stamp(g_save_dir);
}

static int make_save_dir(char *out, size_t size)
{
    const char *home = getenv("HOME");
    if (home == NULL)
        return -1;
    snprintf(out, size, "%s/Library/Application Support/HarryPotterRecomp", home);
    /* A fresh HOME has no Library folders yet. */
    if (w32_fs_mkdir_p(out) != 0) {
        fprintf(stderr, "hp_boot: mkdir %s: %s\n", out, strerror(errno));
        return -1;
    }
    return 0;
}

int hp_boot_run(const char *exe_path, const char *game_dir)
{
    if (guest_mem_init() != 0 || guest_heap_init() != 0)
        return 1;
    g_xbox_mem_offset = (ptrdiff_t)(uintptr_t)g_guest_base;

    const char *trace = getenv("HP_TRACE_IMPORTS");
    g_trace_limit = trace != NULL ? atol(trace) : 0;
    const char *warm = getenv("HPRECOMP_WARM_SHADERS");
    int warm_on = warm != NULL && strcmp(warm, "1") == 0;
    pe_image_t img;
    if (pe_load_image(exe_path, warm_on ? resolve_warm
                                : g_trace_limit > 0 ? resolve_traced
                                : ft_enabled() ? resolve_timed : win32_resolve_import,
                      &img) != 0)
        return 1;
    fprintf(stderr, "hp_boot: image 0x%08X-0x%08X entry 0x%08X unbound %u\n", img.image_base,
            img.image_end, img.entry_va, img.unbound_imports);
    set_code_range(img.image_base);

    char *save_dir = g_save_dir;
    if (make_save_dir(save_dir, sizeof g_save_dir) != 0)
        return 1;
    if (warm_on && warm_setup(save_dir) != 0)
        return 1;
    /* With no cache yet, the game creates one during this run, made by this
     * build's compiler: stamp it when the game quits, or the next start would
     * see a cache with no stamp and mark it stale. */
    if (d3dx_game_cache_check(save_dir) == 0)
        atexit(stamp_on_exit);
    w32_fs_set_game_dir(game_dir);
    w32_fs_set_save_dir(save_dir);
    w32_ui_set_window_flags(SDL_WINDOW_VULKAN);

    recomp_dispatch_init();
    diag_init();

    if (mprotect(guest_ptr(MAIN_STACK_LO), MAIN_STACK_HI - MAIN_STACK_LO,
                 PROT_READ | PROT_WRITE) != 0) {
        perror("hp_boot: map main stack");
        return 1;
    }
    g_esp = MAIN_STACK_HI - 16u;
    /* Windows starts a process with x87 precision 53-bit (0x027F). */
    g_fp_control_word = 0x027F;
    g_mxcsr = 0x1F80;
    w32_tib_attach(MAIN_STACK_HI, MAIN_STACK_LO);

    recomp_func_t entry = recomp_lookup(img.entry_va);
    if (entry == NULL) {
        fprintf(stderr, "hp_boot: entry 0x%08X is not recompiled\n", img.entry_va);
        return 3;
    }
    g_esp -= 4;
    MEM32(g_esp) = ENTRY_RET_SENTINEL;
    g_ebp = 0;
    g_seh_ebp = 0;
    fprintf(stderr, "hp_boot: calling entry 0x%08X\n", img.entry_va);
    entry();
    fprintf(stderr, "hp_boot: entry returned eax=0x%08X\n", g_eax);
    return (int)g_eax;
}
