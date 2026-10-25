/* d3dx9_41 imports other than the shader pair: the three the game reaches
 * (texture from an in-memory TGA, surface to an in-memory BMP, matrix
 * multiply) on the host Direct3D 9 objects, and an UNSUPPORTED stub for each
 * of the eight it never reaches. Contract: runtime/include/d3dx_misc.h;
 * evidence: notes/d3dx_liveness.md. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* DXVK Native defines THIS_ and THIS empty in C; restore the MinGW
 * definitions, as gen_d3d9_guest.c does (notes/dxvk_triangle.md). */
#include <windows.h>
#undef THIS_
#undef THIS
#define THIS_ INTERFACE *This,
#define THIS INTERFACE *This
#include <d3d9.h>
/* d3dx_guest.h defines the same value without the MAKE_D3DHRESULT macro. */
#undef D3DERR_INVALIDCALL

#include "recomp_types.h"
#include "diag.h"
#include "guest_mem.h"
#include "com_guest.h"
#include "d3d9_guest.h"
#include "d3dx_guest.h"
#include "d3dx_misc.h"
#include "win32_thunk.h"

#define D3DXIFF_BMP 0u

#define TGA_HEADER_SIZE 18u
#define TGA_TRUECOLOR 2u
#define TGA_TOP_LEFT 0x20u  /* image descriptor: first row is the top row */
#define TGA_RIGHT_LEFT 0x10u /* image descriptor: rows run right to left */

#define BMP_HEADER_SIZE 54u /* BITMAPFILEHEADER + BITMAPINFOHEADER */

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }

static void wr16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, uint32_t v)
{
    wr16(p, v);
    wr16(p + 2, v >> 16);
}

static int is_pow2(uint32_t v) { return v != 0 && (v & (v - 1u)) == 0; }

/* ---- D3DXCreateTextureFromFileInMemory ----------------------------------- */

/* Box-filter one 32-bit level into the next (D3DX_DEFAULT MipFilter is
 * D3DX_FILTER_BOX); a dimension of 1 stays 1. */
static void box_down(const uint8_t *src, uint32_t sw, uint32_t sh, uint8_t *dst, uint32_t dw, uint32_t dh)
{
    for (uint32_t y = 0; y < dh; y++)
        for (uint32_t x = 0; x < dw; x++) {
            uint32_t x0 = 2u * x, y0 = 2u * y;
            uint32_t x1 = x0 + 1u < sw ? x0 + 1u : x0, y1 = y0 + 1u < sh ? y0 + 1u : y0;
            for (uint32_t c = 0; c < 4u; c++) {
                uint32_t s = src[4u * (y0 * sw + x0) + c] + src[4u * (y0 * sw + x1) + c] +
                             src[4u * (y1 * sw + x0) + c] + src[4u * (y1 * sw + x1) + c];
                dst[4u * (y * dw + x) + c] = (uint8_t)((s + 2u) >> 2);
            }
        }
}

/* Lock level of tex and copy w x h 32-bit texels in, row by row. */
static HRESULT fill_level(IDirect3DTexture9 *tex, UINT level, const uint8_t *px, uint32_t w, uint32_t h)
{
    D3DLOCKED_RECT lr;
    HRESULT hr = tex->lpVtbl->LockRect(tex, level, &lr, NULL, 0);
    if (FAILED(hr))
        return hr;
    for (uint32_t y = 0; y < h; y++)
        memcpy((uint8_t *)lr.pBits + (size_t)y * (size_t)lr.Pitch, px + (size_t)y * w * 4u, (size_t)w * 4u);
    return tex->lpVtbl->UnlockRect(tex, level);
}

/* The A8R8G8B8 texture with a full mip chain in D3DPOOL_MANAGED that D3DX
 * builds from a 32-bit uncompressed TGA (the D3DX_DEFAULT arguments of
 * D3DXCreateTextureFromFileInMemoryEx). */
