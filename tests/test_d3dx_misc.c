/* D3DX misc slice test (d3dx_misc.c) on a real DXVK Native device: every
 * thunk is called through a guest stack, with its pop and the callee-saved
 * registers checked. Texture: the 64x64 32-bit bottom-up TGA the game builds
 * at 0x9b0f55 and a 4x2 top-down one, read back level by level through the
 * guest IDirect3DTexture9::LockRect. Surface: a 5x3 A8R8G8B8 surface saved
 * to BMP, read through the guest ID3DXBuffer. Needs a GPU; run with
 * VK_DRIVER_FILES=<KosmicKrisp ICD> DXVK_WSI_DRIVER=SDL2
 * DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib (notes/dxvk_triangle.md).
 * Links d3dx_misc.c, d3dx_shader.c (ID3DXBuffer) and vkd3d-shader,
 * gen_d3d9_guest.c, d3d9_guest_manual.c, com_guest.c and the engine's
 * guest_mem.c, guest_heap.c, dlmalloc.c, host_funcs.c, recomp_glue.c and
 * guest_call.c, plus libdxvk_d3d9 and SDL2. */
#include <SDL2/SDL.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <windows.h>
#undef THIS_
#undef THIS
#define THIS_ INTERFACE *This,
#define THIS INTERFACE *This
#include <d3d9.h>
#undef D3DERR_INVALIDCALL

#include "recomp_types.h"
#include "guest_heap.h"
#include "guest_mem.h"
#include "host_funcs.h"
#include "com_guest.h"
#include "d3d9_guest.h"
#include "d3dx_guest.h"
#include "d3dx_misc.h"
#include "win32_thunk.h"

/* Generated dispatch; this test has no lifted code. */
recomp_func_t recomp_lookup(uint32_t va)
{
    (void)va;
    return NULL;
}

/* Hooks gen_d3d9_guest.c links against; this test creates the host device
 * itself and never calls Direct3DCreate9 or passes an HWND. */
void *d3d9g_hwnd(uint32_t guest_hwnd)
{
    (void)guest_hwnd;
    return NULL;
}
void *d3d9g_backend_create9(uint32_t sdk_version)
{
    (void)sdk_version;
    return NULL;
}
void d3d9g_backend_fit_window(void *host_window, uint32_t width, uint32_t height, int desktop)
{
    (void)host_window, (void)width, (void)height, (void)desktop;
}
void d3d9g_backend_screen_sizes(uint32_t *desk_w, uint32_t *desk_h, uint32_t *panel_w, uint32_t *panel_h,
                                uint32_t *refresh_hz)
{
    *desk_w = *panel_w = 1920u, *desk_h = *panel_h = 1200u, *refresh_hz = 60u;
}

static int g_fail;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fail = 1;                                                  \
        }                                                                \
    } while (0)

/* A crash or abort() prints the signal and exits 2, so macOS shows no crash
 * dialog. */
static void on_fatal(int sig)
{
    char msg[64];
    int n = snprintf(msg, sizeof msg, "FATAL signal %d\n", sig);
    if (write(2, msg, (size_t)n) < 0)
        _exit(3);
    _exit(2);
}

/* ---- Guest memory and calls ----------------------------------------------- */

#define STACK_LO 0x00F00000u
#define STACK_TOP 0x00FF0000u
#define TIB 0x00FFE000u
#define RET_ADDR 0xDEAD0001u

static uint32_t galloc(uint32_t n)
{
    uint32_t a = guest_malloc(n);
    CHECK(a != 0u);
    memset(guest_ptr(a), 0, n);
    return a;
}

/* Push args right to left and the return address, call fn, and check that it
 * popped 4 + 4 * nargs bytes and kept ebx/esi/edi/ebp. Returns g_eax. */
