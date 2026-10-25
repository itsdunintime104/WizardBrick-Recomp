# Shipped shader cache: decode of GAME*.DAT (measured)

Scope: static read of the seven `GAME*.DAT` archives and `lhp_win32.exe`. Nothing was executed. Scripts: `<job tmp>/` (`parse_dir.py`, `lz2k.py`, `scan_shaders.py`, `hashfind.py`).

## Verdict

**runtime HLSL compiler REQUIRED.**
Measurement: 0 of 10,120 archive members up to 256 KB (the whole range where a cache file can sit) contain the `CTAB` constant-table tag, and none starts with a D3D9 version dword. Every D3DX-compiled shader carries `CTAB`. The 451 shaders embedded in the exe all do. The cache loader calls `D3DXGetShaderConstantTable` on the cache bytes, so a real cache file must contain it.
The 343 `CachedShaders\*.DAT` names in the archive are not members. They are lines in a text log of files the developer's game opened (see section 4).

Limits of this result:
- Members are not found by name. The name-to-entry hash is not decoded (section 5). The absence is shown by content scan, not by lookup.
- 669 of the 1,038 members larger than 256 KB (all compressed) were not decompressed. A cache file holds two shader blobs of a few KB each, so it cannot be one of them.

## 1. Archive format (identical in all seven files)

Sources relied on: the public LZ2K specification and reference decoder (`github.com/LunaticInAHat/liblz2k`, README + `src/LZ2K.cpp`, public domain) for the compression. The public `opensagadev/saga` decompilation covers the `.PAK` container (`nufilepak.h`), not this `.DAT` container; it was not used. No public document of the DAT directory was found (GitHub repository search for PakDat, TT Games DAT tools and LEGO GAME.DAT returned nothing). The directory layout below was derived from the loader code in the exe (function at VA 0x9f4720) and confirmed against the bytes.

All integers are little-endian. Offsets are file offsets in `GAME.DAT` unless stated.

| Offset | Size | Field | Value read in GAME.DAT |
|---|---|---|---|
| 0x00 | 4 | directory offset | 0x62cf1689 (1,657,738,889) |
| 0x04 | 4 | directory size | 0x000456f0 (284,400) |
| 0x08 | 0x30 | text `BEGIN_APP_ID_STRING` `PakDat v1.01` `END_APP_ID_STRING` | present |
| 0x38.. | | padding / uninitialised bytes (copy of 0x62cf1689 again at 0x84) | ignored by the loader |
| 0x200 | | first member (record 0 of the offset-sorted order, 489,754 bytes) | `SAMPLE: Audio\...` text list |

Check: directory offset + directory size = file size (1,658,023,289). True for all seven files.

The loader (VA 0x9f4720) reads 8 bytes at file offset 0, seeks to the directory offset, then reads the directory below. Magic: there is none. The only identity strings are the `PakDat v1.01` header text, which the exe never reads (the string is not in the exe).

Directory, starting at the directory offset (GAME.DAT values; dir-relative offsets):

| Dir offset | Size | Field | GAME.DAT value |
|---|---|---|---|
| 0 | 4 | version (signed) | 0xfffffffc = -4 |
| 4 | 4 | N, member count | 5616 |
| 8 | 16*N | member records (below) | ends at dir offset 89,864 |
| 89,864 | 4 | M, name-node count | 6205 |
| +4 | 8*M | name nodes: u32 A, u32 byte offset into the name blob | ends at 139,508 |
| 139,508 | 4 | name blob size B | 122,416 |
| +4 | B | name blob: NUL-separated path components (`\0levels\0builder\0builderballs\0builderballs_nxg.gsc\0...`) | ends at 261,928 |
| 261,928 | 4*N | sorted ascending u32 keys, one per member (22,464 bytes) | 0x000d024e ... 0xfffd4b0c |
| 284,392 | 8 | two zero dwords | the directory ends here (284,400) |