static HRESULT texture_from_tga(IDirect3DDevice9 *dev, const uint8_t *f, uint32_t size, IDirect3DTexture9 **out,
                                uint32_t ret)
{
    const char *fn = "d3dx9_41.dll!D3DXCreateTextureFromFileInMemory";
    if (size < TGA_HEADER_SIZE) {
        diag_unsupported(fn, ret, "file of %u bytes is not a TGA", size);
        return D3DXERR_INVALIDDATA;
    }
    uint32_t idlen = f[0], cmap = f[1], type = f[2], w = rd16(f + 12), h = rd16(f + 14), bpp = f[16], desc = f[17];
    if (cmap != 0 || type != TGA_TRUECOLOR || bpp != 32u || (desc & TGA_RIGHT_LEFT) != 0) {
        diag_unsupported(fn, ret, "image is not an uncompressed 32-bit TGA (bytes %02X %02X %02X ... bpp %u desc 0x%02X)",
                         f[0], f[1], f[2], bpp, desc);
        return D3DXERR_INVALIDDATA;
    }
    if (!is_pow2(w) || !is_pow2(h)) {
        diag_unsupported(fn, ret, "TGA is %ux%u; resizing to a power of two is not implemented", w, h);
        return D3DXERR_INVALIDDATA;
    }
    uint64_t need = (uint64_t)TGA_HEADER_SIZE + idlen + (uint64_t)w * h * 4u;
    if (need > size) {
        diag_unsupported(fn, ret, "%ux%u TGA needs %llu bytes, has %u", w, h, (unsigned long long)need, size);
        return D3DXERR_INVALIDDATA;
    }

    uint32_t levels = 1;
    while ((w >> levels) != 0 || (h >> levels) != 0)
        levels++;
    /* Level 0 top row first, then every smaller level after it. */
    size_t total = 0;
    for (uint32_t l = 0; l < levels; l++) {
        uint32_t lw = w >> l ? w >> l : 1u, lh = h >> l ? h >> l : 1u;
        total += (size_t)lw * lh * 4u;
    }
    uint8_t *px = malloc(total);
    if (px == NULL)
        return E_OUTOFMEMORY;
    const uint8_t *src = f + TGA_HEADER_SIZE + idlen;
    for (uint32_t y = 0; y < h; y++) {
        uint32_t sy = (desc & TGA_TOP_LEFT) != 0 ? y : h - 1u - y;
        memcpy(px + (size_t)y * w * 4u, src + (size_t)sy * w * 4u, (size_t)w * 4u);
    }
    for (uint32_t l = 1, off = 0; l < levels; l++) {
        uint32_t sw = w >> (l - 1) ? w >> (l - 1) : 1u, sh = h >> (l - 1) ? h >> (l - 1) : 1u;
        uint32_t dw = w >> l ? w >> l : 1u, dh = h >> l ? h >> l : 1u;
        box_down(px + off, sw, sh, px + off + (size_t)sw * sh * 4u, dw, dh);
        off += sw * sh * 4u;
    }

    IDirect3DTexture9 *tex = NULL;
    HRESULT hr = dev->lpVtbl->CreateTexture(dev, w, h, levels, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, NULL);
    size_t off = 0;
    for (uint32_t l = 0; SUCCEEDED(hr) && l < levels; l++) {
        uint32_t lw = w >> l ? w >> l : 1u, lh = h >> l ? h >> l : 1u;
        hr = fill_level(tex, l, px + off, lw, lh);
        off += (size_t)lw * lh * 4u;
    }
    free(px);
    if (FAILED(hr)) {
        if (tex != NULL)
            tex->lpVtbl->Release(tex);
        return hr;
    }
    *out = tex;
    return D3D_OK;
}

void thunk_D3DXCreateTextureFromFileInMemory(void)
{
    uint32_t a_dev = W32_ARG(0), a_data = W32_ARG(1), size = W32_ARG(2), a_out = W32_ARG(3);
    if (a_dev == 0 || a_data == 0 || size == 0 || a_out == 0) {
        W32_RETURN(4, D3DERR_INVALIDCALL);
        return;
    }
    IDirect3DDevice9 *dev = com_guest_self(a_dev);
    IDirect3DTexture9 *tex = NULL;
    HRESULT hr = texture_from_tga(dev, guest_ptr(a_data), size, &tex, MEM32(g_esp));
    if (SUCCEEDED(hr))
        MEM32(a_out) = d3d9g_wrap(tex, &d3d9g_iface_IDirect3DTexture9);
    W32_RETURN(4, (uint32_t)hr);
}

/* ---- D3DXSaveSurfaceToFileInMemory --------------------------------------- */

/* A bottom-up 32-bit BI_RGB BMP of an A8R8G8B8 surface, the layout the
 * game's reader expects (0x9c0033: file size at +2, pixels at +54, 4 bytes
 * each). */
