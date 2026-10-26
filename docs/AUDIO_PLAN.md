# Audio plan: XAudio2 2.4 on FAudio

The game uses **XAudio2 2.4** (DirectX SDK August 2008), created through
`ole32!CoCreateInstance`, plus the built-in **AudioReverb 2.4** effect and
X3DAudio 1.6. It does not use XACT, DirectSound or its own XAPOs. X3DAudio is
done (`engine/windows/runtime/src/win32_audio.c`, test `engine/windows/tests/test_win32_audio.c`). This file
is the plan for the XAudio2 COM layer.

All addresses are guest VAs in `orig.nosync/lhp_win32.exe`. Scripts that
produced the evidence: `<job tmp>/audio/{scan_guids,xrefs,dis,cbvt}.py`
(capstone, run with `-I`).

## 1. Evidence: API and version

### GUIDs present

GUID values come from Wine (`dlls/xaudio2_7/xaudio_classes.idl`,
`include/xaudio2.idl`, `dlls/xactengine3_7/xact_classes.idl`,
`include/dsound.h`). The debug-engine CLSIDs of 2.0-2.6 are not in Wine; the
2.4 one was read from the binary (below).

| GUID | Value | Where |
|---|---|---|
| CLSID_XAudio2 (2.4) | 03219e78-5bc3-44d1-b92e-f63d89cc6526 | 0x00D17C2C, copy 0x00EEA5CC |
| CLSID_XAudio2_Debug (2.4) | 4256535c-1ea4-4d4b-8ad5-f9db762eca9e | 0x00D17C3C, copy 0x00EEA5DC |
| IID_IXAudio2 (2.0-2.7 share it, `xaudio2.idl:1116`) | 8bcf1f58-9fe7-4583-8ac6-e2adc465c8bb | 0x00D17C1C, copy 0x00EEA5EC |
| CLSID_AudioReverb (2.4) | 8bb7778b-645b-4475-9a73-1de3170bd3af | 0x00D17BAC, copy 0x00EEA61C |
| CLSID_AudioReverb_Debug (2.4) | da7738a2-cd0c-4367-9aac-d7ead7c64f98 | 0x00D17BBC, copy 0x00EEA62C |
| CLSID_AudioVolumeMeter (2.4) | c7338b95-52b8-4542-aa79-42eb016c8c1c | 0x00EEA5FC only (no code reference) |
| CLSID_XACTEngine (3.4) | 0977d092-2d95-4e43-8d42-9ddcc2545ed5 | 0x00EEA58C only (no code reference) |
| CLSID_DirectSound, DirectSound8, Capture, Capture8, IID_IDirectSound, IID_IDirectSound8 | (dsound.h) | 0x00D2C994-0x00D2CA44 (no CoCreateInstance site uses them) |

No CLSID of XAudio2 2.0-2.3 or 2.5-2.7, no 2.8/2.9 IID, no XACT CLSID other
than 3.4, and no IID_IXAPO / IID_IXAPOParameters (`include/xapo.idl:106,144`)
is in the file. The `.data` block 0x00EEA58C-0x00EEA63B is the set of GUIDs
the SDK headers define; no code references it.

### CoCreateInstance call sites (IAT slot 0x010DEE38)

| Call site | CLSID pushed | IID pushed | What it is |
|---|---|---|---|
| 0x00907213 | 0x00D17C2C, or 0x00D17C3C when `Flags & 1` | 0x00D17C1C | inline `XAudio2Create` (fn 0x009071F0); then calls vtable slot 5 (`[edx+0x14]`, Initialize) at 0x0090722E |
| 0x0095FE03 | 0x00D17BAC (AudioReverb 2.4) | 0x00D17B9C (IUnknown) | reverb creation in fn 0x0095FDB0 |
| 0x00906FD0 | 0x00D17BAC or 0x00D17BBC | IUnknown | inline `XAudio2CreateReverb` (fn 0x00906FB0); no direct caller found |
| 0x00941EF5 | 0x00D18DD4 CLSID_WbemLocator | 0x00D18DE4 IID_IWbemLocator | WMI, not audio |
| 0x00A21685 | 0x00D1C528 CLSID_GameExplorer | 0x00D1C538 IID_IGameExplorer | gameux, not audio |

### The game's calls match the 2.4 vtables