Member record (16 bytes):

| Byte | Field | Meaning (measured) |
|---|---|---|
| 0 | data offset / 256 | file offset = value * 256. First record `0x95fd2` -> 0x95fd200. |
| 4 | stored size | bytes in the archive |
| 8 | uncompressed size | equals stored size when not compressed |
| 12 | flags | low byte: 0 = stored raw, 2 = LZ2K compressed. Upper 3 bytes are uninitialised garbage (for example 0x756100); ignore. |

Proof of the record layout, in GAME.DAT: sorted by offset, no two members overlap (0 overlaps in 5,616); the last member ends at 1,657,738,889, exactly the directory offset; gaps between neighbours are small padding. The first member is at 0x200 with size 489,754.
The key array is sorted. Record i belongs to key i. Version -4 needs no sort in the loader (the swap loop at VA 0x9f4cb5 only runs for versions above -3).

Compression, flag 2: a sequence of LZ2K blocks, each `4C 5A 32 4B` ("LZ2K"), u32 uncompressed size, u32 compressed size, then the data. Blocks are 0x8000 uncompressed bytes. Example, member 0 of GAME.DAT at 0x95fd200: `4c5a324b 00800000 ca3b0000 25a88c99` (block of 0x8000 out, 0x3bca in); whole member stored 347,889 -> 1,194,728 bytes. The decoder in `lz2k.py` (port of the public reference) decompressed the GAME.DAT member at offset 0x62775200 (record with stored 848, uncompressed 10,169) to a valid text `begin page\r\nName "PromptPage"...`, and 10,120 members across the archives with 0 errors.

Counts per archive:

| File | Size | Members | Compressed (flag 2) |
|---|---|---|---|
| GAME.DAT | 1,658,023,289 | 5,616 | 2,037 |
| GAME1.DAT | 856,399,891 | 1,171 | 435 |
| GAME2.DAT | 1,052,633,420 | 1,119 | 416 |
| GAME3.DAT | 1,231,650,067 | 1,351 | 489 |
| GAME4.DAT | 925,179,187 | 1,087 | 380 |
| GAME5.DAT | 268,890,690 | 668 | 525 |
| GAME6.DAT | 208,440,881 | 146 | 0 |
| total | | 11,158 | 4,282 |

## 2. Table of CachedShaders members

Empty. No member of any archive is a cache file. The list that looks like a table of 343 members is a log, not a directory (section 4). I could not give name, offset, stored and uncompressed size for any `CachedShaders` member because none exists in the measured data.

## 3. First 16 bytes and D3D9 verdict

No member qualifies, so there is no member to show. For reference, what a real shader looks like in this project, measured in the exe by the earlier survey: first blob at VA 0xc8c678, bytes `01 01 fe ff fe ff 24 00 43 54 41 42` (vs_1_1 token `0xFFFE0101`, comment token, `CTAB`).

What the archives gave, over 10,120 members of at most 256 KB (all decompressed, 0 errors) plus 369 larger ones (all raw members and part of the compressed ones):
- Members containing `CTAB`: 0 of 10,120. In the 369 larger members: 2, both false positives (GAME.DAT record 11, an FMV movie of 21,996,135 bytes, head `464d5621 12011e00`; GAME.DAT record 395, a text CSV, head `224c4142454c222c22504c4154464f52` = `"LABEL","PLATFORM`).
- Members containing a D3D9 version-dword byte pattern (`00 03 fe ff`, `00 03 ff ff`, vs/ps 2_0 and 1_1 variants) anywhere: 978 of 10,120. None of these also has `CTAB`, and none starts with it at byte 0. They are chance hits in vertex, audio and texture data. Example: GAME.DAT member 25 (stored 66,339, LZ2K, 146,682 bytes), first 16 bytes `00 01 43 c6 00 00 00 01 30 32 55 4e 00 00 00 0a`, which is not a token stream.

