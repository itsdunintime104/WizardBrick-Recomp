"""Write the proxy .def: forward every export of the real DLL to d3dx9ms.

Usage: python -I gen_def.py <real d3dx9_41.dll> <out.def>
D3DXCompileShader is the one export served by proxy.c.
"""
import struct
import sys


def exports(path):
    data = open(path, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    opt = pe + 24
    exp_rva = struct.unpack_from("<I", data, opt + 96)[0]
    sec = opt + struct.unpack_from("<H", data, pe + 20)[0]
    spans = []
    for i in range(nsec):
        vsize, va, rsize, raw = struct.unpack_from("<IIII", data, sec + 40 * i + 8)
        spans.append((va, max(vsize, rsize), raw))

    def off(rva):
        for va, size, raw in spans:
            if va <= rva < va + size:
                return raw + rva - va
        raise ValueError(hex(rva))

    e = off(exp_rva)
    base, nfunc, nname, _, names, ords = struct.unpack_from("<IIIIII", data, e + 16)
    out = []
    for i in range(nname):
        name_off = off(struct.unpack_from("<I", data, off(names) + 4 * i)[0])
        name = data[name_off:data.index(b"\0", name_off)].decode()
        ordinal = base + struct.unpack_from("<H", data, off(ords) + 2 * i)[0]
        out.append((name, ordinal))
    return out


def main():
    real, out = sys.argv[1], sys.argv[2]
    lines = ["LIBRARY d3dx9_41.dll", "EXPORTS"]
    for name, ordinal in sorted(exports(real), key=lambda x: x[1]):
        if name == "D3DXCompileShader":
            lines.append(f"    {name}=hook_D3DXCompileShader@40 @{ordinal}")
        else:
            lines.append(f"    {name}=d3dx9ms.{name} @{ordinal}")
    open(out, "w").write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
