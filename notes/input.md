# Input: what the game uses

Read from `orig.nosync/lhp_win32.exe` with `i686-w64-mingw32-objdump -d`. Thunks: DirectInput8Create 0xAC2410, XInputSetState 0xAC2416, XInputGetCapabilities 0xAC241C, XInputGetState 0xAC2422.

## DirectInput 8

Setup at 0x9B45A0 (object 0xED7F40, called from 0xA3A43C):

- DirectInput8Create(hinst, 0x800, IID_IDirectInput8A) into 0xED7F70.
- Mouse (GUID_SysMouse) into 0xED7F60: GetCapabilities (size 44), SetCooperativeLevel 5 (EXCLUSIVE|FOREGROUND), SetDataFormat c_dfDIMouse2 (0xD90E04, 20 bytes), SetProperty DIPROP_AXISMODE = DIPROPAXISMODE_ABS (0x9B466B), SetProperty DIPROP_BUFFERSIZE = 16 (0x9B4699), Acquire.
- EnumDevices for KEYBOARD, POINTER and GAMECTRL, flag ATTACHEDONLY, callback 0x9AA390.

Callback 0x9AA390 first calls 0x941EA0, the Microsoft IsXInputDevice sample (WMI Win32_PNPEntity, "IG_" in DeviceID). If CoCreateInstance(CLSID_WbemLocator) fails it returns FALSE. Then by dwDevType:

- 0x13 keyboard: CreateDevice(GUID_SysKeyboard) into 0xED7F64, SetCooperativeLevel 0x16 (NONEXCLUSIVE|BACKGROUND|NOWINKEY), SetDataFormat c_dfDIKeyboard (0xD909F4, 256 bytes), Acquire, GetCapabilities.
- 0x14/0x15 joystick or gamepad: CreateDevice, GetCapabilities, SetDataFormat c_dfDIJoystick2 (0xD90BFC, 272 bytes), SetCooperativeLevel 9 (EXCLUSIVE|BACKGROUND), Acquire. Pad slots 4-9.
- Anything else (the enumerated mouse): ignored.

Per frame:

- Keyboard 0x9427B0: Poll; if Poll fails, Acquire, then Poll. GetDeviceState(256) into a double buffer at 0xFF1E40. Any result other than DI_OK zeroes the buffer.
- Mouse 0x9425F0: Poll; if Poll fails, Acquire. GetDeviceState(20) into a double buffer at 0xFEEE68. The caller 0x96D8B2 keeps the low 16 bits of lX/lY/lZ and takes current minus previous (0x96D98E). So the game needs ABS positions.
- Focus change 0x942580 and 0xA3B7E6: Unacquire, SetCooperativeLevel 5 or 6, Acquire.
- Joysticks 0x9AA015: Poll, GetDeviceState(272). If Poll fails: Acquire, Unacquire, Release.

No GetDeviceData call was found for any device (scan of vtable offset 0x28 calls). The mouse buffer is set but not read.

## XInput

0x9AA750 loops users 0-3: XInputGetCapabilities(user, XINPUT_FLAG_GAMEPAD); success marks pad slot 0-3 connected. XInputGetState at 0x9A9E0A, XInputSetState at 0x96F3C8 and 0x9A3CB7..0x9A3F8C.

## What we implement (runtime/src/win32_input.c)

- EnumDevices lists the system keyboard and mouse only, no joysticks. So the WMI check runs only for the keyboard and mouse, and FALSE is the right answer for both. Every pad goes through XInput (slots 0-3). WMI is not needed.
- Keyboard: SDL scancode to DIK table, GetDeviceState from SDL_GetKeyboardState, buffered GetDeviceData from an SDL event watch.
- Mouse: DIPROP_AXISMODE (new): ABS accumulates lX/lY/lZ into a position in GetDeviceState; REL returns deltas.
- Poll returns DI_NOEFFECT; cooperative levels are accepted and ignored.
- XInput from SDL_GameController: connected pad -> ERROR_SUCCESS with dwPacketNumber incremented on change; none -> ERROR_DEVICE_NOT_CONNECTED (1167). SetState -> SDL rumble.

