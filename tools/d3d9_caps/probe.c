/* Host probe: asks DXVK Native the Direct3D 9 questions LEGO Harry Potter
 * asks (notes/d3d9_caps.md), with the same arguments, then creates and uses
 * the resources its full-quality path needs. Build and run: build.sh. */
#define COBJMACROS
#include <windows.h>
#undef THIS_
#undef THIS
#define THIS_ INTERFACE *This,
#define THIS INTERFACE *This
#include <d3d9.h>
#include <SDL.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define FOURCC(a, b, c, d) ((DWORD)(a) | ((DWORD)(b) << 8) | ((DWORD)(c) << 16) | ((DWORD)(d) << 24))
#define F_INTZ FOURCC('I', 'N', 'T', 'Z')
#define F_RAWZ FOURCC('R', 'A', 'W', 'Z')
#define F_DF24 FOURCC('D', 'F', '2', '4')
#define F_NULL FOURCC('N', 'U', 'L', 'L')

static int fails;

static void on_signal(int sig)
{
    char msg[64];
    int n = snprintf(msg, sizeof msg, "FAIL signal %d\n", sig);
    write(2, msg, (size_t)n);
    _exit(2);
}

static void check(const char *what, HRESULT hr, int want_ok)
{
    int ok = SUCCEEDED(hr);
    printf("%-58s hr=0x%08x %s%s\n", what, (unsigned)hr, ok ? "OK" : "FAIL",
           ok == want_ok ? "" : "   <-- differs from what the game needs");
    if (ok != want_ok)
        fails++;
}

/* vs_3_0: pass position (v0) and a float4 texcoord (v1). */
static const DWORD vs30[] = {
    0xFFFE0300,
    0x0200001F, 0x80000000, 0x900F0000, /* dcl_position v0 */
    0x0200001F, 0x80000005, 0x900F0001, /* dcl_texcoord v1 */
    0x0200001F, 0x80000000, 0xE00F0000, /* dcl_position o0 */
    0x0200001F, 0x80000005, 0xE00F0001, /* dcl_texcoord o1 */
    0x02000001, 0xE00F0000, 0x90E40000, /* mov o0, v0 */
    0x02000001, 0xE00F0001, 0x90E40001, /* mov o1, v1 */
    0x0000FFFF,
};
/* ps_3_0: texld r0, v0, s0; mov oC0, r0.xxxx */
static const DWORD ps30[] = {
    0xFFFF0300,
    0x0200001F, 0x80000005, 0x900F0000, /* dcl_texcoord v0 */
    0x0200001F, 0x90000000, 0xA00F0800, /* dcl_2d s0 */
    0x03000042, 0x800F0000, 0x90E40000, 0xA0E40800,
    0x02000001, 0x800F0800, 0x80000000,
    0x0000FFFF,
};

struct vtx { float x, y, z, w, u, v, r, q; };

static void quad(IDirect3DDevice9 *dev, float depth, float refl, float refr)
{
    struct vtx q[4] = {
        { -1, 1, depth, 1, 0, 0, refl, 1 }, { 1, 1, depth, 1, 1, 0, refr, 1 },
        { -1, -1, depth, 1, 0, 1, refl, 1 }, { 1, -1, depth, 1, 1, 1, refr, 1 },
    };
    IDirect3DDevice9_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, q, sizeof q[0]);
}

/* Read back pixel (x, 32) of a 64x64 A8R8G8B8 render target. */
static unsigned readpx(IDirect3DDevice9 *dev, IDirect3DSurface9 *rt, IDirect3DSurface9 *sys, int x)
{
    D3DLOCKED_RECT lr;
    IDirect3DDevice9_GetRenderTargetData(dev, rt, sys);
    IDirect3DSurface9_LockRect(sys, &lr, NULL, D3DLOCK_READONLY);
    unsigned v = ((unsigned *)((char *)lr.pBits + 32 * lr.Pitch))[x];
    IDirect3DSurface9_UnlockRect(sys);
    return v;
}

