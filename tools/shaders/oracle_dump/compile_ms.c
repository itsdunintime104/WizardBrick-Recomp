/* Compile one HLSL file with Microsoft's d3dx9_41 (renamed d3dx9ms.dll).
 *
 * Usage: compile_ms.exe <src.hlsl> <entry> <profile> <flags> <out prefix>
 * Writes <out prefix>.bin, .err and .asm (D3DXDisassembleShader); prints the
 * HRESULT. Run it under Wine with d3dx9ms.dll in the same folder.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct ID3DXBufferVtbl {
    HRESULT (WINAPI *QueryInterface)(void *, REFIID, void **);
    ULONG (WINAPI *AddRef)(void *);
    ULONG (WINAPI *Release)(void *);
    void *(WINAPI *GetBufferPointer)(void *);
    DWORD (WINAPI *GetBufferSize)(void *);
} ID3DXBufferVtbl;
typedef struct ID3DXBuffer { const ID3DXBufferVtbl *lpVtbl; } ID3DXBuffer;

typedef HRESULT (WINAPI *compile_fn)(const char *, UINT, const void *, void *,
    const char *, const char *, DWORD, ID3DXBuffer **, ID3DXBuffer **, void **);
typedef HRESULT (WINAPI *disasm_fn)(const DWORD *, BOOL, const char *,
    ID3DXBuffer **);

static void save(const char *prefix, const char *ext, ID3DXBuffer *b, BOOL text)
{
    char path[MAX_PATH];
    DWORD size;
    FILE *f;

    if (!b)
        return;
    size = b->lpVtbl->GetBufferSize(b);
    /* Text buffers end with a NUL; do not write it. */
    if (text && size && ((char *)b->lpVtbl->GetBufferPointer(b))[size - 1] == 0)
        size--;
    snprintf(path, sizeof(path), "%s.%s", prefix, ext);
    f = fopen(path, "wb");
    if (!f)
        return;
    fwrite(b->lpVtbl->GetBufferPointer(b), 1, size, f);
    fclose(f);
}

int main(int argc, char **argv)
{
    ID3DXBuffer *code = NULL, *errors = NULL, *listing = NULL;
    compile_fn compile;
    disasm_fn disasm;
    HMODULE dll;
    HRESULT hr;
    char *src;
    long len;
    FILE *f;

    if (argc != 6) {
        fprintf(stderr, "usage: %s src entry profile flags outprefix\n", argv[0]);
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (!f)
        return 2;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    src = malloc(len);
    fread(src, 1, len, f);
    fclose(f);

    dll = LoadLibraryA("d3dx9ms.dll");
    if (!dll)
        return 3;
    compile = (compile_fn)GetProcAddress(dll, "D3DXCompileShader");
    disasm = (disasm_fn)GetProcAddress(dll, "D3DXDisassembleShader");
    if (!compile || !disasm)
        return 3;

    hr = compile(src, (UINT)len, NULL, NULL, argv[2], argv[3],
                 strtoul(argv[4], NULL, 0), &code, &errors, NULL);
    printf("hr=0x%08lx code=%lu\n", (unsigned long)hr,
           code ? code->lpVtbl->GetBufferSize(code) : 0);
    save(argv[5], "bin", code, FALSE);
    save(argv[5], "err", errors, TRUE);
    if (code && SUCCEEDED(disasm(code->lpVtbl->GetBufferPointer(code), FALSE,
                                 NULL, &listing)))
        save(argv[5], "asm", listing, TRUE);
    return FAILED(hr);
}
