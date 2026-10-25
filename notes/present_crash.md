# Present crash: SIGTRAP in objc_storeWeak under CAMetalLayer nextDrawable

Crash (skip run 2, boot run 28, both 2026-10-24 09:29:53): FATAL signal 5 at
`objc_storeWeak+224` (`brk #0xc472`), stack `-[CAMetalDrawable initWithDrawablePrivate:layer:waitStart:]`
<- `-[CAMetalLayer nextDrawable]` <- KosmicKrisp (Mesa 26.2.5, Homebrew bottle) <- DXVK
`Presenter::presentImage` on the DXVK submit thread.

## Cause (confirmed)

The game destroys its window while its D3D9 device still presents, and the
CAMetalLayer under DXVK's swapchain is freed with it.

1. `brk #0xc472` at objc_storeWeak+224 follows `autda x16, x17` with discriminator
   0x6ae1: the isa of the new weak target failed pointer authentication. The
   drawable init stores its layer argument (x3) weakly, so the CAMetalLayer had
   been freed (lldb disassembly of libobjc and QuartzCore, this machine).
2. Mesa does not retain the layer: `surface->pLayer = pCreateInfo->pLayer`
   (src/vulkan/wsi/wsi_common_metal.c:850, mesa-26.2.5). SDL3 3.4.18 (under
   sdl2-compat) owns it through a metal subview of the window
   (SDL_cocoavulkan.m:237-273), so it dies with the window.
3. Trigger: SIGTERM. SDL3 installs a SIGTERM handler that posts SDL_QUIT;
   `w32_ui_pump` turns it into WM_CLOSE; the game's window procedure calls
   DestroyWindow; `destroy_window` called SDL_DestroyWindow. A CAMetalLayer
   lifetime hook (scratch `present/hook/layerhook.m`, DYLD_INSERT_LIBRARIES)
   shows it, run t1, SIGTERM at 60 s:
   `dealloc CAMetalLayer 0xa09cbebb0 main=1` <- `-[SDL3_cocoametalview dealloc]` <-
   `Cocoa_DestroyWindow` <- `destroy_window` <- `imp_DestroyWindow` <- guest
   `sub_009D4220` <- `imp_PeekMessageA`. The same layer was the nextDrawable target.
   The submit thread then races the free: t1 exited cleanly, runs 2 and 28 trapped.
4. Why it looked like a two-instance crash: `tools/boot/shoot.sh` ends with
   `pkill -TERM -x lhp_boot`, which signals every lhp_boot. Skip run 2 and boot
   run 28 ended in the same second (log mtimes 09:29:53) with the same trap.
   A Cmd+Q or a window close takes the same path.

## Hypothesis ledger

| Hypothesis | Verdict | Evidence |
|---|---|---|
| H1 layer freed while the submit thread presents | Confirmed | points 1-3 |
| H2 nextDrawable without an autorelease pool | Refuted | wsi_common_metal_layer.m:272 wraps nextDrawable in @autoreleasepool and retains the drawable |
| H3 GPU or drawable exhaustion with two processes | Refuted as cause | a drawable timeout returns nil and VK_NOT_READY (wsi_common_metal.c:496-517); it cannot fail an isa check. The two processes share no memory; the second instance only supplied the pkill. |

## Fix

`runtime/src/win32_ui.c`: a window handed to the D3D9 backend
(`w32_hwnd_to_sdl`, called only by `d3d9g_hwnd`) is marked `surface`;
`destroy_window` hides such a window and never frees it, so the layer lives to
process exit. Rejected: retaining the layer in Mesa (needs a Mesa source
build to replace the bottle; it fixes only this driver); SDL_HINT_NO_SIGNAL_HANDLERS
(fixes only SIGTERM, not Cmd+Q or close).

## Verification
Fixed binary (scratch build `ENGINE=MacRecomp-hp-int build_boot.sh hp-lift-cfg/generated`,
with the lifetime hook), two instances at once, SIGTERM to each at 60 s and 75 s:
no `removeFromSuperview`, no CAMetalLayer dealloc, no FATAL, exit 0 both.
Unfixed binary, single instance, SIGTERM at 60 s (run t1): layer freed on the
main thread (stack in point 3); exit 0 because the race was not lost that time.

Repro rates (unfixed): SIGTERM from `shoot.sh` pkill, pair: 2 of 2 trapped (skip run 2,
boot run 28); SIGTERM, single: 0 of 1 trapped but 1 of 1 freed the layer (t1); boot
run 24 (20 min soak, ended by the same pkill): 0 of 1. Pair without SIGTERM: not
run. The cause needs no second process, so the pair rate is not separated out.
The trap is a race; the proof is that the layer is no longer freed.

A first scratch build with the `build_boot.sh` default GEN and ENGINE failed at boot
(bus error at guest 0xfffffffc, as in boot run 1); that is a build input mismatch,
not this change.
