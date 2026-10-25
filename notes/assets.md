# Game assets: audio and video formats (measured)

Scope: static read of the game folder, the seven `GAME*.DAT` archives and `orig.nosync/lhp_win32.exe`, plus a decode test through FAudio. No game code was run. Scripts and samples: `<job tmp>/assets/` (`names.py`, `ext.py`, `ogg.py`, `sfxpak.py`, `rest_scan.py`, `wavs.py`, `pe.py`, `fa/fa_play.c`, `fa/run.py`, `samples/`).

## Verdict

- XAudio2 gets only PCM: `wFormatTag` 1 (WAVE_FORMAT_PCM) for sound effects and Ogg music, and 0xFFFE (WAVE_FORMAT_EXTENSIBLE, SubFormat PCM) for movie audio.
- The game decodes Ogg Vorbis itself (static libVorbis, string `Xiph.Org libVorbis I 20070622` at file offset 0x91dfb0) and sends 16-bit PCM.
- No xWMA, XMA2 or ADPCM data exists. The exe has `XWMAFormatData` and `XMA2FormatData` classes (console code), but no member carries an XWMA, `dpds` or non-PCM fmt chunk.
- FAudio 26.10 (Homebrew) plays all four format variants to the SDL3 disk driver with the right length (table below). No WMA decoder is needed.
- Video: yes. 8 `.fmv` files in TT's own `FMV!` container, decoded in the exe (`NuFmvStreamChroma`, `NuFmvChromaTexture`, `NuFmvChromaXAudio2Voice`). No Bink, Media Foundation, DirectShow or VfW import (`notes/win32_imports.md`; the string `bink` at 0x880a70 is a platform tag next to `pc_only`, `ps3_only`).

## 1. Game folder

| Type | Files | Bytes |
|---|---|---|
| `GAME*.DAT` | 7 | 6,201,217,385 |
| `LEGOHarryPotter.exe` | 1 | 14,598,144 |
| `redist/` `.cab` (DirectX), vcredist `.exe` | 143 + 2 | ~60 MB |
| `.rtf`, `.dll` (2), `.vdf`, `.inf` | 18 + 4 | small |

No loose audio or video files.

## 2. DAT names and member types

The name table decodes (closes `notes/shader_cache.md` section 5):
- Name node (8 bytes): `i16 a`, `i16 prev`, `u32 name offset`. Folder: `a` = index of its last child; children chain back through `prev` (0 ends). File: `a` = minus the file id. Node 0 is the root.
- Key = FNV-1a with prime 0x199933 (offset basis 0x811c9dc5) over the upper-case path with backslashes, for example `LEVELS\BUILDER\BUILDERBALLS\BUILDERBALLS_NXG.GSC`. Record i belongs to key i of the sorted key array.
- Check: all 11,158 paths in the seven archives map to a key. 0 misses.

Members by extension (all archives; stored = bytes in the archive):

| Ext | Count | Stored bytes | Uncompressed bytes |
|---|---|---|---|
| txt | 2320 | 5,038,094 | 5,038,094 |
| scp | 1833 | 3,177,883 | 3,177,883 |
| gsc | 1365 | 4,424,531,011 | 8,293,112,023 |
| led | 1004 | 2,851,323 | 11,019,895 |
| cd | 632 | 2,460,549 | 2,460,549 |
| tex | 526 | 3,048,555 | 13,814,960 |
| cu2 | 523 | 67,307,296 | 124,667,208 |
| as | 502 | 4,070,431 | 4,070,431 |
| pac | 486 | 51,655,126 | 51,655,126 |
| dno | 353 | 6,795,954 | 14,750,064 |
| ghg | 303 | 52,806,473 | 160,207,560 |
| **ogg** | 289 | 290,692,066 | 290,692,066 |
| giz | 199 | 3,557,682 | 3,557,682 |
| pak | 169 | 123,136,619 | 160,859,805 |
| git | 153 | 4,195,109 | 4,195,109 |
| sfx (text cue lists) | 141 | 251,933 | 251,933 |
| ptl, anm, par, gip, spl, sav, apj | 95, 86, 52, 32, 29, 20, 10 | | |
| **fmv** | 8 | 1,146,212,150 | 1,146,212,150 |
| ft2, gix, csv, cfg, ats, raw, ter, fpk, fnt, gil, png | 8, 5, 3, 3, 2, 2, 1, 1, 1, 1, 1 | | |