/* Depth pass into a depth texture of format fmt with a NULL colour target
 * (the game's 0x6f8 choice), then sample the depth texture in ps_3_0. */
static void depth_test(IDirect3DDevice9 *dev, D3DFORMAT fmt, const char *name)
{
    IDirect3DTexture9 *dt = NULL, *ct = NULL;
    IDirect3DSurface9 *ds = NULL, *nullrt = NULL, *cs = NULL, *sys = NULL, *bb = NULL, *bbds = NULL;
    char what[96];
    HRESULT hr = IDirect3DDevice9_CreateTexture(dev, 64, 64, 1, D3DUSAGE_DEPTHSTENCIL, fmt, D3DPOOL_DEFAULT, &dt, NULL);
    snprintf(what, sizeof what, "CreateTexture %s DEPTHSTENCIL 64x64", name);
    check(what, hr, 1);
    if (FAILED(hr))
        return;
    IDirect3DTexture9_GetSurfaceLevel(dt, 0, &ds);
    hr = IDirect3DDevice9_CreateRenderTarget(dev, 64, 64, (D3DFORMAT)F_NULL, D3DMULTISAMPLE_NONE, 0, FALSE, &nullrt, NULL);
    check("CreateRenderTarget NULL 64x64", hr, 1);
    IDirect3DDevice9_CreateTexture(dev, 64, 64, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &ct, NULL);
    IDirect3DTexture9_GetSurfaceLevel(ct, 0, &cs);
    IDirect3DDevice9_CreateOffscreenPlainSurface(dev, 64, 64, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, NULL);
    IDirect3DDevice9_GetRenderTarget(dev, 0, &bb);
    IDirect3DDevice9_GetDepthStencilSurface(dev, &bbds);

    IDirect3DDevice9_BeginScene(dev);
    /* pass 1: depth only, z = 0.25 */
    IDirect3DDevice9_SetRenderTarget(dev, 0, nullrt ? nullrt : cs);
    IDirect3DDevice9_SetDepthStencilSurface(dev, ds);
    IDirect3DDevice9_Clear(dev, 0, NULL, D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ZENABLE, TRUE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ZWRITEENABLE, TRUE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    IDirect3DDevice9_SetTexture(dev, 0, NULL);
    quad(dev, 0.25f, 0, 0);
    /* pass 2: sample the depth texture; texcoord.z is the compare value
     * for a shadow (compare) sampler: 0.2 at the left edge, 0.3 at the right. */
    IDirect3DDevice9_SetRenderTarget(dev, 0, cs);
    IDirect3DDevice9_SetDepthStencilSurface(dev, NULL);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ZENABLE, FALSE);
    IDirect3DDevice9_Clear(dev, 0, NULL, D3DCLEAR_TARGET, 0xff00ff00, 1.0f, 0);
    IDirect3DDevice9_SetTexture(dev, 0, (IDirect3DBaseTexture9 *)dt);
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    quad(dev, 0.5f, 0.2f, 0.3f);
    IDirect3DDevice9_EndScene(dev);
    unsigned l = readpx(dev, cs, sys, 4), r = readpx(dev, cs, sys, 60);
    printf("  %s sampled: left (ref 0.21) 0x%08x  right (ref 0.29) 0x%08x  red %u / %u (raw depth 0.25 = 64; compare = 255 / 0)\n",
           name, l, r, (l >> 16) & 255, (r >> 16) & 255);
    IDirect3DDevice9_SetTexture(dev, 0, NULL);
    IDirect3DDevice9_SetRenderTarget(dev, 0, bb);
    IDirect3DDevice9_SetDepthStencilSurface(dev, bbds);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ZENABLE, TRUE);
    if (bbds) IDirect3DSurface9_Release(bbds);
    IDirect3DSurface9_Release(bb);
    IDirect3DSurface9_Release(sys);
    IDirect3DSurface9_Release(cs);
    IDirect3DTexture9_Release(ct);
    if (nullrt) IDirect3DSurface9_Release(nullrt);
    IDirect3DSurface9_Release(ds);
    IDirect3DTexture9_Release(dt);
}

