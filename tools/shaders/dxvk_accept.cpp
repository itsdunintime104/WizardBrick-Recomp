/* DXVK acceptance of compiled Shader Model 1-3 byte code: creates one D3D9
 * device through DXVK Native (SDL2 WSI, one window) and calls
 * CreateVertexShader or CreatePixelShader, chosen by the version token, on
 * every file given (HPRECOMP_SHADER_DUMP <key>.bin or sweep run/bin files).
 * DXVK Native's d3d9.h has no C method form (THIS_ is empty), so this is
 * C++ with C style. Prints the pass and fail counts and the first 10 failures; exits 0 when all
 * pass, 1 when any fails, 2 on a setup error or a crash.
 *
 * Build:
 *   c++ -std=c++17 -nostdinc++ -isystem $(xcrun --show-sdk-path)/usr/include/c++/v1 -O1 -I build.nosync/deps/dxvk/include/dxvk -I /opt/homebrew/include -I /opt/homebrew/include/SDL2 \
 *      tools/shaders/dxvk_accept.cpp -L build.nosync/deps/dxvk/lib -ldxvk_d3d9 \
 *      -Wl,-rpath,build.nosync/deps/dxvk/lib -L/opt/homebrew/lib -lSDL2 -o dxvk_accept
 * Run:
 *   VK_DRIVER_FILES=build.nosync/deps/kosmickrisp/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json DXVK_WSI_DRIVER=SDL2 \
 *   DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib ./dxvk_accept FILE.bin... */
#include <SDL2/SDL.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <windows.h>
#include <d3d9.h>

static void on_fatal(int sig)
{
    char msg[32];
    int n = snprintf(msg, sizeof msg, "FATAL signal %d\n", sig);
    if (write(2, msg, (size_t)n) < 0)
        _exit(3);
    _exit(2);
}

static void *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    void *p = n > 0 ? malloc((size_t)n) : NULL;
    if (p != NULL && fread(p, 1, (size_t)n, f) != (size_t)n) {
        free(p);
        p = NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return p;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    int trace = getenv("DXVK_ACCEPT_TRACE") != NULL; /* print each path before use */
    signal(SIGSEGV, on_fatal);
    signal(SIGBUS, on_fatal);
    signal(SIGABRT, on_fatal);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "FATAL: SDL_Init: %s\n", SDL_GetError());
        return 2;
    }
    SDL_Window *win = SDL_CreateWindow("dxvk_accept", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 64, 64,
                                       SDL_WINDOW_VULKAN);
    IDirect3D9 *d3d = win != NULL ? Direct3DCreate9(D3D_SDK_VERSION) : NULL;
    if (d3d == NULL) {
        fprintf(stderr, "FATAL: window or Direct3DCreate9 failed: %s\n", SDL_GetError());
        return 2;
    }
    D3DPRESENT_PARAMETERS pp = {};
    pp.BackBufferWidth = 64;
    pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = (HWND)win;
    pp.Windowed = TRUE;
    IDirect3DDevice9 *dev = NULL;
    HRESULT hr = d3d->CreateDevice(0, D3DDEVTYPE_HAL, (HWND)win, D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                &pp, &dev);
    if (FAILED(hr)) {
        fprintf(stderr, "FATAL: CreateDevice 0x%08X\n", (unsigned)hr);
        return 2;
    }
    unsigned pass = 0, fail = 0, vs = 0, ps = 0;
    for (int i = 1; i < argc; i++) {
        size_t len = 0;
        DWORD *code = (DWORD *)slurp(argv[i], &len);
        unsigned type = code != NULL && len >= 4 ? (unsigned)(code[0] >> 16) : 0;
        hr = E_FAIL;
        if (trace)
            fprintf(stderr, "file %s\n", argv[i]);
        if (type == 0xFFFEu) {
            IDirect3DVertexShader9 *s = NULL;
            hr = dev->CreateVertexShader(code, &s);
            if (s != NULL)
                s->Release();
            vs++;
        } else if (type == 0xFFFFu) {
            IDirect3DPixelShader9 *s = NULL;
            hr = dev->CreatePixelShader(code, &s);
            if (s != NULL)
                s->Release();
            ps++;
        }
        if (SUCCEEDED(hr)) {
            pass++;
        } else if (++fail <= 10) {
            printf("FAIL %s version 0x%08X hr 0x%08X\n", argv[i], code != NULL && len >= 4 ? (unsigned)code[0] : 0u,
                   (unsigned)hr);
        }
        free(code);
    }
    printf("dxvk_accept: %u files, vs %u, ps %u, pass %u, fail %u\n", (unsigned)(argc - 1), vs, ps, pass, fail);
    if (trace)
        fprintf(stderr, "teardown\n");
    dev->Release();
    d3d->Release();
    SDL_DestroyWindow(win);
    SDL_Quit();
    return fail != 0;
}