The only `XAudio2Create` caller is 0x0091B8F5 (fn 0x0091B8E0, the audio
init). It passes `Flags = 0`, `Processor = -1`, so the debug CLSID is never
used. Then:

| Site | Call | Slot | Proves |
|---|---|---|---|
| 0x0091B91F | `IXAudio2::CreateMasteringVoice(pp, 0, 0, 0, DeviceIndex, NULL)` | 10 (`+0x28`) | 2.0-2.7 IXAudio2 order (2.8 moved it to slot 7) |
| 0x0091B94B | `IXAudio2::GetDeviceDetails(0, &details)` | 4 (`+0x10`) | 2.0-2.7 only |
| 0x0091B95F | reads `details + 0x418` (`OutputFormat.dwChannelMask`) for X3DAudioInitialize | | XAUDIO2_DEVICE_DETAILS layout, pack 1 |
| 0x0095FDC8 | `master->GetVoiceDetails(&d)`; reads `d + 8` | voice 0 | 12-byte XAUDIO27_VOICE_DETAILS (no ActiveFlags) |
| 0x0095FDEC | `IXAudio2::CreateSubmixVoice(pp, 1, rate, 0, 1, NULL, NULL)` | 9 (`+0x24`) | |
| 0x0095FE1D-0x0095FE6E | builds `XAUDIO2_VOICE_SENDS{2, {Flags 0, master}, {Flags 0, submix}}` and `XAUDIO2_EFFECT_CHAIN{1, {reverb, TRUE, 1}}` | | 2.4 send descriptors (2.3 used `IXAudio2Voice**`) |
| 0x0095FE7B | `submix->SetEffectChain(&chain)` | voice 2 (`+0x08`) | |
| 0x0095FEBC | `submix->SetVolume(1.0f, 0)` | voice 12 (`+0x30`) | 2.4 voice order (2.3 has SetVolume at slot 10) |
| 0x00960053 | `voice->DestroyVoice()` | voice 18 (`+0x48`) | 2.4 voice order (2.3: slot 16) |

### Guest callback objects

Three guest classes implement IXAudio2VoiceCallback. Their vtables are in
`.rdata`; each entry's `ret N` matches the stdcall signature with `this` on
the stack (`ret 8`, `ret 4`, `ret 4`, `ret 8`, `ret 8`, `ret 8`, `ret 0xC`):

| Vtable | Installed at | Entries |
|---|---|---|
| 0x00D17F5C | 0x0091B526, 0x0091B5D2, 0x0091B626, 0x00960030 | 0x0091B590 0x0091B580 0x0091B550 0x0091B5A0 0x0091B570 0x0091B5B0 0x0091B5C0 (only OnStreamEnd, slot 2, has a body) |
| 0x00D193AC | 0x0094ABE7, 0x009C12C7 | 0x0094AC40 0x0094AC30 0x009AE540 0x0091CDA0 0x0094AC10 0x0094AC50 0x0094AC60 |
| 0x00D197C4 | 0x00969BD3, 0x009A8A14 | 0x00969C20 0x00969C10 0x00969C00 0x00969C30 0x00992A90 0x00969C40 0x00969C50 |

No IXAudio2EngineCallback implementation was confirmed (the 3-entry
`ret 4, ret 4, ret 8` pattern also matches many unrelated vtables). Implement
RegisterForCallbacks fully anyway.

### X3DAudio calling convention

Both imports are cdecl. X3DAudioInitialize at 0x0091B96A is followed by
`add esp, 0xc`. X3DAudioCalculate at 0x0098AA34 is not followed by a pop:
the caller reads its DSP struct at `[esp+0x6C]` right after the call and at
`[esp+0x5C]` after `add esp, 0x10` (0x0098AA42, 0x0098AA55), which is the
same slot only if the 20 argument bytes stayed on the stack; the 4 bytes
left are reused as an argument slot for 0x0091C090 (`ret 8`). The game calls
it with `Flags = 1` (matrix only) and copies 8 coefficients out.

**Open item for the lifter lane:** fn 0x0098A670 (analysis.json, size 1210),
which holds the only X3DAudioCalculate call, is absent from
`generated_6864e48`. Searched for `sub_0098A670` and the return address
`0x0098AA3A` in every generated file.

## 2. Interfaces (vtable order for 2.4)