## Scripted input (test tool, off by default)

`HPRECOMP_INPUT_SCRIPT=<file>` presses keys and pad controls on a schedule, so an unattended run gets past the menus. Code: `runtime/src/input_script.c`, `runtime/include/input_script.h`.

- Line: `<ms> <down|up|tap> <name>`. ms counts from the first DirectInput8Create or XInputGetState. `tap` is down, then up 100 ms later. `#` starts a comment. A bad line stops the run with its line number.
- Keys (DIK): RETURN ESCAPE SPACE UP DOWN LEFT RIGHT A..Z 0..9 LSHIFT LCONTROL. They are ORed into the keyboard GetDeviceState bytes.
- Pad: PAD_A PAD_B PAD_X PAD_Y PAD_START PAD_BACK PAD_UP PAD_DOWN PAD_LEFT PAD_RIGHT, PAD_LX=<v> and PAD_LY=<v> (-32768..32767; up sets the stick back to 0). A script with any pad line makes XInput user 0 connected (GetCapabilities, GetState, SetState succeed). Buttons are ORed onto a real pad; a scripted stick other than 0 replaces the real one. dwPacketNumber goes up when the state changes.
- Real SDL keyboard and pads still work.
- Each event logs once to stderr when the game first reads it: `INPUT t=<ms> <down|up> <name>`, where t is the read time, so it can be later than the scripted ms.

Example (`tools/boot/menu_skip.input`, 20 taps):

```
# Menu skip: tap RETURN every 3 s for 60 s.
3000 tap RETURN
6000 tap RETURN
...
60000 tap RETURN
```

## Player 1 keyboard (measured, boot runs 28-31)

Method: tools/boot/key_map.input holds each key 1 s, 3 s apart, in Leaky Cauldron gameplay; frames dumped every 60 Presents and compared before the press and about 200 Presents (1.7 s) after it. The game's own prompts confirm J and K.

| Key | Effect |
|---|---|
| W | Walk away from the camera |
| S | Walk toward the camera |
| A | Walk left |
| D | Walk right |
| J | Cast. Harry: a tap fires spell bolts; "Hold (J) to cast Wingardium Leviosa". Hagrid: umbrella magic with the purple lift circle on objects |
| K | Switch character ("Press (K) to switch characters"; "Tap (K) to regain control of other characters") |
| F2 | Player 2 joins ("Press F2 or any button to start") |
| RETURN | Menus: select (title, Load Game, Confirm Load, Autosave OK); does not skip cutscenes |

No visible movement or action within 1.7 s of a 1 s press: UP, DOWN, LEFT, RIGHT, I, L, U, O, SPACE, LSHIFT, LCONTROL, Z, X, C, Q, E. The jump key is not identified.

The scripted pad (XInput user 0) is player 2 (Hagrid) and splits the screen; pad B is player 2's cast ("Hold (J) (1) / B (2)").

## Tests (tests/test_win32_input.c)

Synthetic SDL events with the dummy video driver: keyboard GetDeviceState and GetDeviceData bytes, BUFFERSIZE, AXISMODE ABS/REL and its errors, mouse wheel and buttons, XInput with no pad (1167) and with an SDL virtual controller; scripted input parse errors, tap timing on an injected clock, keyboard bytes, pad user 0 buttons, stick and packet number, and the event log.

## Untested or open

- A real controller and a real keyboard or mouse in a game run.
- Mouse lX/lY in ABS mode: the test drives only lZ (wheel). A pushed SDL_MOUSEMOTION does not move SDL_GetRelativeMouseState, so lX/lY share the code path but are not exercised.
- Buffered mouse data stays relative in ABS mode (DirectInput reports positions). The game does not read it.
- Poll returns DI_NOEFFECT even when not acquired (DirectInput returns DIERR_NOTACQUIRED). We never unacquire on our own, so the game's recovery path is not needed.
- Exclusive mouse mode does not hide or capture the cursor.

## Cutscene skip (static read of the exe, 2026-10-24)