Verdict: no member is a Direct3D 9 token stream.

## 4. What the "342 CachedShaders members" are

GAME.DAT member 965 (offset 0x1cde9a00, 722,651 bytes, stored raw) is a CRLF text file of 12,323 lines. It lists relative game paths (`chars\minifigs\schoolkids\GryffindorBoy.txt`) and absolute developer paths. It has repeated lines (4,243 distinct). It is the list of files the game opened on the developer's machine. It contains 343 lines with `CachedShaders`: `...\ukjnewton\Application Data\Warner Bros. Interactive Entertainment\LEGO(R) Harry Potter\CachedShaders\<name>.DAT`: 1 `VERSION.DAT` plus 336 distinct hash names (343 lines, with repeats). These are paths under the developer's Application Data folder, the folder where the engine writes its own cache at run time. The log records that the developer's machine had these files. It does not prove they were packed. The measurement says they were not.
The distinct names (the engine format is `%d%08x.DAT`: one digit, eight hex digits):

```
602a496f8.DAT  6031be5c9.DAT  6044e6c9a.DAT  60491c7d1.DAT  60514d37b.DAT  605943294.DAT  605e210de.DAT  605f19330.DAT
60678fbd8.DAT  6068b04aa.DAT  607bda191.DAT  60867b090.DAT  60a09458e.DAT  60a4a92a8.DAT  60aff9a34.DAT  60b625136.DAT
60c1e24d6.DAT  60d550c24.DAT  60dda5583.DAT  60efe79f0.DAT  60fb15d59.DAT  610698dd1.DAT  61109cbc6.DAT  61141c2d4.DAT
611de3466.DAT  611e6267c.DAT  613faa4a7.DAT  614288723.DAT  6159ee840.DAT  616149ac5.DAT  61917a4b0.DAT  6195d8d58.DAT
6196abc86.DAT  61a714fab.DAT  61b70af60.DAT  61cb65bfa.DAT  61e986f2d.DAT  61ffba1b8.DAT  6213eda4f.DAT  6215e3abb.DAT
62313f877.DAT  6231eea8c.DAT  623697124.DAT  624f8474d.DAT  624f94547.DAT  625838b46.DAT  625a92051.DAT  626422334.DAT
62650e98c.DAT  6265e937a.DAT  626a0cdb0.DAT  62732583d.DAT  627840269.DAT  6284c7d5f.DAT  628768901.DAT  629476db9.DAT
629d1a725.DAT  62b94114b.DAT  62c396768.DAT  62d4a5b5b.DAT  62e144550.DAT  62e1a3fa6.DAT  62e3de43a.DAT  62eea1b9a.DAT
63196a563.DAT  631d40180.DAT  631dba75d.DAT  632f272d0.DAT  632f2fe4b.DAT  6335d0ca5.DAT  63542c271.DAT  635cbfa48.DAT
636908c14.DAT  6371c8f56.DAT  6399ed7aa.DAT  63b1dc536.DAT  63b96977f.DAT  63bbaad05.DAT  63d6614be.DAT  63ddfebfb.DAT
63ec8c8a0.DAT  63f645170.DAT  641511d27.DAT  64178bc4d.DAT  64186e287.DAT  641da1ebd.DAT  6428f0f66.DAT  642a86ec7.DAT
643559e75.DAT  643df6a70.DAT  643f73c85.DAT  6462b94a4.DAT  647bd182f.DAT  64806952c.DAT  6492eda29.DAT  649326a67.DAT
6493c1091.DAT  649a4a0fb.DAT  64a8d1e9f.DAT  64b1f485f.DAT  64c7f016a.DAT  64f378f6f.DAT  6518d90d7.DAT  651db261c.DAT
651fcf91b.DAT  6527b2546.DAT  6539b811b.DAT  654acdc32.DAT  655c9802d.DAT  65799e471.DAT  65859d721.DAT  658d4e6f4.DAT
659eee797.DAT  65a079a06.DAT  65af9c4cc.DAT  65ba3efce.DAT  65db7a63c.DAT  65eb75e40.DAT  6609b8ea6.DAT  660e1f4bd.DAT
663601746.DAT  6649860fb.DAT  6654c1df9.DAT  6659d19df.DAT  66696a403.DAT  667848777.DAT  669684d9c.DAT  669b4bfa0.DAT
66ac971f7.DAT  66b1ab947.DAT  66bd49fa6.DAT  66dd9b503.DAT  66ed76d25.DAT  66ee6e3a5.DAT  670bef9cf.DAT  6719ece0d.DAT
672d19029.DAT  672f7ea57.DAT  67384d664.DAT  673daa2e7.DAT  673f3038d.DAT  674567f95.DAT  675112e33.DAT  6762fe4f1.DAT
6765403ad.DAT  6767b5dad.DAT  677eb709c.DAT  67951c2b2.DAT  679783e01.DAT  67b7ce08a.DAT  67ccfd465.DAT  67ce2f775.DAT
67e1a257a.DAT  67e2d3bc9.DAT  67e4c93b1.DAT  67f0574cc.DAT  680d97d9e.DAT  6818bef24.DAT  68301c7d2.DAT  6835099ed.DAT
683586a15.DAT  683793887.DAT  683de50b4.DAT  683f7f1de.DAT  683f98b28.DAT  684221a55.DAT  684dc449f.DAT  684f5e5f5.DAT
6860f386d.DAT  686979ddb.DAT  686d8c7cd.DAT  686f166a7.DAT  68848c4e2.DAT  688638f88.DAT  689376dff.DAT  68992b826.DAT
68aa6be96.DAT  68b43793e.DAT  68b5370c0.DAT  68b9f9552.DAT  68ca8eaac.DAT  68d5e257a.DAT  68d6433d8.DAT  68e847d08.DAT
68e8c8ef0.DAT  68e9211e7.DAT  68ebbb08d.DAT  68ee61105.DAT  68fe4e8b2.DAT  691680e81.DAT  6917bf45d.DAT  692c480a5.DAT
69356d665.DAT  695423ee7.DAT  69747a03a.DAT  6982bd99d.DAT  69896da02.DAT  69918831d.DAT  699312277.DAT  69932f027.DAT
699738694.DAT  699fd4fcd.DAT  69aaef5a6.DAT  69b5ea4ac.DAT  69c3d7ec5.DAT  69c9ab40b.DAT  69d456114.DAT  69db45ece.DAT
69ec123ff.DAT  69f7a36f5.DAT  6a241df07.DAT  6a311ba11.DAT  6a32b5422.DAT  6a41ab9fc.DAT  6a448de9f.DAT  6a4e9cdac.DAT
6a53f7674.DAT  6a58b2b0b.DAT  6a66bef61.DAT  6a6bf6bcb.DAT  6a90c4f4d.DAT  6a9c2c21f.DAT  6aa4680f4.DAT  6ab14ac33.DAT
6ac03a6e3.DAT  6ac5d454c.DAT  6ae706774.DAT  6aed70f47.DAT  6aed783dc.DAT  6af4f852a.DAT  6b1f5c0e5.DAT  6b2181536.DAT
6b2f2edcf.DAT  6b453780d.DAT  6b47ad967.DAT  6b48772fe.DAT  6b4a75229.DAT  6b4f4103e.DAT  6b98f6f10.DAT  6ba67d238.DAT
6ba768a35.DAT  6ba7b6276.DAT  6bac0b248.DAT  6bafb8399.DAT  6bd1b2b43.DAT  6c02bdb6b.DAT  6c30df077.DAT  6c3181d3b.DAT
6c3441f66.DAT  6c52a79e9.DAT  6c5e58aa4.DAT  6cac79b2a.DAT  6cbac95db.DAT  6cc0906ad.DAT  6ce160205.DAT  6cebd8385.DAT
6ceedab4c.DAT  6d0110c12.DAT  6d1092b3d.DAT  6d19a280d.DAT  6d2b65f78.DAT  6d3a15dc9.DAT  6d3f08daf.DAT  6d50b2b25.DAT
6d5bedbee.DAT  6d791443c.DAT  6d84afc21.DAT  6d99c6a71.DAT  6d9b6ab36.DAT  6db16207a.DAT  6db3f8110.DAT  6db98e923.DAT
6dbcbab34.DAT  6ddb7b5b8.DAT  6ddd57b72.DAT  6dea00643.DAT  6deaafe6a.DAT  6decd3c4d.DAT  6dff08e96.DAT  6dff9e11b.DAT
6e1d74990.DAT  6e1e88415.DAT  6e1fee8fa.DAT  6e208cac5.DAT  6e220fabb.DAT  6e29df924.DAT  6e3d3cac2.DAT  6e4085bbf.DAT
6e4770346.DAT  6e4a94341.DAT  6e4d1dee9.DAT  6e4dfa41f.DAT  6e5ca478b.DAT  6e5d27091.DAT  6e60acadd.DAT  6e65b94e2.DAT
6e6723588.DAT  6e69b3adf.DAT  6e6d638e1.DAT  6e6d7e051.DAT  6e7027e77.DAT  6e7243dff.DAT  6e8b49a91.DAT  6e94a601a.DAT
6e99d9fba.DAT  6e9a3e7a3.DAT  6e9b43ed0.DAT  6ea71f2a1.DAT  6ead69a92.DAT  6eb414aa6.DAT  6ebb0bd82.DAT  6ebde2e38.DAT
6ec3c60f0.DAT  6ec957235.DAT  6ec9b08c3.DAT  6ecbcd35f.DAT  6eeceb753.DAT  6ef491dc1.DAT  6f016cfe8.DAT  6f0906e84.DAT
6f2299809.DAT  6f22c369b.DAT  6f40329ef.DAT  6f61f7f89.DAT  6f636dee3.DAT  6f65ddb6a.DAT  6f6747a00.DAT  6f6b817ba.DAT
6f6d34fbc.DAT  6f6e12143.DAT  6f9ee4aac.DAT  6fa72f7a3.DAT  6fe170d40.DAT  6fe55a9a3.DAT  6fe9f13fb.DAT  6fed07336.DAT
VERSION.DAT
```