Version mapping: `dlls/xaudio2_4/Makefile.in` builds with `XAUDIO2_VER=4`;
`dlls/xaudio2_7/xaudio_private.h:55-60,104-111,144-150` uses the
`IXAudio27*` interfaces for `4 <= XAUDIO2_VER <= 7`; `compat.c:50-56` lists
the 2.4 ABI changes. Interface text: `include/xaudio2.idl`. Wine vtables:
`dlls/xaudio2_7/compat.c:2834-2851` (IXAudio27), `:865-895` (source voice),
`:1435` (submix), `:1995` (mastering).

All methods are `__stdcall`, `this` is `W32_ARG(0)`, arguments start at
`W32_ARG(1)`, and a method with n arguments pops `4 + 4*(1+n)` bytes
(`com_guest.h`). Floats arrive as raw 32-bit slots. "n" below excludes `this`.

### IXAudio2 (IXAudio27, `xaudio2.idl:1120-1173`): 16 methods

| # | Method | n |
|---|---|---|
| 0 | QueryInterface(riid, ppv) | 2 |
| 1 | AddRef() | 0 |
| 2 | Release() | 0 |
| 3 | GetDeviceCount(UINT32 *pCount) | 1 |
| 4 | GetDeviceDetails(UINT32 Index, XAUDIO2_DEVICE_DETAILS *p) | 2 |
| 5 | Initialize(UINT32 Flags, XAUDIO2_PROCESSOR Processor) | 2 |
| 6 | RegisterForCallbacks(IXAudio2EngineCallback *cb) | 1 |
| 7 | UnregisterForCallbacks(IXAudio2EngineCallback *cb) (void) | 1 |
| 8 | CreateSourceVoice(pp, const WAVEFORMATEX *fmt, UINT32 Flags, float MaxFrequencyRatio, IXAudio2VoiceCallback *cb, const XAUDIO2_VOICE_SENDS *sends, const XAUDIO2_EFFECT_CHAIN *chain) | 7 |
| 9 | CreateSubmixVoice(pp, UINT32 InputChannels, UINT32 InputSampleRate, UINT32 Flags, UINT32 ProcessingStage, sends, chain) | 7 |
| 10 | CreateMasteringVoice(pp, UINT32 InputChannels, UINT32 InputSampleRate, UINT32 Flags, UINT32 DeviceIndex, chain) | 6 |
| 11 | StartEngine() | 0 |
| 12 | StopEngine() (void) | 0 |
| 13 | CommitChanges(UINT32 OperationSet) | 1 |
| 14 | GetPerformanceData(XAUDIO2_PERFORMANCE_DATA *p) (void) | 1 |
| 15 | SetDebugConfiguration(const XAUDIO2_DEBUG_CONFIGURATION *p, void *reserved) (void) | 2 |

### IXAudio2Voice (IXAudio27Voice, `xaudio2.idl:482-557`): 19 methods, no IUnknown

Voices are not COM objects: slot 0 is GetVoiceDetails, not QueryInterface.

| # | Method | n |
|---|---|---|
| 0 | GetVoiceDetails(XAUDIO27_VOICE_DETAILS *p) (void) | 1 |
| 1 | SetOutputVoices(const XAUDIO2_VOICE_SENDS *p) | 1 |
| 2 | SetEffectChain(const XAUDIO2_EFFECT_CHAIN *p) | 1 |
| 3 | EnableEffect(UINT32 idx, UINT32 OperationSet) | 2 |
| 4 | DisableEffect(UINT32 idx, UINT32 OperationSet) | 2 |
| 5 | GetEffectState(UINT32 idx, BOOL *pEnabled) (void) | 2 |
| 6 | SetEffectParameters(UINT32 idx, const void *p, UINT32 size, UINT32 OperationSet) | 4 |
| 7 | GetEffectParameters(UINT32 idx, void *p, UINT32 size) | 3 |
| 8 | SetFilterParameters(const XAUDIO2_FILTER_PARAMETERS *p, UINT32 OperationSet) | 2 |
| 9 | GetFilterParameters(XAUDIO2_FILTER_PARAMETERS *p) (void) | 1 |
| 10 | SetOutputFilterParameters(IXAudio2Voice *dst, const XAUDIO2_FILTER_PARAMETERS *p, UINT32 OperationSet) | 3 |
| 11 | GetOutputFilterParameters(IXAudio2Voice *dst, XAUDIO2_FILTER_PARAMETERS *p) (void) | 2 |
| 12 | SetVolume(float v, UINT32 OperationSet) | 2 |
| 13 | GetVolume(float *p) (void) | 1 |
| 14 | SetChannelVolumes(UINT32 ch, const float *p, UINT32 OperationSet) | 3 |
| 15 | GetChannelVolumes(UINT32 ch, float *p) (void) | 2 |
| 16 | SetOutputMatrix(IXAudio2Voice *dst, UINT32 src_ch, UINT32 dst_ch, const float *p, UINT32 OperationSet) | 5 |
| 17 | GetOutputMatrix(IXAudio2Voice *dst, UINT32 src_ch, UINT32 dst_ch, float *p) (void) | 4 |
| 18 | DestroyVoice() (void) | 0 |

