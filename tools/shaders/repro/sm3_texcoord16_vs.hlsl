// vkd3d gap repro: vs_3_0 output with semantic index 16.
// D3D9 byte code has 4 bits for the usage index (D3DSP_DCL_USAGEINDEX_MASK
// 0x000F0000) and vs_3_0 has 12 output registers (o0-o11). vkd3d 2.1 (fed0c34)
// compiles this without an error and writes usage token 0x80100005
// (d3dbc.c:3342 shifts usage_idx with no mask or range check). Readers mask
// to 4 bits, so o2 reads as dcl_texcoord0, the same as o1. vkd3d's own
// disassembler prints "dcl_texcoord o1" and "dcl_texcoord o2". DXVK's
// dxbc-spirv then aborts at sm3_io_map.cpp:269 (!foundExisting) when it
// translates the shader. sm3_texcoord16_vs.bin is the vkd3d output.
// Expected: a compile error, as for any SM3 output index > 15.
struct VertexOutput {
    float4 position : POSITION;
    float4 a : TEXCOORD0;
    float4 b : TEXCOORD16;
};

VertexOutput main(float4 p : POSITION)
{
    VertexOutput o;
    o.position = p;
    o.a = p.xxxx;
    o.b = p.yyyy;
    return o;
}
