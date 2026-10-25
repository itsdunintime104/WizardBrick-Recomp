/* d3dx9_41 imports other than the shader pair (d3dx_guest.h). Implemented
 * in runtime/src/d3dx_misc.c; which of them the game reaches, and with what
 * data, is in notes/d3dx_liveness.md.
 *
 * All are stdcall import thunks (win32_thunk.h). They are not in any export
 * table here: tools/d3d9/d3dx_exports.json lists them for win32_exports_d3d. */
#ifndef D3DX_MISC_H
#define D3DX_MISC_H

/* ---- Live ---- */

/* HRESULT D3DXCreateTextureFromFileInMemory(LPDIRECT3DDEVICE9 pDevice,
 *     LPCVOID pSrcData, UINT SrcDataSize, LPDIRECT3DTEXTURE9 *ppTexture)
 * 4 arguments, pops 16 bytes. Reads an uncompressed 32-bit TGA, the only
 * file the game passes (0x9b0f55). */
void thunk_D3DXCreateTextureFromFileInMemory(void);

/* HRESULT D3DXSaveSurfaceToFileInMemory(LPD3DXBUFFER *ppDestBuf,
 *     D3DXIMAGE_FILEFORMAT DestFormat, LPDIRECT3DSURFACE9 pSrcSurface,
 *     const PALETTEENTRY *pSrcPalette, const RECT *pSrcRect)
 * 5 arguments, pops 20 bytes. Writes a 32-bit BMP of an A8R8G8B8 surface,
 * the only case the game uses (0x9bb64e). */
void thunk_D3DXSaveSurfaceToFileInMemory(void);

/* D3DXMATRIX *D3DXMatrixMultiply(D3DXMATRIX *pOut, const D3DXMATRIX *pM1,
 *     const D3DXMATRIX *pM2)
 * 3 arguments, pops 12 bytes; returns pOut. */
void thunk_D3DXMatrixMultiply(void);

/* ---- Not reached by the game: an UNSUPPORTED line, D3DERR_INVALIDCALL ---- */

void thunk_D3DXCreateTextureFromFileInMemoryEx(void); /* 15 arguments, 60 bytes */
void thunk_D3DXCreateCubeTextureFromFileInMemory(void); /* 4 arguments, 16 bytes */
void thunk_D3DXCreateEffectFromFileA(void); /* 8 arguments, 32 bytes */
void thunk_D3DXCreateEffect(void); /* 9 arguments, 36 bytes */
void thunk_D3DXCreateEffectCompiler(void); /* 7 arguments, 28 bytes */
void thunk_D3DXCreateFontA(void); /* 12 arguments, 48 bytes */
void thunk_D3DXCreateRenderToSurface(void); /* 7 arguments, 28 bytes */
void thunk_D3DXCreateBuffer(void); /* 2 arguments, 8 bytes */

#endif