### IXAudio2SourceVoice (IXAudio27SourceVoice, `xaudio2.idl:739-768`): 29 methods

Slots 0-18 as IXAudio2Voice, then:

| # | Method | n |
|---|---|---|
| 19 | Start(UINT32 Flags, UINT32 OperationSet) | 2 |
| 20 | Stop(UINT32 Flags, UINT32 OperationSet) | 2 |
| 21 | SubmitSourceBuffer(const XAUDIO2_BUFFER *p, const XAUDIO2_BUFFER_WMA *wma) | 2 |
| 22 | FlushSourceBuffers() | 0 |
| 23 | Discontinuity() | 0 |
| 24 | ExitLoop(UINT32 OperationSet) | 1 |
| 25 | GetState(XAUDIO2_VOICE_STATE *p) (void; no Flags in 2.4) | 1 |
| 26 | SetFrequencyRatio(float r, UINT32 OperationSet) | 2 |
| 27 | GetFrequencyRatio(float *p) (void) | 1 |
| 28 | SetSourceSampleRate(UINT32 rate) | 1 |

### IXAudio2SubmixVoice and IXAudio2MasteringVoice: 19 methods each

Exactly slots 0-18 (`xaudio2.idl:828-830, 862-864`). The 2.4 mastering voice
has no GetChannelMask (added in 2.8, `compat.c:80`).

### Guest-implemented callbacks (the host calls into them)

IXAudio2VoiceCallback (`xaudio2.idl:902-919`), 7 slots, no IUnknown:

| # | Method | Guest args (this first) |
|---|---|---|
| 0 | OnVoiceProcessingPassStart(UINT32 BytesRequired) | 2 |
| 1 | OnVoiceProcessingPassEnd() | 1 |
| 2 | OnStreamEnd() | 1 |
| 3 | OnBufferStart(void *pBufferContext) | 2 |
| 4 | OnBufferEnd(void *pBufferContext) | 2 |
| 5 | OnLoopEnd(void *pBufferContext) | 2 |
| 6 | OnVoiceError(void *pBufferContext, HRESULT Error) | 3 |

IXAudio2EngineCallback (`xaudio2.idl:301-308`), 3 slots: 0
OnProcessingPassStart() (1 arg), 1 OnProcessingPassEnd() (1), 2
OnCriticalError(HRESULT) (2).

Call one as `guest_call_cdecl(MEM32(MEM32(obj) + 4*slot), nargs, {obj, ...})`
(`engine/runtime/include/guest_call.h`). The callee is stdcall and pops its
own arguments; guest_call_cdecl restores `g_esp`, so this is safe
(`docs/WIN32_THUNKS.md`, "Callbacks into guest code").

FAudio's host structs list the same callbacks in a different order
(`FAudio.h:1300` voice: OnBufferEnd, OnBufferStart, OnLoopEnd, OnStreamEnd,
OnVoiceError, PassEnd, PassStart; `FAudio.h:1231` engine: OnCriticalError,
PassEnd, PassStart). Fill the host struct by name and use the guest slot
numbers above.

### AudioReverb object

CLSID_AudioReverb 2.4 (and its debug CLSID) returns a guest IUnknown object
(3 methods: QueryInterface, AddRef, Release). Its host side owns an `FAPO *`
from `FAudioCreateReverb(&fapo, 0)` (`FAudioFX.h:272`; Wine uses the
non-"9" reverb below 2.9, `dlls/xaudio2_7/xapo.c:314-325`). QueryInterface
answers IUnknown only (the game has no IXAPO IID). Release at 0 calls
`fapo->Release(fapo)` and `com_guest_free`.