static uint32_t call_fn(const char *what, recomp_func_t fn, unsigned nargs, const uint32_t *args)
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
    if (g_esp != start + 4u + 4u * nargs) {
        fprintf(stderr, "FAIL: %s popped %d bytes, want %u\n", what, (int)(g_esp - start), 4u + 4u * nargs);
        g_fail = 1;
    }
    if (g_ebx != 0xB0B0B0B0u || g_esi != 0x51515151u || g_edi != 0xD1D1D1D1u || g_ebp != 0xBEBEBEBEu) {
        fprintf(stderr, "FAIL: %s changed a callee-saved register\n", what);
        g_fail = 1;
    }
    return g_eax;
}
#define CALL(fn, ...) \
    call_fn(#fn, fn, sizeof((uint32_t[]){__VA_ARGS__}) / sizeof(uint32_t), (uint32_t[]){__VA_ARGS__})

#define SLOT(I, M) ((unsigned)(offsetof(I##Vtbl, M) / sizeof(void *)))

/* Call method slot of guest object obj with n arguments, through its guest
 * vtable. */
static uint32_t call_m(const char *what, uint32_t obj, unsigned slot, unsigned n, ...)
{
    uint32_t args[16];
    args[0] = obj;
    va_list ap;
    va_start(ap, n);
    for (unsigned i = 0; i < n; i++)
        args[1 + i] = va_arg(ap, uint32_t);
    va_end(ap);
    recomp_func_t fn = guest_resolve(MEM32(MEM32(obj) + 4u * slot));
    if (fn == NULL) {
        fprintf(stderr, "FATAL: %s: no thunk in slot %u\n", what, slot);
        _exit(2);
    }
    return call_fn(what, fn, 1u + n, args);
}
#define M(obj, I, Meth, n, ...) call_m(#I "::" #Meth, obj, SLOT(I, Meth), n, ##__VA_ARGS__)

/* ID3DXBuffer slots (d3dx9core.h): IUnknown, GetBufferPointer, GetBufferSize. */
#define BUF_RELEASE 2u
#define BUF_POINTER 3u
#define BUF_SIZE 4u

/* ---- Device ----------------------------------------------------------------- */

static IDirect3D9 *g_d3d;
static IDirect3DDevice9 *g_dev;

static uint32_t create_device(SDL_Window *win)
{
    g_d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (g_d3d == NULL) {
        fprintf(stderr, "FATAL: Direct3DCreate9 returned NULL\n");
        _exit(2);
    }
    D3DPRESENT_PARAMETERS pp = {0};
    pp.BackBufferWidth = 64;
    pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = (HWND)win;
    pp.Windowed = TRUE;
    HRESULT hr = g_d3d->lpVtbl->CreateDevice(g_d3d, 0, D3DDEVTYPE_HAL, (HWND)win,
                                             D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &g_dev);
    printf("CreateDevice: 0x%08X\n", (unsigned)hr);
    if (FAILED(hr) || g_dev == NULL) {
        fprintf(stderr, "FATAL: CreateDevice failed\n");
        _exit(2);
    }
    return d3d9g_wrap(g_dev, &d3d9g_iface_IDirect3DDevice9);
}

/* ---- D3DXCreateTextureFromFileInMemory --------------------------------------- */

static uint32_t g_rng = 12345u;
static uint8_t rnd(void)
{
    g_rng = g_rng * 1103515245u + 12345u;
    return (uint8_t)(g_rng >> 16);
}

/* An uncompressed 32-bit TGA in guest memory; the header as 0x9b0df2 builds
 * it. pixels holds the image top row first; the file stores it bottom row
 * first unless top_down. */
static uint32_t make_tga(uint32_t w, uint32_t h, int top_down, const uint8_t *pixels, uint32_t *size)
{
    *size = 18u + w * h * 4u;
    uint32_t a = galloc(*size);
    uint8_t *f = guest_ptr(a);
    f[2] = 2;
    f[12] = (uint8_t)w, f[13] = (uint8_t)(w >> 8);
    f[14] = (uint8_t)h, f[15] = (uint8_t)(h >> 8);
    f[16] = 32;
    f[17] = top_down ? 0x20 : 0;
    for (uint32_t y = 0; y < h; y++)
        memcpy(f + 18 + 4u * w * (top_down ? y : h - 1u - y), pixels + 4u * w * y, 4u * w);
    return a;
}

/* Expected level l + 1 from level l: the mean of each 2x2 block, rounded. */
static void expect_next(const uint8_t *s, uint32_t sw, uint32_t sh, uint8_t *d, uint32_t dw, uint32_t dh)
{
    for (uint32_t y = 0; y < dh; y++)
        for (uint32_t x = 0; x < dw; x++)
            for (uint32_t c = 0; c < 4; c++) {
                uint32_t xs[2] = {2 * x, sw > 1 ? 2 * x + 1 : 2 * x}, ys[2] = {2 * y, sh > 1 ? 2 * y + 1 : 2 * y};
                uint32_t sum = 0;
                for (int j = 0; j < 2; j++)
                    for (int i = 0; i < 2; i++)
                        sum += s[4 * (ys[j] * sw + xs[i]) + c];
                d[4 * (y * dw + x) + c] = (uint8_t)((sum + 2) / 4);
            }
}

static void test_texture(uint32_t gdev, uint32_t w, uint32_t h, int top_down)
{
    uint8_t *px = malloc(4u * w * h), *next = malloc(4u * w * h);
    for (uint32_t i = 0; i < 4u * w * h; i++)
        px[i] = rnd();
    uint32_t size;
    uint32_t file = make_tga(w, h, top_down, px, &size);
    uint32_t out = galloc(4);
    uint32_t hr = CALL(thunk_D3DXCreateTextureFromFileInMemory, gdev, file, size, out);
    uint32_t tex = MEM32(out);
    printf("CreateTextureFromFileInMemory %ux%u %s: 0x%08X, texture 0x%08X\n", w, h,
           top_down ? "top-down" : "bottom-up", hr, tex);
    CHECK(hr == D3D_OK && tex != 0u);
    if (hr != D3D_OK || tex == 0u)
        return;
    CHECK(com_guest_iface(tex) == &d3d9g_iface_IDirect3DTexture9);
    uint32_t levels = M(tex, IDirect3DTexture9, GetLevelCount, 0);
    uint32_t want_levels = 1;
    while ((w >> want_levels) != 0 || (h >> want_levels) != 0)
        want_levels++;
    CHECK(levels == want_levels);

    uint32_t desc = galloc(32), lr = galloc(8);
    uint32_t lw = w, lh = h, good = 0;
    for (uint32_t l = 0; l < levels; l++) {
        CHECK(M(tex, IDirect3DTexture9, GetLevelDesc, 2, l, desc) == D3D_OK);
        CHECK(MEM32(desc) == D3DFMT_A8R8G8B8 && MEM32(desc + 12) == D3DPOOL_MANAGED);
        CHECK(MEM32(desc + 24) == lw && MEM32(desc + 28) == lh);
        CHECK(M(tex, IDirect3DTexture9, LockRect, 4, l, lr, 0u, (uint32_t)D3DLOCK_READONLY) == D3D_OK);
        uint32_t pitch = MEM32(lr), bits = MEM32(lr + 4);
        unsigned bad = 0;
        for (uint32_t y = 0; y < lh; y++)
            if (memcmp(guest_ptr(bits + y * pitch), px + 4u * lw * y, 4u * lw) != 0)
                bad++;
        CHECK(bad == 0);
        good += bad == 0;
        CHECK(M(tex, IDirect3DTexture9, UnlockRect, 1, l) == D3D_OK);
        uint32_t nw = lw > 1 ? lw / 2 : 1, nh = lh > 1 ? lh / 2 : 1;
        expect_next(px, lw, lh, next, nw, nh);
        memcpy(px, next, 4u * nw * nh);
        lw = nw, lh = nh;
    }
    printf("%s texture %ux%u: %u/%u levels match byte for byte\n", good == levels ? "PASS" : "FAIL", w, h, good,
           levels);
    CHECK(M(tex, IDirect3DTexture9, Release, 0) == 0u);
    guest_free(file);
    free(px);
    free(next);
}

static void test_texture_reject(uint32_t gdev)
{
    static const uint8_t dds[128] = {'D', 'D', 'S', ' ', 124};
    uint32_t file = galloc(sizeof dds), out = galloc(4);
    memcpy(guest_ptr(file), dds, sizeof dds);
    MEM32(out) = 0x5A5A5A5Au;
    uint32_t hr = CALL(thunk_D3DXCreateTextureFromFileInMemory, gdev, file, (uint32_t)sizeof dds, out);
    printf("%s reject DDS: 0x%08X (want D3DXERR_INVALIDDATA), out untouched\n",
           hr == D3DXERR_INVALIDDATA && MEM32(out) == 0x5A5A5A5Au ? "PASS" : "FAIL", hr);
    CHECK(hr == D3DXERR_INVALIDDATA && MEM32(out) == 0x5A5A5A5Au);
}

/* ---- D3DXSaveSurfaceToFileInMemory ------------------------------------------- */

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static void test_save_surface(void)
{
    enum { SW = 5, SH = 3 };
    IDirect3DSurface9 *s = NULL;
    CHECK(g_dev->lpVtbl->CreateOffscreenPlainSurface(g_dev, SW, SH, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &s,
                                                      NULL) == D3D_OK);
    if (s == NULL)
        return;
    uint8_t px[SH][SW * 4];
    for (int y = 0; y < SH; y++)
        for (int i = 0; i < SW * 4; i++)
            px[y][i] = rnd();
    D3DLOCKED_RECT lr;
    CHECK(s->lpVtbl->LockRect(s, &lr, NULL, 0) == D3D_OK);
    for (int y = 0; y < SH; y++)
        memcpy((uint8_t *)lr.pBits + y * lr.Pitch, px[y], SW * 4);
    CHECK(s->lpVtbl->UnlockRect(s) == D3D_OK);
    uint32_t gs = d3d9g_wrap(s, &d3d9g_iface_IDirect3DSurface9);

    uint32_t out = galloc(4);
    uint32_t hr = CALL(thunk_D3DXSaveSurfaceToFileInMemory, out, 0u /* D3DXIFF_BMP */, gs, 0u, 0u);
    uint32_t buf = MEM32(out);
    CHECK(hr == D3D_OK && buf != 0u);
    if (hr == D3D_OK && buf != 0u) {
        uint32_t size = call_m("ID3DXBuffer::GetBufferSize", buf, BUF_SIZE, 0);
        const uint8_t *f = guest_ptr(call_m("ID3DXBuffer::GetBufferPointer", buf, BUF_POINTER, 0));
        uint32_t want = 54 + SW * SH * 4;
        int head = size == want && f[0] == 'B' && f[1] == 'M' && rd32(f + 2) == want && rd32(f + 10) == 54 &&
                   rd32(f + 14) == 40 && rd32(f + 18) == SW && rd32(f + 22) == SH && (rd32(f + 26) & 0xFFFF) == 1 &&
                   rd32(f + 26) >> 16 == 32 && rd32(f + 30) == 0;
        int rows = 0;
        for (int y = 0; y < SH; y++)
            rows += size == want && memcmp(f + 54 + y * SW * 4, px[SH - 1 - y], SW * 4) == 0;
        printf("%s save surface %dx%d to BMP: size %u (want %u), header %s, %d/%d rows bottom-up\n",
               head && rows == SH ? "PASS" : "FAIL", SW, SH, size, want, head ? "ok" : "bad", rows, SH);
        CHECK(head && rows == SH);
        CHECK(call_m("ID3DXBuffer::Release", buf, BUF_RELEASE, 0) == 0u);
    }

    /* Anything but a whole surface to BMP is refused. */
    MEM32(out) = 0;
    hr = CALL(thunk_D3DXSaveSurfaceToFileInMemory, out, 1u /* D3DXIFF_JPG */, gs, 0u, 0u);
    printf("%s save surface to JPG refused: 0x%08X\n", hr == D3DERR_INVALIDCALL && MEM32(out) == 0 ? "PASS" : "FAIL",
           hr);
    CHECK(hr == D3DERR_INVALIDCALL && MEM32(out) == 0);
    CHECK(M(gs, IDirect3DSurface9, Release, 0) == 0u);
}

/* ---- D3DXMatrixMultiply ------------------------------------------------------- */

static void test_matrix(void)
{
    float a[16], b[16], want[16];
    for (int i = 0; i < 16; i++)
        a[i] = (float)(i - 5), b[i] = (float)((i * 7) % 11 - 3);
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            float s = 0;
            for (int k = 0; k < 4; k++)
                s += a[4 * i + k] * b[4 * k + j];
            want[4 * i + j] = s;
        }
    uint32_t ga = galloc(64), gb = galloc(64), gout = galloc(64);
    memcpy(guest_ptr(ga), a, 64);
    memcpy(guest_ptr(gb), b, 64);
    uint32_t r = CALL(thunk_D3DXMatrixMultiply, gout, ga, gb);
    int ok = r == gout && memcmp(guest_ptr(gout), want, 64) == 0;
    /* pOut == pM1, as the game's in-place callers do. */
    r = CALL(thunk_D3DXMatrixMultiply, ga, ga, gb);
    ok = ok && r == ga && memcmp(guest_ptr(ga), want, 64) == 0;
    memcpy(guest_ptr(ga), a, 64);
    r = CALL(thunk_D3DXMatrixMultiply, gb, ga, gb);
    ok = ok && r == gb && memcmp(guest_ptr(gb), want, 64) == 0;
    printf("%s matrix multiply: out, out == M1, out == M2\n", ok ? "PASS" : "FAIL");
    CHECK(ok);
}