static HRESULT bmp_from_surface(IDirect3DSurface9 *s, uint32_t *buf, uint32_t ret)
{
    const char *fn = "d3dx9_41.dll!D3DXSaveSurfaceToFileInMemory";
    D3DSURFACE_DESC d;
    HRESULT hr = s->lpVtbl->GetDesc(s, &d);
    if (FAILED(hr))
        return hr;
    if (d.Format != D3DFMT_A8R8G8B8) {
        diag_unsupported(fn, ret, "surface format %u is not A8R8G8B8", (unsigned)d.Format);
        return D3DERR_INVALIDCALL;
    }
    uint64_t bytes = (uint64_t)d.Width * d.Height * 4u;
    if (bytes > 0x7FFFFFFFu - BMP_HEADER_SIZE)
        return E_OUTOFMEMORY;
    uint32_t size = BMP_HEADER_SIZE + (uint32_t)bytes;
    uint8_t *f = calloc(1, size);
    if (f == NULL)
        return E_OUTOFMEMORY;
    f[0] = 'B';
    f[1] = 'M';
    wr32(f + 2, size);
    wr32(f + 10, BMP_HEADER_SIZE);
    wr32(f + 14, 40u); /* biSize */
    wr32(f + 18, d.Width);
    wr32(f + 22, d.Height); /* positive: bottom row first */
    wr16(f + 26, 1u);       /* biPlanes */
    wr16(f + 28, 32u);      /* biBitCount; biCompression BI_RGB = 0 */
    wr32(f + 34, (uint32_t)bytes);

    D3DLOCKED_RECT lr;
    hr = s->lpVtbl->LockRect(s, &lr, NULL, D3DLOCK_READONLY);
    if (SUCCEEDED(hr)) {
        for (uint32_t y = 0; y < d.Height; y++)
            memcpy(f + BMP_HEADER_SIZE + (size_t)y * d.Width * 4u,
                   (const uint8_t *)lr.pBits + (size_t)(d.Height - 1u - y) * (size_t)lr.Pitch, (size_t)d.Width * 4u);
        hr = s->lpVtbl->UnlockRect(s);
    }
    if (SUCCEEDED(hr)) {
        *buf = d3dx_buffer_new(f, size);
        if (*buf == 0)
            hr = E_OUTOFMEMORY;
    }
    free(f);
    return hr;
}

void thunk_D3DXSaveSurfaceToFileInMemory(void)
{
    uint32_t a_out = W32_ARG(0), fmt = W32_ARG(1), a_surf = W32_ARG(2), a_pal = W32_ARG(3), a_rect = W32_ARG(4);
    if (a_out == 0 || a_surf == 0) {
        W32_RETURN(5, D3DERR_INVALIDCALL);
        return;
    }
    if (fmt != D3DXIFF_BMP || a_pal != 0 || a_rect != 0) {
        diag_unsupported("d3dx9_41.dll!D3DXSaveSurfaceToFileInMemory", MEM32(g_esp),
                         "only a whole surface to BMP is implemented (format %u, palette 0x%08X, rect 0x%08X)", fmt,
                         a_pal, a_rect);
        W32_RETURN(5, D3DERR_INVALIDCALL);
        return;
    }
    uint32_t buf = 0;
    HRESULT hr = bmp_from_surface(com_guest_self(a_surf), &buf, MEM32(g_esp));
    if (SUCCEEDED(hr))
        MEM32(a_out) = buf;
    W32_RETURN(5, (uint32_t)hr);
}

/* ---- D3DXMatrixMultiply --------------------------------------------------- */

void thunk_D3DXMatrixMultiply(void)
{
    uint32_t a_out = W32_ARG(0), a_m1 = W32_ARG(1), a_m2 = W32_ARG(2);
    float m1[16], m2[16], r[16];
    memcpy(m1, guest_ptr(a_m1), sizeof m1);
    memcpy(m2, guest_ptr(a_m2), sizeof m2);
    for (unsigned i = 0; i < 4u; i++)
        for (unsigned j = 0; j < 4u; j++)
            r[4u * i + j] = m1[4u * i] * m2[j] + m1[4u * i + 1u] * m2[4u + j] + m1[4u * i + 2u] * m2[8u + j] +
                            m1[4u * i + 3u] * m2[12u + j];
    /* pOut may be pM1 or pM2: both are read before the store. */
    memcpy(guest_ptr(a_out), r, sizeof r);
    W32_RETURN(3, a_out);
}

/* ---- Imports the game never reaches (notes/d3dx_liveness.md) -------------- */

#define D3DX_UNREACHED(name, nargs)                                                                  \
    void thunk_##name(void)                                                                          \
    {                                                                                                \
        diag_unsupported("d3dx9_41.dll!" #name, MEM32(g_esp), "not implemented: the game never calls it"); \
        W32_RETURN(nargs, D3DERR_INVALIDCALL);                                                       \
    }

D3DX_UNREACHED(D3DXCreateTextureFromFileInMemoryEx, 15)
D3DX_UNREACHED(D3DXCreateCubeTextureFromFileInMemory, 4)
D3DX_UNREACHED(D3DXCreateEffectFromFileA, 8)
D3DX_UNREACHED(D3DXCreateEffect, 9)
D3DX_UNREACHED(D3DXCreateEffectCompiler, 7)
D3DX_UNREACHED(D3DXCreateFontA, 12)
D3DX_UNREACHED(D3DXCreateRenderToSurface, 7)
D3DX_UNREACHED(D3DXCreateBuffer, 2)