## 3. Structs that cross the boundary

Guest offsets: `#pragma pack(1)` structs (XAudio2 headers are pack 1,
`xaudio2.idl:24`) compiled with `clang -target i686-w64-windows-gnu`. Host
sizes: arm64 with `FAudio.h`.

| Guest struct (size) | Guest offsets | Host FAudio struct (size) | Handling |
|---|---|---|---|
| WAVEFORMATEX (18) / WAVEFORMATEXTENSIBLE (40) | tag 0, ch 2, rate 4, avg 8, align 12, bits 14, cbSize 16; Samples 18, dwChannelMask 20, SubFormat 24 | FAudioWaveFormatEx (18) / Extensible (40) | same bytes: pass `guest_ptr` |
| XAUDIO2_DEVICE_DETAILS (1068) | DeviceID 0 (UTF-16 x256), DisplayName 512, Role 1024, OutputFormat 1028 | FAudioDeviceDetails (1068) | same bytes: FAudio writes `guest_ptr` |
| XAUDIO27_VOICE_DETAILS (12) | CreationFlags 0, InputChannels 4, InputSampleRate 8 | FAudioVoiceDetails (16, has ActiveFlags at 4) | copy fields |
| XAUDIO2_SEND_DESCRIPTOR (8) | Flags 0, pOutputVoice 4 | FAudioSendDescriptor (12) | voice: `com_guest_self` -> host voice -> `FAudioVoice *` |
| XAUDIO2_VOICE_SENDS (8) | SendCount 0, pSends 4 | FAudioVoiceSends (12) | host array of SendCount descriptors |
| XAUDIO2_EFFECT_DESCRIPTOR (12) | pEffect 0, InitialState 4, OutputChannels 8 | FAudioEffectDescriptor (16) | pEffect must be our AudioReverb object -> its `FAPO *`; anything else: `UNSUPPORTED xaudio2: effect 0x%08X` and abort |
| XAUDIO2_EFFECT_CHAIN (8) | EffectCount 0, pEffectDescriptors 4 | FAudioEffectChain (12) | host array |
| XAUDIO2_BUFFER (36) | Flags 0, AudioBytes 4, pAudioData 8, PlayBegin 12, PlayLength 16, LoopBegin 20, LoopLength 24, LoopCount 28, pContext 32 | FAudioBuffer (44; pAudioData 8, pContext 36) | copy; `pAudioData = guest_ptr(g)`; `pContext = (void *)(uintptr_t)g_context` |
| XAUDIO2_BUFFER_WMA (8) | pDecodedPacketCumulativeBytes 0, PacketCount 4 | FAudioBufferWMA (16) | copy; pointer -> `guest_ptr` |
| XAUDIO2_VOICE_STATE (16) | pCurrentBufferContext 0, BuffersQueued 4, SamplesPlayed 8 (UINT64) | FAudioVoiceState (20; SamplesPlayed 12) | copy back; context -> `(uint32_t)(uintptr_t)` |
| XAUDIO2_FILTER_PARAMETERS (12) | Type 0, Frequency 4, OneOverQ 8 | FAudioFilterParameters (12) | same bytes |
| XAUDIO2_PERFORMANCE_DATA (64) | 2 x UINT64, 12 x UINT32 (`xaudio2.idl:155-171`) | FAudioPerformanceData (64) | same bytes |
| XAUDIO2_DEBUG_CONFIGURATION (24) | 6 x 4 bytes | FAudioDebugConfiguration (24) | same bytes |
| XAUDIO2FX_REVERB_PARAMETERS (52, through SetEffectParameters) | WetDryMix 0, ReflectionsDelay 4, 12 x BYTE 8-19, 8 floats 20-51 | FAudioFXReverbParameters (52) | same bytes; check `size == 52` |
| float arrays (volumes, matrices) | | | `guest_ptr` |

Guest NULL is 0: map it to host NULL before every FAudio call
(`guest_ptr(0)` is not NULL).

Error codes: return FAudio's result unchanged; FAudio uses the XAudio2
HRESULT values (Wine casts them straight through, `compat.c:2050`).

## 4. Threading