Audio members (all in `audio\`): 289 `.ogg` (`_music` 146 = 208,398,044 B; `_cutscenes` 143 = 82,294,022 B), `globalsfx_pc.pak` and `restsfx_pc.pak` (GAME5.DAT, LZ2K), and `audio.cfg`, `music.cfg`, `sfx.txt`, `levelsfx.txt` (text). The exe opens them as `Audio\GlobalSfx_%s.pak` and `Audio\RestSfx_%s.pak`.

LZ2K note: `restsfx_pc.pak` has 4 blocks (of 3,901) whose compressed size equals the uncompressed size (0x8000). They are stored raw. `lz2k.py` tries to Huffman-decode them and fails with `bad code`; `rest_scan.py` copies such blocks as-is. With that, all 3,901 blocks decode.

## 3. Audio formats

Sound-effect paks: a table (head `7a563412`, count) then concatenated RIFF/WAVE files. Every fmt chunk was parsed.

| Source | Files | wFormatTag | Ch | Rate (Hz) | Bits | Block align |
|---|---|---|---|---|---|---|
| globalsfx_pc.pak (9,479,070 B, 109 s) | 120 | 1 | 1 | 44100 (103), 22050 (10), 11025, 18000, 12571 | 16 | 2 |
| restsfx_pc.pak (127,812,348 B, 1,834 s) | 1,286 | 1 | 1 | 44100 (755), 22050 (516), 11025 (5), 12 others 18000-48000 | 16 (1,285), 8 (1) | 2, 1 |
| `.ogg` music and cutscenes | 289 | Vorbis in file; 1 to XAudio2 | 2 | 44100 | 16 after decode | 4 |
| `.fmv` movie audio | 8 | 0xFFFE, SubFormat PCM | from stream | from stream | from stream | |

Extra RIFF chunks (`iXML`, `smpl`, `bext`, `LIST`, `FLLR`, ...) sit beside fmt and data; the game passes only the format.

Static cross-check. CreateSourceVoice is IXAudio2 slot 8 (`+0x20`); the engine keeps IXAudio2 at `[audio + 0x39c8]`. Exactly two call sites:
- 0x0091B867 (fn 0x0091B5E0): format pointer = `FormatData` vfunc slot 2. In all four subclasses it is `lea eax, [ecx+0x20]` (0x009072B0 XMA2, 0x009072F0 XWMA, 0x00907320 WAV, 0x00907370 OGG). WAV keeps the file's fmt chunk. OGG builds it at 0x0091B370: `wFormatTag = 1`, `nChannels`/`nSamplesPerSec` from the Vorbis info (call 0x00A40180), `nAvgBytesPerSec = rate*ch*16/8`, `nBlockAlign = ch*16/8`, `wBitsPerSample = 16`, `cbSize = 0`. The scan for `mov word [r+0x20], imm` with imm 1, 2, 3, 0x161, 0x162, 0x165, 0x166, 0xFFFE found only this write.
- 0x00992EA2 (fn 0x00992C00, `NuFmvChromaXAudio2Voice`): WAVEFORMATEXTENSIBLE on the stack: `wFormatTag = 0xFFFE` (0x00992DD4), `cbSize = 22`, `wValidBitsPerSample = wBitsPerSample`, `dwChannelMask` by channel count (1, 3, 7, 0xF, 0x1F, 0x3F, 0x23F, 0x63F; jump table 0x00992EE8), SubFormat copied from 0x00D19D34 = `00000001-0000-0010-8000-00aa00389b71` (KSDATAFORMAT_SUBTYPE_PCM).

## 4. Decode test

Harness `fa/fa_play.c` calls FAudio 26.10 directly (Homebrew bottle, depends on SDL3 3.4.18) with `SDL_AUDIO_DRIVER=disk`, output in the job folder, signal handlers that print and `_exit(2)`. Our `IXAudio2_CreateSourceVoice` (`runtime/src/win32_xaudio2.c:424`) passes the guest WAVEFORMATEX to `FAudio_CreateSourceVoice` in place, so FAudio sees the same bytes. Mastering voice: 2 ch, 44100 Hz, float32 output. "Sound span" = first to last non-zero output sample.

| Sample | Format given | Exit | Output RMS / peak | Sound span | Input length |
|---|---|---|---|---|---|
| globalsfx WAV at 1,470,464 | tag 1, 1 ch, 44100, 16 | 0 | 0.154 / 0.966 | 0.180 s | 0.181 s |
| restsfx WAV at 81,567,232 | tag 1, 1 ch, 11025, 8 | 0 | 0.135 / 0.969 | 37.802 s | 37.808 s |
| `y2_riddle_intro.ogg` (ffmpeg to s16le), first 3 s | tag 1, 2 ch, 44100, 16 | 0 | 0.093 / 0.517 | 3.000 s | 3.000 s |
| same PCM | tag 0xFFFE, mask 3, SubFormat PCM | 0 | 0.093 / 0.517 | 3.000 s | 3.000 s |

Not tested: the guest path through our layer with a real guest buffer (only the format bytes are shared), and movie audio from a real `.fmv` (the FMV codec is the game's own; its PCM output format follows the 0xFFFE layout above).

## 5. WMA

Not needed: no xWMA data ships. For reference, the Homebrew FAudio 26.10 exports `_decode_wma` and `DATAFORMAT_SUBTYPE_WMAUDIO2/3/LOSSLESS`; whether that decoder works was not tested.

## 6. Video

- Files: `movies\nxg\25fps\ttident.fmv`, `lij2_attract.fmv`, `attract.fmv`, `movies\nxg\30fps\attract.fmv` and 4 more; 1,146,212,150 B total, stored raw. Head `464d5621 12011e00 0005d002` (`FMV!`), with an `FMA` audio tag at byte 0x28.
- Decoder: in the exe. RTTI classes `NuFmvStream`, `NuFmvStreamChroma`, `NuFmvChromaTexture`, `NuFmvChromaNativeTexture<2>`, `NuFmvChromaXAudio2Voice`, `GUIFmv`. Frames go to D3D9 textures; audio goes to XAudio2 as 0xFFFE PCM.
- No video library import. The runtime needs nothing beyond D3D9 and XAudio2 for movies.