int main(void)
{
    signal(SIGSEGV, on_signal); signal(SIGBUS, on_signal); signal(SIGABRT, on_signal); signal(SIGILL, on_signal);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { printf("FAIL SDL_Init %s\n", SDL_GetError()); return 2; }
    SDL_Window *win = SDL_CreateWindow("d3d9_caps", 0, 0, 640, 480, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    if (!win) { printf("FAIL window %s\n", SDL_GetError()); return 2; }
    IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) { printf("FAIL Direct3DCreate9\n"); return 2; }

    D3DADAPTER_IDENTIFIER9 id;
    IDirect3D9_GetAdapterIdentifier(d3d, 0, 0, &id);
    printf("adapter: %s  VendorId 0x%04x DeviceId 0x%04x\n", id.Description, (unsigned)id.VendorId, (unsigned)id.DeviceId);

    D3DCAPS9 c;
    check("GetDeviceCaps(0, HAL)", IDirect3D9_GetDeviceCaps(d3d, 0, D3DDEVTYPE_HAL, &c), 1);
    printf("caps VertexShaderVersion 0x%08x PixelShaderVersion 0x%08x\n", (unsigned)c.VertexShaderVersion, (unsigned)c.PixelShaderVersion);
    printf("caps DevCaps HWTRANSFORMANDLIGHT %d  PresentationIntervals 0x%x\n", !!(c.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT), (unsigned)c.PresentationIntervals);
    printf("caps TextureFilterCaps MINFANISOTROPIC %d MAGFANISOTROPIC %d  MaxAnisotropy %u\n",
           !!(c.TextureFilterCaps & 0x400), !!(c.TextureFilterCaps & 0x4000000), (unsigned)c.MaxAnisotropy);
    printf("caps DeclTypes FLOAT16_2 %d FLOAT16_4 %d  MaxSimultaneousTextures %u\n",
           !!(c.DeclTypes & D3DDTCAPS_FLOAT16_2), !!(c.DeclTypes & D3DDTCAPS_FLOAT16_4), (unsigned)c.MaxSimultaneousTextures);
    printf("caps TextureCaps POW2 %d NONPOW2CONDITIONAL %d  StencilCaps TWOSIDED %d\n",
           !!(c.TextureCaps & D3DPTEXTURECAPS_POW2), !!(c.TextureCaps & D3DPTEXTURECAPS_NONPOW2CONDITIONAL), !!(c.StencilCaps & D3DSTENCILCAPS_TWOSIDED));
    printf("caps NumSimultaneousRTs %u MaxStreams %u MaxVertexShaderConst %u MaxTextureWidth %u\n",
           (unsigned)c.NumSimultaneousRTs, (unsigned)c.MaxStreams, (unsigned)c.MaxVertexShaderConst, (unsigned)c.MaxTextureWidth);
    {
        DWORD vs = c.VertexShaderVersion & 0xffff, ps = c.PixelShaderVersion & 0xffff;
        int lvl = (vs >= 0x300 && ps >= 0x300) ? 6 : (vs >= 0x200 && ps >= 0x200) ? 2 : (vs >= 0x101 && ps >= 0x101) ? 1 : 0;
        printf("game shader level (0xa2c8a0 rule): %d (needs 6 for vs_3_0/ps_3_0)\n", lvl);
        if (lvl != 6) fails++;
    }

    D3DDISPLAYMODE dm;
    IDirect3D9_GetAdapterDisplayMode(d3d, 0, &dm);
    printf("display mode %ux%u @%u fmt %u; X8R8G8B8 mode count %u\n", dm.Width, dm.Height, dm.RefreshRate, dm.Format,
           IDirect3D9_GetAdapterModeCount(d3d, 0, D3DFMT_X8R8G8B8));
    check("CheckDeviceType(HAL, display fmt, A8R8G8B8, windowed)",
          IDirect3D9_CheckDeviceType(d3d, 0, D3DDEVTYPE_HAL, dm.Format, D3DFMT_A8R8G8B8, TRUE), 1);
    check("CheckDeviceType(HAL, display fmt, A8R8G8B8, fullscreen)",
          IDirect3D9_CheckDeviceType(d3d, 0, D3DDEVTYPE_HAL, dm.Format, D3DFMT_A8R8G8B8, FALSE), 1);
    for (int ms = 2; ms <= 8; ms += 2) {
        DWORD q1 = 0, q2 = 0;
        HRESULT a = IDirect3D9_CheckDeviceMultiSampleType(d3d, 0, D3DDEVTYPE_HAL, D3DFMT_A8R8G8B8, TRUE, (D3DMULTISAMPLE_TYPE)ms, &q1);
        HRESULT b = IDirect3D9_CheckDeviceMultiSampleType(d3d, 0, D3DDEVTYPE_HAL, D3DFMT_D24S8, TRUE, (D3DMULTISAMPLE_TYPE)ms, &q2);
        printf("CheckDeviceMultiSampleType %dx: A8R8G8B8 hr=0x%08x q=%u  D24S8 hr=0x%08x q=%u\n", ms, (unsigned)a, (unsigned)q1, (unsigned)b, (unsigned)q2);
    }
    /* 0x93dee0: AdapterFormat X8R8G8B8 (0x16), RType TEXTURE (3). */
    static const struct { DWORD usage, fmt; const char *n; } cf[] = {
        { D3DUSAGE_DEPTHSTENCIL, F_INTZ, "INTZ" }, { D3DUSAGE_DEPTHSTENCIL, F_RAWZ, "RAWZ" },
        { D3DUSAGE_DEPTHSTENCIL, D3DFMT_D24S8, "D24S8" }, { D3DUSAGE_DEPTHSTENCIL, F_DF24, "DF24" },
        { D3DUSAGE_RENDERTARGET, F_NULL, "NULL" }, { D3DUSAGE_RENDERTARGET, D3DFMT_R5G6B5, "R5G6B5" },
    };
    for (unsigned i = 0; i < sizeof cf / sizeof cf[0]; i++) {
        char what[96];
        snprintf(what, sizeof what, "CheckDeviceFormat(X8R8G8B8, %s, TEXTURE, %s)", cf[i].usage == D3DUSAGE_DEPTHSTENCIL ? "DEPTHSTENCIL" : "RENDERTARGET", cf[i].n);
        HRESULT hr = IDirect3D9_CheckDeviceFormat(d3d, 0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, cf[i].usage, D3DRTYPE_TEXTURE, (D3DFORMAT)cf[i].fmt);
        printf("%-58s hr=0x%08x %s\n", what, (unsigned)hr, SUCCEEDED(hr) ? "OK" : "no");
    }

    /* D3DCreateDevice 0xa37d90: windowed, A8R8G8B8, 1 buffer, DISCARD, D24S8 auto depth,
     * DISCARDDEPTHSTENCIL, IMMEDIATE; PUREDEVICE | HARDWARE_VERTEXPROCESSING. */
    D3DPRESENT_PARAMETERS pp;
    memset(&pp, 0, sizeof pp);
    pp.BackBufferWidth = 640; pp.BackBufferHeight = 480; pp.BackBufferFormat = D3DFMT_A8R8G8B8; pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = (HWND)win; pp.Windowed = TRUE;
    pp.EnableAutoDepthStencil = TRUE; pp.AutoDepthStencilFormat = D3DFMT_D24S8; pp.Flags = D3DPRESENTFLAG_DISCARD_DEPTHSTENCIL;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9 *dev = NULL;
    HRESULT hr = IDirect3D9_CreateDevice(d3d, 0, D3DDEVTYPE_HAL, (HWND)win, D3DCREATE_PUREDEVICE | D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev);
    check("CreateDevice(PUREDEVICE|HWVP, game present params)", hr, 1);
    if (FAILED(hr)) return 2;
    check("TestCooperativeLevel", IDirect3DDevice9_TestCooperativeLevel(dev), 1);

    IDirect3DVertexShader9 *vs = NULL; IDirect3DPixelShader9 *ps = NULL; IDirect3DVertexDeclaration9 *decl = NULL;
    static const D3DVERTEXELEMENT9 el[] = {
        { 0, 0, D3DDECLTYPE_FLOAT4, 0, D3DDECLUSAGE_POSITION, 0 }, { 0, 16, D3DDECLTYPE_FLOAT4, 0, D3DDECLUSAGE_TEXCOORD, 0 }, D3DDECL_END(),
    };
    check("CreateVertexShader vs_3_0", IDirect3DDevice9_CreateVertexShader(dev, vs30, &vs), 1);
    check("CreatePixelShader ps_3_0", IDirect3DDevice9_CreatePixelShader(dev, ps30, &ps), 1);
    check("CreateVertexDeclaration", IDirect3DDevice9_CreateVertexDeclaration(dev, el, &decl), 1);
    IDirect3DDevice9_SetVertexShader(dev, vs); IDirect3DDevice9_SetPixelShader(dev, ps);
    IDirect3DDevice9_SetVertexDeclaration(dev, decl);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);

    /* Occlusion query: the game creates type 9 at 0x9f1481. */
    IDirect3DQuery9 *q = NULL;
    hr = IDirect3DDevice9_CreateQuery(dev, D3DQUERYTYPE_OCCLUSION, &q);
    check("CreateQuery(OCCLUSION)", hr, 1);

    depth_test(dev, (D3DFORMAT)F_INTZ, "INTZ");
    depth_test(dev, D3DFMT_D24S8, "D24S8");

    if (q) {
        DWORD n = 0; int spins = 0;
        IDirect3DDevice9_BeginScene(dev);
        IDirect3DQuery9_Issue(q, D3DISSUE_BEGIN);
        IDirect3DDevice9_SetRenderState(dev, D3DRS_ZENABLE, FALSE);
        quad(dev, 0.5f, 0, 0);
        IDirect3DQuery9_Issue(q, D3DISSUE_END);
        IDirect3DDevice9_EndScene(dev);
        while ((hr = IDirect3DQuery9_GetData(q, &n, sizeof n, D3DGETDATA_FLUSH)) == S_FALSE && spins++ < 2000)
            usleep(500);
        printf("occlusion query: hr=0x%08x samples %u (640x480 = 307200)\n", (unsigned)hr, (unsigned)n);
        if (n != 640u * 480u) fails++;
        IDirect3DQuery9_Release(q);
    }
    check("Present", IDirect3DDevice9_Present(dev, NULL, NULL, NULL, NULL), 1);
    /* Lost-device path 0xa38930: EvictManagedResources, Reset(same params). */
    check("EvictManagedResources", IDirect3DDevice9_EvictManagedResources(dev), 1);
    check("Reset(game present params)", IDirect3DDevice9_Reset(dev, &pp), 1);
    check("TestCooperativeLevel after Reset", IDirect3DDevice9_TestCooperativeLevel(dev), 1);

    IDirect3DVertexDeclaration9_Release(decl); IDirect3DPixelShader9_Release(ps); IDirect3DVertexShader9_Release(vs);
    IDirect3DDevice9_Release(dev); IDirect3D9_Release(d3d);
    SDL_DestroyWindow(win); SDL_Quit();
    printf("%s d3d9_caps probe (%d differences)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