The first viewing of a story cutscene cannot be skipped. This is the game's rule, not an input bug. Our runs (tapping RETURN, START, A) behave as the original does.

- Skip check 0x796770, called at 0x79710d from the per-frame cutscene update (0x796ec8..0x797233). It allows a skip if 0xF762F9 is set, or 0x793FF0 is nonzero ([0xF7634C]+4), or the config flag `allowcutskip` (byte 0xF762F8, parsed at 0x6FFFEB) is set, or cutscene flag bit 0x08 at +0x1D is clear. 0x793020 is the same test without the other conditions.
- Bit 0x08 means "not seen yet". 0x799741..0x79975E sets it from a bitset at [0xF762E4] (progress block + 0xE094, set at 0x43950F), indexed by the cutscene id (word at +0xC). 0x7945F0 sets the seen bit; 0x794620 reads it.
- When a skip is allowed, it takes two presses. The first press of a pad bit in mask 0x840 (0xE289F4 = 0x850 minus 0x10) starts a 2.6 s prompt timer at +0x298 (0xE28F54 = 2.6). The prompt fades in for 0.3 s, shows fully, and fades out in the last 0.3 s (0x7912F0). A press of bit 0x10 (0xE289D8) while the prompt shows fully, 0.3 to 2.3 s after the first press, skips the cutscene (0x7971B5 sets 0x1000 in +0x88). If the prompt resource [0xF76330] is not loaded, one press skips. Cutscene type [0xF761D4] uses mask 0x40; type [0xF761B4] has no skip.
- So a tap every 3 s never skips: each tap starts the prompt again after the last one has timed out. Use two taps 1 s apart.
- Run (job tmp skip/run2.log, frames2): load Game 1, then RETURN x2, then PAD_START x2, 1 s apart, in the first viewing of the opening. No prompt and no skip, as expected. The run then crashed in the host: CAMetalLayer nextDrawable on the DXVK submit thread at about 135 s, while a second lhp_boot was running. Cause not checked.
- Not confirmed: the keyboard keys and pad buttons behind TT bits 0x10, 0x40 and 0x800 (the per-device to TT-bit mapping after 0x9A9E0A was not traced), whether +0x74 is a "pressed" edge or a "held" mask, and a skip on a second viewing in a game run.

## Default keyboard map (static read of the exe, 2026-10-24)

The defaults are built in code, not read from a file. 0x531CE0 fills two 25-word DIK arrays on the stack (player 1 at esp+0x4C, player 2 at esp+0x18; -1 = unbound) and calls 0x998E60. That installs player 1 into binding slot 0xB and player 2 into slot 0xD (0x998D50, slot table 0xED1E28, stride 0x9C). Array layout (0x998D50): words 0-7 are four axes as (negative, positive) key pairs, words 8-23 are buttons 0-15, word 24 ends an extra list (-1). Each button is stored as type 7 (key) plus a DIK code at slot+0x7D+2*i.

Button i sets TT pad bit `1 << i`: the per-frame loop 0x96DC70 does `or (1 << entry[2]), result` at 0x96E04C, and entry[2] is the same index 0x96D7CB uses to read slot+0x7C+2*i. The bit names follow the TT word (PS2 order, bytes swapped): 0x10 triangle (Xbox Y), 0x20 circle (B), 0x40 cross (A), 0x80 square (X), 0x800 start. The menu code agrees: RETURN = 0x40 (confirm), ESC = 0x20 (back).

[V] = read from code or data. [I] = inferred, not traced.

