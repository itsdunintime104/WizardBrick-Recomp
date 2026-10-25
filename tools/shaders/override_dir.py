"""Build a HPRECOMP_SHADER_OVERRIDE folder from the oracle dump.

usage: override_dir.py DUMP OUT [--only HASH ...] [--profile vs_3_0|ps_3_0]
Writes OUT/<sha256 of the .hlsl bytes>.bin for each dumped compile (MS bytecode).
"""
import argparse, hashlib, json, pathlib, shutil

ap = argparse.ArgumentParser()
ap.add_argument("dump"); ap.add_argument("out")
ap.add_argument("--only", nargs="*"); ap.add_argument("--profile")
a = ap.parse_args()
out = pathlib.Path(a.out); out.mkdir(parents=True, exist_ok=True)
n = 0
for h in sorted(pathlib.Path(a.dump).glob("*.hlsl")):
    meta = json.loads(h.with_suffix(".json").read_text())
    if a.profile and meta.get("profile") != a.profile:
        continue
    key = hashlib.sha256(h.read_bytes()).hexdigest()
    if a.only is not None and key not in a.only:
        continue
    b = h.with_suffix(".bin")
    if b.exists() and not (out / f"{key}.bin").exists():
        shutil.copyfile(b, out / f"{key}.bin"); n += 1
print(f"{n} files in {out}")