## 5. Field that defeated me

The 32-bit key array at dir offset 261,928 (sorted ascending, one per member) is a hash of the member path. I could not reproduce the hash, so I cannot map a path to a record.
- Attempt 1: 11 candidate functions (FNV-1, FNV-1a, djb2, djb2-xor, sdbm, Jenkins one-at-a-time, x31, x65599, CRC32, inverted CRC32, Adler-32) over 2,596 distinct relative paths from member 965, each in 3 case forms and 2 slash forms. Checked against the union of all 11,158 keys. 0 matches (`hashfind.py`).
- Attempt 2: read the loader around VA 0x9f4110 to 0x9f5400 for the lookup code. The lookup goes through virtual calls (VA 0x928430 device layer); I found no hash loop in that range.
The key array starts `4e02 0d00 37e9 1000 13ce 1f00` (0x000d024e, 0x0010e937, 0x001fce13).
Not needed for the verdict, needed only to name members.

## What to do next

- The shipped archives do not remove the need for a shader compiler. Either keep the run-time path (generate source and compile at start-up) or generate the cache offline: run the original game once under Wine (it writes `CachedShaders\*.DAT` into the user's Application Data folder) or use its `-DumpShaders` mode, then cross-compile those files. Neither was run here.
- If the hash is wanted later: look for the lookup in the file-device layer (virtual method at VA 0x928430) or search `lhp_win32.exe` for the loop that walks the `[obj+0x1c]` key array.
