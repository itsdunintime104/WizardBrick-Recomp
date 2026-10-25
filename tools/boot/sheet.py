"""sheet.py DIR OUT.ppm [COLS] [SCALE]: contact sheet (1/SCALE size, default 4) of DIR/frame_<n>.ppm in n order."""
import glob, os, re, sys
d, out = sys.argv[1], sys.argv[2]
cols = int(sys.argv[3]) if len(sys.argv) > 3 else 6
fs = sorted(glob.glob(d + "/frame_*.ppm"), key=lambda p: int(re.findall(r"\d+", os.path.basename(p))[0]))
W, H = 960, 600
S = int(sys.argv[4]) if len(sys.argv) > 4 else 4
w, h = W // S, H // S
rows = (len(fs) + cols - 1) // cols
sheet = bytearray(b"\x40" * (cols * w * rows * h * 3))
for k, p in enumerate(fs):
    raw = open(p, "rb").read()
    px = raw[len(raw) - W * H * 3:]
    ox, oy = (k % cols) * w, (k // cols) * h
    for y in range(h):
        src = (y * S) * W * 3
        dst = ((oy + y) * cols * w + ox) * 3
        for x in range(w):
            i = src + x * S * 3
            sheet[dst + x * 3: dst + x * 3 + 3] = px[i:i + 3]
open(out, "wb").write(b"P6\n%d %d\n255\n" % (cols * w, rows * h) + bytes(sheet))
print(" ".join(os.path.basename(p)[6:-4] for p in fs))
