# Prior art and reference builds (researched 2026-10-24)

## Mac build: exists, not usable as a reference
- Feral Interactive / Open Planet Software released an Intel-only OS X port on 2011-01-06 (PCGamingWiki; Wikipedia; archived demo `LEGO_Harry_Potter_Years_1-4_Demo_Intel.dmg`).
- Steam appid 21130 has Windows depots only (21131, 228981, 228990; api.steamcmd.net oslist=windows). Mac App Store id411279622 is delisted. A steamcmd macosx download fetches nothing.
- Feral binaries are stripped: Hobbit 379 and Marvel 375 symbols, all imports. HobbitRecomp got function starts from LC_FUNCTION_STARTS (docs/RESEARCH.md:15-17), names from RTTI, asserts and the decomps below. A January 2011 build probably predates LC_FUNCTION_STARTS (not checked).
- GCC + OpenGL vs our MSVC 2005 + D3D9: boundaries and shaders would not map one to one.

## Name sources for the same engine
- https://github.com/wasdhjklxyz/lbtvg: LEGO Batman (2008) PC decomp, MSVC. Closest match.
- https://github.com/opensagadev/saga: matching decomp of LEGO Star Wars: The Complete Saga; `matching.json` holds original symbols from the Android x86 `libTTapp.so`.
- Console decomps: https://github.com/ThePlayerRolo/LegoCloneWarsWii, https://github.com/ellierocks/lsw1-decomp.
- Unchecked lead: the 2016 mobile release of LEGO Harry Potter Years 1-4 may ship an x86 Android library with exported symbols (like saga's source).

## Facts from our exe
- Build tag `lhp-pc-sub1-28-05-10-v11.9k`; PDB path `legogame_Final_PC.pdb` (not shipped); 1,035 RTTI class names.