FAudio 26.10 (Homebrew) runs its mixer on SDL3's audio thread
(`otool -L libFAudio.dylib`: libSDL3). Voice and engine callbacks come on
that thread, and that thread must run guest code:

1. In every host callback shim, call `w32_host_thread_attach()`
   (`engine/windows/runtime/include/win32_thunk.h`, sync slice) before the first guest call.
   It gives the thread a guest stack (`guest_thread_attach`) and a TIB; lifted
   MSVC code reads `fs:[0]` in its SEH prologs, so a bare
   `guest_thread_attach` is not enough. It is safe to call every time.
2. Then `guest_call_cdecl` as in section 2.
3. Guest code in a callback may call XAudio2 methods (for example
   SubmitSourceBuffer from OnBufferEnd). The thunk then runs on the mixer
   thread and calls FAudio from inside FAudio's callback, as XAudio2 allows.
   So the com_guest object table must be safe for `com_guest_self` on one
   thread while another thread runs `com_guest_new`/`com_guest_free`.
4. The game already synchronises its callbacks with its main thread, as it
   must on Windows; the host adds no lock around guest calls.

## 5. Audio buffer memory

SubmitSourceBuffer passes `guest_ptr(pAudioData)` to FAudio, no copy:

- XAudio2 requires the caller to keep the buffer valid and unchanged until
  OnBufferEnd for it. The game already follows that rule, and FAudio needs
  the same guarantee.
- The guest window is one fixed host mapping (`guest_mem.h`): a guest
  address always maps to the same host address, so the pointer never
  goes stale.
- Copying would cost memory and time for long or looping buffers, and would
  hide game bugs.

FAudio copies the FAudioBuffer struct itself into its queue, so the host
FAudioBuffer can be a local in the thunk. `pContext` is the guest's 32-bit
value carried in a host pointer and converted back for OnBufferStart,
OnBufferEnd, OnLoopEnd, OnVoiceError and GetState.

## 6. Host objects

- Engine: `struct xa2_engine { FAudio *fa; FAudioEngineCallback cb; uint32_t refs; uint32_t guest_cbs[8]; ... }`.
  Create with `FAudioCOMConstructEXT(&fa, 4)` (`FAudio.h:528`; Wine passes
  `XAUDIO2_VER` the same way, `xaudio_dll.c:1859`), so FAudio applies
  2.4 behaviour. Initialize calls `FAudio_Initialize(fa, Flags,
  FAUDIO_DEFAULT_PROCESSOR)`. Register `FAudio_RegisterForCallbacks` once,
  and fan out to the registered guest engine callbacks.
- Voice: `struct xa2_voice { FAudioVoiceCallback cb; /* first member */ FAudioVoice *v; uint32_t guest_obj; uint32_t guest_cb; }`.
  The FAudio callback shim casts `FAudioVoiceCallback *` back to
  `struct xa2_voice *`. DestroyVoice calls `FAudioVoice_DestroyVoice` (no
  callbacks run after it returns), then `com_guest_free`, then frees the
  struct.
- One guest vtable per interface: IXAudio2 (16), source voice (29), submix
  voice (19), mastering voice (19), AudioReverb IUnknown (3). Slots 0-18 of
  the three voice tables point at the same 19 thunks.
- Register CLSID_XAudio2 2.4, CLSID_XAudio2_Debug 2.4, CLSID_AudioReverb 2.4
  and CLSID_AudioReverb_Debug 2.4 with `com_register_class`. IID check:
  IID_IXAudio2 or IUnknown for the engine, IUnknown for the reverb; else
  `E_NOINTERFACE`.

## 7. Dependencies on other slices

- COM slice (`com_guest.c`): `com_guest_new/self/free`, `com_register_class`,
  CoCreateInstance dispatch. It must not assume slots 0-2 are IUnknown
  (voices have no IUnknown) and must be thread-safe (section 4, point 3).
- Sync slice: `w32_host_thread_attach`.
- Lifter: fn 0x0098A670 must be lifted (section 1).

## 8. Slices

