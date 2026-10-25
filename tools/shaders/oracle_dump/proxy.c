/* Proxy d3dx9_41.dll for the shader oracle.
 *
 * Every export except D3DXCompileShader is forwarded by the .def file to the
 * real Microsoft DLL, renamed d3dx9ms.dll. D3DXCompileShader calls the real
 * function and writes each call to C:\shaderdump\<index>.{hlsl,json,bin,err}.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct ID3DXBufferVtbl {
    HRESULT (WINAPI *QueryInterface)(void *, REFIID, void **);
    ULONG (WINAPI *AddRef)(void *);
    ULONG (WINAPI *Release)(void *);
    void *(WINAPI *GetBufferPointer)(void *);
    DWORD (WINAPI *GetBufferSize)(void *);
} ID3DXBufferVtbl;
typedef struct ID3DXBuffer { const ID3DXBufferVtbl *lpVtbl; } ID3DXBuffer;
typedef struct D3DXMACRO { const char *Name; const char *Definition; } D3DXMACRO;

typedef HRESULT (WINAPI *compile_fn)(const char *, UINT, const D3DXMACRO *,
    void *, const char *, const char *, DWORD, ID3DXBuffer **, ID3DXBuffer **,
    void **);

static compile_fn real_compile;
static volatile LONG next_index;
static const char dump_dir[] = "C:\\shaderdump";

static void write_file(const char *name, const void *data, size_t size)
{
    FILE *f = fopen(name, "wb");
    if (!f)
        return;
    if (size)
        fwrite(data, 1, size, f);
    fclose(f);
}

/* JSON string body: escapes quote, backslash and control bytes. */
static void json_str(FILE *f, const char *s)
{
    if (!s) {
        fputs("null", f);
        return;
    }
    fputc('"', f);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\')
            fprintf(f, "\\%c", c);
        else if (c < 0x20)
            fprintf(f, "\\u%04x", c);
        else
            fputc(c, f);
    }
    fputc('"', f);
}

static void buffer_bytes(ID3DXBuffer *b, const void **data, DWORD *size)
{
    *data = NULL;
    *size = 0;
    if (b) {
        *data = b->lpVtbl->GetBufferPointer(b);
        *size = b->lpVtbl->GetBufferSize(b);
    }
}

HRESULT WINAPI hook_D3DXCompileShader(const char *src, UINT src_len,
    const D3DXMACRO *defines, void *include, const char *entry,
    const char *profile, DWORD flags, ID3DXBuffer **shader,
    ID3DXBuffer **errors, void **constants)
{
    ID3DXBuffer *own_shader = NULL, *own_errors = NULL;
    ID3DXBuffer **shader_out = shader ? shader : &own_shader;
    ID3DXBuffer **errors_out = errors ? errors : &own_errors;
    const void *code, *msg;
    DWORD code_size, msg_size, t0, t1;
    char path[MAX_PATH];
    LONG index;
    HRESULT hr;
    FILE *f;

    if (!real_compile) {
        HMODULE real = LoadLibraryA("d3dx9ms.dll");
        if (real)
            real_compile = (compile_fn)GetProcAddress(real, "D3DXCompileShader");
        if (!real_compile)
            return E_FAIL;
    }

    t0 = GetTickCount();
    hr = real_compile(src, src_len, defines, include, entry, profile, flags,
                      shader_out, errors_out, constants);
    t1 = GetTickCount();

    index = InterlockedIncrement(&next_index) - 1;
    CreateDirectoryA(dump_dir, NULL);

    if (src) {
        snprintf(path, sizeof(path), "%s\\%06ld.hlsl", dump_dir, index);
        write_file(path, src, src_len);
    }
    buffer_bytes(*shader_out, &code, &code_size);
    if (code) {
        snprintf(path, sizeof(path), "%s\\%06ld.bin", dump_dir, index);
        write_file(path, code, code_size);
    }
    buffer_bytes(*errors_out, &msg, &msg_size);
    if (msg) {
        snprintf(path, sizeof(path), "%s\\%06ld.err", dump_dir, index);
        write_file(path, msg, msg_size);
    }

    snprintf(path, sizeof(path), "%s\\%06ld.json", dump_dir, index);
    f = fopen(path, "w");
    if (f) {
        const D3DXMACRO *m;
        fprintf(f, "{\"index\": %ld, \"src_len\": %u, \"entry\": ", index, src_len);
        json_str(f, entry);
        fputs(", \"profile\": ", f);
        json_str(f, profile);
        fprintf(f, ", \"flags\": %lu, \"include\": %s, \"hr\": %ld, "
                   "\"code_size\": %lu, \"msg_size\": %lu, \"ms\": %lu, "
                   "\"thread\": %lu, \"want_shader\": %s, \"want_errors\": %s, "
                   "\"want_constants\": %s, \"defines\": [",
                flags, include ? "true" : "false", (long)hr, code_size, msg_size,
                t1 - t0, GetCurrentThreadId(), shader ? "true" : "false",
                errors ? "true" : "false", constants ? "true" : "false");
        for (m = defines; m && m->Name; m++) {
            fputs(m == defines ? "[" : ", [", f);
            json_str(f, m->Name);
            fputs(", ", f);
            json_str(f, m->Definition);
            fputc(']', f);
        }
        fputs("]}\n", f);
        fclose(f);
    }

    if (own_shader)
        own_shader->lpVtbl->Release(own_shader);
    if (own_errors)
        own_errors->lpVtbl->Release(own_errors);
    return hr;
}
