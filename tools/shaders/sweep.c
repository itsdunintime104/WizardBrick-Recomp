/* Uber-shader sweep harness: compiles generated HLSL files through the
 * guest-facing thunk_D3DXCompileShader (runtime/src/d3dx_shader.c), the
 * same path the game takes. Job file lines (tab-separated):
 *   source-file  entry  profile  flags-hex  output-file
 * Output, one line per job: "<n> OK <bytes> <usec> <hit>" or
 * "<n> FAIL <usec>" followed by the error buffer lines prefixed "| ".
 * Driver: tools/shaders/sweep.py. The guest scaffolding is a copy of
 * tests/test_d3dx_shader.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

#include "recomp_types.h"
#include "guest_heap.h"
#include "guest_mem.h"
#include "host_funcs.h"
#include "com_guest.h"
#include "win32_thunk.h"
#include "d3dx_guest.h"

recomp_func_t recomp_lookup(uint32_t va)
{
    (void)va;
    return NULL;
}

#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            exit(3);                                                \
        }                                                           \
    } while (0)

/* ---- com_guest.h doubles ----------------------------------------------- */

#define MAX_OBJ 4096u
static void *g_obj_self[MAX_OBJ];
static uint32_t g_obj_guest[MAX_OBJ];
static int g_live_objects;
static struct {
    const com_interface_t *iface;
    uint32_t vtbl;
} g_vtbls[8];

uint32_t com_guest_new(const com_interface_t *iface, void *host_self)
{
    uint32_t vtbl = 0;
    for (unsigned i = 0; i < 8 && vtbl == 0; i++) {
        if (g_vtbls[i].iface == NULL) {
            g_vtbls[i].iface = iface;
            g_vtbls[i].vtbl = guest_malloc(4u * iface->n_methods);
            for (uint32_t k = 0; k < iface->n_methods; k++)
                MEM32(g_vtbls[i].vtbl + 4u * k) = host_func_va(iface->methods[k]);
        }
        if (g_vtbls[i].iface == iface)
            vtbl = g_vtbls[i].vtbl;
    }
    uint32_t idx = 1;
    while (idx < MAX_OBJ && g_obj_guest[idx] != 0)
        idx++;
    if (vtbl == 0 || idx == MAX_OBJ)
        abort();
    uint32_t obj = guest_malloc(8);
    MEM32(obj) = vtbl;
    MEM32(obj + 4u) = idx;
    g_obj_self[idx] = host_self;
    g_obj_guest[idx] = obj;
    g_live_objects++;
    return obj;
}

void *com_guest_self(uint32_t guest_this)
{
    uint32_t idx = MEM32(guest_this + 4u);
    if (idx == 0 || idx >= MAX_OBJ || g_obj_guest[idx] != guest_this) {
        fprintf(stderr, "UNSUPPORTED com: bad object 0x%08X\n", guest_this);
        abort();
    }
    return g_obj_self[idx];
}

void com_guest_free(uint32_t guest_this)
{
    uint32_t idx = MEM32(guest_this + 4u);
    CHECK(idx < MAX_OBJ && g_obj_guest[idx] == guest_this);
    g_obj_guest[idx] = 0;
    g_live_objects--;
    guest_free(guest_this);
}

com_guid_t com_guid_read(uint32_t a)
{
    com_guid_t g;
    g.d1 = MEM32(a);
    g.d2 = (uint16_t)MEM16(a + 4u);
    g.d3 = (uint16_t)MEM16(a + 6u);
    for (int i = 0; i < 8; i++)
        g.d4[i] = MEM8(a + 8u + (uint32_t)i);
    return g;
}

int com_guid_eq(const com_guid_t *a, const com_guid_t *b)
{
    return a->d1 == b->d1 && a->d2 == b->d2 && a->d3 == b->d3 && memcmp(a->d4, b->d4, 8) == 0;
}

/* ---- Guest stack and calls --------------------------------------------- */

#define STACK_LO 0x00F00000u
#define STACK_TOP 0x00FF0000u
#define RET_ADDR 0xDEAD0001u

/* Push args right to left and the return address, call fn, check that it
 * popped 4 + 4*nargs bytes and kept ebx/esi/edi/ebp. Returns g_eax. */
static uint32_t call_thunk(recomp_func_t fn, unsigned nargs, const uint32_t *args)
{
    g_esp = STACK_TOP;
    for (unsigned i = nargs; i-- > 0;) {
        g_esp -= 4;
        MEM32(g_esp) = args[i];
    }
    g_esp -= 4;
    MEM32(g_esp) = RET_ADDR;
    uint32_t start = g_esp;
    g_ebx = 0xB0B0B0B0u, g_esi = 0x51515151u, g_edi = 0xD1D1D1D1u, g_ebp = 0xBEBEBEBEu;
    g_eax = 0xCCCCCCCCu;
    fn();
    CHECK(g_esp == start + 4u + 4u * nargs);
    CHECK(g_ebx == 0xB0B0B0B0u && g_esi == 0x51515151u && g_edi == 0xD1D1D1D1u &&
          g_ebp == 0xBEBEBEBEu);
    return g_eax;
}