All in the audio slice: new `engine/windows/runtime/src/win32_xaudio2.c` (one file; slices
run one after another) and `engine/windows/tests/test_win32_xaudio2.c`. The test links
`win32_xaudio2.c`, `guest_mem.c`, `host_funcs.c`, `guest_call.c`,
`recomp_glue.c` and `-lFAudio`, with doubles for `com_guest_*`,
`com_register_class` and `w32_host_thread_attach` in the test file (no
win32_core.c, no com_guest.c). Guest callback objects in the test are guest
memory whose vtable slots are `host_func_va` thunks that record the call and
pop like stdcall. Headless audio: run with `SDL_AUDIO_DRIVER=dummy`
(**not verified** that SDL3's dummy driver pulls audio on a thread; if
callbacks never arrive, use `SDL_AUDIO_DRIVER=disk` with
`SDL_AUDIO_DISK_OUTPUT_FILE` in the job tmp folder).

Every check calls each thunk through a guest stack and checks the pop count
from section 2 and that ebx/esi/edi/ebp are unchanged.

| Slice | Content | Finish line (the test, run as one command, prints PASS) |
|---|---|---|
| X1 engine | factory for the 2 XAudio2 CLSIDs; all 16 IXAudio2 methods; CreateMasteringVoice; the 19 voice methods on the mastering voice only as far as GetVoiceDetails and DestroyVoice | replays fn 0x0091B8E0: factory(IID_IXAudio2) -> Initialize(0, 0xFFFFFFFF) -> CreateMasteringVoice(pp, 0, 0, 0, 0, NULL) returns 0 and a guest object -> GetDeviceDetails(0) writes a non-zero dwChannelMask at +0x418 -> master GetVoiceDetails writes InputSampleRate > 0 at +8 and nothing past +12; QueryInterface with IID_IUnknown gives the same object, another IID gives E_NOINTERFACE; Release to 0 frees the guest object |
| X2 voices and effects | all 19 IXAudio2Voice methods; CreateSubmixVoice; VOICE_SENDS and EFFECT_CHAIN marshalling; AudioReverb factory and object | replays fn 0x0095FDB0: CreateSubmixVoice(pp, 1, rate, 0, 1, NULL, NULL) -> AudioReverb via the factory with IID_IUnknown -> SetEffectChain({1, {reverb, 1, 1}}) returns 0 -> SetVolume(1.0f) then GetVolume reads 1.0f -> SetEffectParameters(0, 52-byte reverb params) then GetEffectParameters returns the same 52 bytes -> SetOutputVoices with sends {master} returns 0 -> an unknown pEffect aborts with the UNSUPPORTED line (fork the check) |
| X3 source voices and callbacks | CreateSourceVoice; slots 19-28; XAUDIO2_BUFFER/STATE marshalling; voice callback shims with `w32_host_thread_attach` | 16-bit mono PCM, 44100 Hz, 4410 frames in guest memory, `Flags = END_OF_STREAM`, `pContext = 0x12345678`, guest callback object with the 7-slot vtable: Start -> within 2 s OnBufferStart and OnBufferEnd arrive with context 0x12345678, then OnStreamEnd; all on a thread other than main, and the `w32_host_thread_attach` double ran on that thread before the first guest call; GetState reports SamplesPlayed >= 4410 and BuffersQueued 0; SetFrequencyRatio(0.5f) then GetFrequencyRatio reads 0.5f; DestroyVoice -> no callback after it returns |
| X4 game run | link into the game build | the game reaches its menu with no `UNSUPPORTED xaudio2` or `UNSUPPORTED com` line, and a log of the first 50 XAudio2 calls shows CreateMasteringVoice, CreateSubmixVoice, CreateSourceVoice and SubmitSourceBuffer returning 0; sound is audible (owner check). Needs fn 0x0098A670 lifted and the COM and sync slices |

## 9. Risks

- **Source formats.** The game's `wFormatTag` values are not known yet.
  FAudio decodes PCM, float and MS-ADPCM itself; xWMA needs FAudio built
  with a WMA decoder, which the Homebrew bottle may not have. X4 must log the
  format tags of the first CreateSourceVoice calls.
- **FAudio copies the format.** Passing `guest_ptr` for the WAVEFORMATEX
  relies on FAudio copying it during CreateSourceVoice, as XAudio2 does and
  as Wine relies on. Not checked in FAudio source (not on this machine).
- **Callback rate.** OnVoiceProcessingPassStart/End fire per voice per
  quantum (about every 10 ms). Each is a guest call; the 0x00D17F5C class has
  empty bodies for them. Measure in X4 before optimising.