/* ---- Unreached imports --------------------------------------------------------- */

static void test_unreached(void)
{
    static const struct {
        const char *name;
        recomp_func_t fn;
        unsigned nargs;
    } t[] = {
        {"D3DXCreateTextureFromFileInMemoryEx", thunk_D3DXCreateTextureFromFileInMemoryEx, 15},
        {"D3DXCreateCubeTextureFromFileInMemory", thunk_D3DXCreateCubeTextureFromFileInMemory, 4},
        {"D3DXCreateEffectFromFileA", thunk_D3DXCreateEffectFromFileA, 8},
        {"D3DXCreateEffect", thunk_D3DXCreateEffect, 9},
        {"D3DXCreateEffectCompiler", thunk_D3DXCreateEffectCompiler, 7},
        {"D3DXCreateFontA", thunk_D3DXCreateFontA, 12},
        {"D3DXCreateRenderToSurface", thunk_D3DXCreateRenderToSurface, 7},
        {"D3DXCreateBuffer", thunk_D3DXCreateBuffer, 2},
    };
    uint32_t zeros[15] = {0};
    int ok = 0;
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++)
        ok += call_fn(t[i].name, t[i].fn, t[i].nargs, zeros) == D3DERR_INVALIDCALL;
    printf("%s unreached imports: %d/8 return D3DERR_INVALIDCALL\n", ok == 8 ? "PASS" : "FAIL", ok);
    CHECK(ok == 8);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    signal(SIGSEGV, on_fatal);
    signal(SIGBUS, on_fatal);
    signal(SIGABRT, on_fatal);
    if (guest_mem_init() != 0 || guest_heap_init() != 0) {
        fprintf(stderr, "guest memory init failed\n");
        return 2;
    }
    g_xbox_mem_offset = (ptrdiff_t)(uintptr_t)g_guest_base; /* as startup.c does */
    if (mprotect(guest_ptr(STACK_LO), 0x00100000u, PROT_READ | PROT_WRITE) != 0) {
        perror("mprotect");
        return 2;
    }
    g_fs_base = TIB;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "FATAL: SDL_Init: %s\n", SDL_GetError());
        return 2;
    }
    SDL_Window *win = SDL_CreateWindow("d3dx misc", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 64, 64,
                                       SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    if (win == NULL) {
        fprintf(stderr, "FATAL: SDL_CreateWindow: %s\n", SDL_GetError());
        return 2;
    }
    uint32_t gdev = create_device(win);

    test_texture(gdev, 64, 64, 0);
    test_texture(gdev, 4, 2, 1);
    test_texture_reject(gdev);
    test_save_surface();
    test_matrix();
    test_unreached();

    CHECK(M(gdev, IDirect3DDevice9, Release, 0) == 0u);
    CHECK(g_d3d->lpVtbl->Release(g_d3d) == 0u);
    SDL_DestroyWindow(win);
    SDL_Quit();

    if (g_fail) {
        printf("test_d3dx_misc: FAILED\n");
        return 1;
    }
    printf("test_d3dx_misc: PASS\n");
    return 0;
}