/* Call vtable slot of obj with n further arguments. */
static uint32_t call_method(uint32_t obj, uint32_t slot, unsigned n, const uint32_t *more)
{
    uint32_t args[8] = {obj};
    memcpy(args + 1, more, 4u * n);
    recomp_func_t fn = guest_resolve(MEM32(MEM32(obj) + 4u * slot));
    CHECK(fn != NULL);
    return fn != NULL ? call_thunk(fn, n + 1u, args) : 0u;
}
#define M(obj, slot, ...) \
    call_method((obj), (slot), sizeof((uint32_t[]){__VA_ARGS__}) / 4u, (uint32_t[]){__VA_ARGS__})
#define M0(obj, slot) call_method((obj), (slot), 0, NULL)

/* ID3DXBuffer / ID3DXConstantTable vtable slots. */
enum { RELEASE = 2, GET_BUFFER_POINTER = 3, GET_BUFFER_SIZE = 4, GET_DESC = 5,
       GET_CONSTANT_DESC = 6, GET_CONSTANT = 8, GET_CONSTANT_BY_NAME = 9,
       GET_CONSTANT_ELEMENT = 10, SET_FLOAT_ARRAY = 18 };

static uint32_t guest_bytes(const void *p, uint32_t n)
{
    uint32_t a = guest_malloc(n + 1u);
    memcpy(guest_ptr(a), p, n);
    MEM8(a + n) = 0;
    return a;
}
static uint32_t guest_str(const char *s) { return guest_bytes(s, (uint32_t)strlen(s)); }

static char *slurp(const char *path, uint32_t *len)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *p = malloc((size_t)n + 1u);
    if (p != NULL && fread(p, 1, (size_t)n, f) != (size_t)n) {
        free(p);
        p = NULL;
    }
    fclose(f);
    *len = (uint32_t)n;
    return p;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: sweep JOBFILE\n");
        return 2;
    }
    CHECK(guest_mem_init() == 0 && guest_heap_init() == 0);
    g_xbox_mem_offset = (ptrdiff_t)(uintptr_t)g_guest_base;
    CHECK(mprotect(guest_ptr(STACK_LO), 0x00100000u, PROT_READ | PROT_WRITE) == 0);
    FILE *jobs = fopen(argv[1], "r");
    CHECK(jobs != NULL);
    char line[4096], src[1024], entry[64], prof[16], dst[1024];
    unsigned flags, n = 0;
    while (fgets(line, sizeof line, jobs) != NULL) {
        if (sscanf(line, "%1023s %63s %15s %x %1023s", src, entry, prof, &flags, dst) != 5)
            continue;
        uint32_t len, outs = guest_malloc(12);
        char *text = slurp(src, &len);
        CHECK(text != NULL);
        uint32_t gsrc = guest_bytes(text, len), gentry = guest_str(entry), gprof = guest_str(prof);
        free(text);
        MEM32(outs) = MEM32(outs + 4u) = MEM32(outs + 8u) = 0;
        const uint32_t args[10] = {gsrc, len, 0, 0, gentry, gprof, flags, outs, outs + 4u, outs + 8u};
        uint32_t hits = d3dx_shader_cache_hits();
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        uint32_t hr = call_thunk(thunk_D3DXCompileShader, 10, args);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        long us = (t1.tv_sec - t0.tv_sec) * 1000000L + (t1.tv_nsec - t0.tv_nsec) / 1000L;
        uint32_t shader = MEM32(outs), errors = MEM32(outs + 4u), table = MEM32(outs + 8u);
        if (hr == 0 && shader != 0) {
            uint32_t size = M0(shader, GET_BUFFER_SIZE);
            FILE *o = fopen(dst, "wb");
            CHECK(o != NULL && fwrite(guest_ptr(M0(shader, GET_BUFFER_POINTER)), 1, size, o) == size);
            fclose(o);
            printf("%u OK %u %ld %u\n", n, size, us, d3dx_shader_cache_hits() - hits);
        } else {
            printf("%u FAIL %ld hr=0x%08X\n", n, us, hr);
            if (errors != 0) {
                char *e = strdup((const char *)guest_ptr(M0(errors, GET_BUFFER_POINTER)));
                for (char *l = strtok(e, "\n"); l != NULL; l = strtok(NULL, "\n"))
                    printf("| %s\n", l);
                free(e);
            }
        }
        fflush(stdout);
        for (uint32_t o = 0; o < 3; o++)
            if (MEM32(outs + 4u * o) != 0)
                M0(MEM32(outs + 4u * o), RELEASE);
        (void)table;
        guest_free(gsrc);
        guest_free(gentry);
        guest_free(gprof);
        guest_free(outs);
        n++;
    }
    fclose(jobs);
    return 0;
}