| Action | TT pad bit | Player 1 key | Player 2 key | Tap or hold | Evidence |
|---|---|---|---|---|---|
| Move left / right / forward / back | axis LX-, LX+, LY-, LY+ | A / D / W / S | LEFT / RIGHT / UP / DOWN | hold | [V] 0x531D84..0x531D97, 0x531DDD..0x531DF2; run 28-31 |
| Camera stick (4 directions) | axes RX, RY | unbound | unbound | - | [V] words 4-7 left at -1 (0x531CE9..) |
| Switch character (TAG) | 0x10 (Y) | K | NUMPAD3 | tap; "Tap (K) to regain control" | [V] key 0x531DBA; [V] game prompt |
| Cast spell (SPECIAL) | 0x20 (B) | J | NUMPAD2 | tap fires; hold = Wingardium Leviosa | [V] key 0x531DC1; [V] game prompt; label [I] |
| Jump (JUMP) | 0x40 (A) | U | NUMPAD5 | tap [I] | [V] key 0x531DC8; action [I] |
| Action: use, build, pick up (ACTION) | 0x80 (X) | H | NUMPAD1 | tap to use or pick up, hold to build [I] | [V] key 0x531DCF; action [I] |
| Pause (START_PAUSE) | 0x800 (start) | RETURN | NUMPADENTER | tap | [V] key 0x531DD6; action [I] |
| Button 0 | 0x1 (L2) | LSHIFT | RSHIFT | ? | [V] key 0x531D9E; action unknown |
| Button 1 | 0x2 (R2) | C | NUMPAD1 | ? | [V] key 0x531DA5 / 0x531D6E; action unknown |
| Button 2 | 0x4 (L1) | LCONTROL | RCONTROL | ? | [V] key 0x531DAC / 0x531E00; action unknown |
| Button 3 | 0x8 (R1) | SPACE | NUMPAD0 | ? | [V] key 0x531DB3 / 0x531E07; action unknown |
| Buttons 8, 9, 10, 12-15 | 0x100, 0x200, 0x400, 0x1000-0x8000 | unbound | unbound | - | [V] left at -1 |

Notes:

- Player 2 has NUMPAD1 on both button 1 and button 7 (0x531D6E and 0x531D73 store 0x4F at esp+0x2A and esp+0x36). That is how the data reads; whether it is a typo in the game was not checked.
- The four unknown buttons are probably TOGGLE_CHAR_UP/DOWN, NEXTSPELL/LASTSPELL or RUN/WALK (label ids 175-198, table 0xE2C168). The code that pairs a label with a control index was not traced. The config menu row order is in the byte list at 0xFC06E8 (copied from 0x531E2A..0x531E8D): controls 0, 1, 2, 3, 12, 13, 14, 15, 16, 18, 17, 19, 23, 4, 5, 6, 7, 8, 10, 11, 9. Controls 0-3 are the move keys and 12-15 the K, J, U, H buttons, in the same order as the labels MOVE_LEFT, MOVE_RIGHT, MOVE_UP, MOVE_DOWN, TAG, SPECIAL, JUMP, ACTION. The action names above come from that order [I].
- Fixed keys, set apart from the bindings (0x96E2D0, called at 0x9B4488): RETURN, NUMPADENTER, SPACE and the left mouse button set 0x40; ESC and BACKSPACE set 0x20 (ESC only when +0x4E1A is 0); F1 sets 0x80; F2 sets 0x10; arrow keys set the d-pad bits (UP 0x1000, RIGHT 0x2000, DOWN 0x4000, LEFT 0x8000). Slots 10-13 clear 0x800 there (0x96E349). Where this word is used (menus only or gameplay too) was not traced.
- Why the scripted test missed jump: U was held 1 s and the frames were compared 1.7 s later, so the player had already landed. H was never tried.
- Key names for tools/boot/*.input: U (jump), H (action), J, K, W, A, S, D, RETURN, SPACE, LSHIFT, LCONTROL, C. input_script.c has no RSHIFT, RCONTROL or NUMPAD names, so player 2's keyboard keys cannot be scripted yet.
- DIK -> name table: 0xED17A8 (pointer per DIK code, English, "(empty)" for 0). Special keys also have an 11-language table at 0xED0F38 (48-byte rows: internal id plus 11 names), and DIK -> internal id byte pairs are at 0xED1B60.

Rebinding: the game has an in-game control setup screen. The strings PC_CONTROL_CONFIG, PC_CONTROL_SETUP, PC_BUTTON_WAIT, PC_RESET_TO_DEFAULTS and "You must configure all items" are in the exe. No key-binding text file name was found ("keyboard", "controls", "KeyMap" give no file names). Where custom bindings are saved (save file or settings) was not traced.
