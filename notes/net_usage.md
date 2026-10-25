# What the game uses Winsock for

**Conclusion:** Winsock serves a boot-time local IP lookup whose result is
ignored, a one-shot HTTP error reporter that runs only after a fatal NuError,
and a developer "Target Manager filesystem" link that runs only with
`TMSERVER <host>` on the command line; the game can boot and play with
networking unavailable.

Measured by disassembling `orig.nosync/lhp_win32.exe` (capstone) and scanning
the lifted C (`generated_6864e48/lhp_*.c`) for the 22 WS2_32 IAT slots.

## Ordinals

All 22 ordinals in `notes/win32_imports.md` agree with the standard
Microsoft `ws2_32.dll` export ordinals (1 accept, 2 bind, 3 closesocket,
4 connect, 8 htonl, 9 htons, 10 ioctlsocket, 11 inet_addr, 12 inet_ntoa,
13 listen, 15 ntohs, 16 recv, 17 recvfrom, 19 send, 20 sendto,
21 setsockopt, 23 socket, 52 gethostbyname, 57 gethostname,
111 WSAGetLastError, 115 WSAStartup, 116 WSACleanup). The argument counts
match the pushes at the call sites below, and no caller adjusts `esp` after
the call, so all 22 are stdcall.

## Layers

| Layer | Functions | What it is |
|---|---|---|
| Socket class | `0x93C3E0`–`0x93CB67` | Thin wrapper. `this`: `+0` SOCKET, `+4` type (1 TCP, 0 UDP), `+8` closed flag. WSAStartup(2.2) is reference-counted through `0x1042E6C` (InterlockedIncrement/Decrement), success flag `0x1042E5D`. |
| TCP stream | `0x96AEE0`–`0x96B0C0` | `0x96AEE0` = WSAStartup + create socket; `0x96AF50` = close + WSACleanup; `0x96B020`/`0x96ACC0` = send/recv. |
| HTTP client "NU2API" | `0x9BB160`, `0x997790`–`0x997A69` | `GET %s%s HTTP/1.0`, `POST %s HTTP/1.0`, `User-Agent: NU2API/1.0`, port 80 (`0x9BB176`). |
| Error reporter | `0x9C2150` | POSTs `appName=%s&ip=%s&platform=WINPC&errorCode=…` to `ukknulp-iportal.intl.warnerbros.com/DCTech/getdata.php`. |
| Target Manager filesystem | `0x9E4CE0`, `0x9D2F70`, `0x966680`–`0x966E40`, `0x98EE40`–`0x98F30E`, `0x9A74B0`, `0x9A7770` | Dev host-file link over TCP: default host `10.135.72.60`, port `0xC738` (51000) (`0x9A7790`), handshake `"__PC"` + `"0304"`. |

## Call sites

Containing function, then call VA.

| Import | Call sites | Notes |
|---|---|---|
| WSAStartup | `sub_0093C3E0` 0x93C3F8; `sub_0096AEE0` 0x96AEFD; also unlifted `0x93C4C0` 0x93C4DD | version 0x202, WSADATA on the stack (0x190 bytes) |
| WSACleanup | `sub_0093C3E0` 0x93C49B; `sub_0096AF50` 0x96AF94 | |
| gethostname | `sub_0093C3E0` 0x93C44A | buffer 255 |
| gethostbyname | `sub_0093C3E0` 0x93C455; `sub_0093C740` 0x93C75D | NULL → keeps "127.0.0.1" / falls back to inet_addr |
| inet_ntoa | `sub_0093C3E0` 0x93C467; `sub_0093C990` 0x93C9FA | |
| inet_addr | `sub_0093C740` 0x93C771; `sub_0093C820` 0x93C85E; `sub_0093C8A0` 0x93C906 | 0x93C8FC pushes "255.255.255.255" (UDP broadcast) |
| socket | `sub_0093C550` 0x93C56B (2,1,6 TCP), 0x93C585 (2,2,17 UDP) | |
| ioctlsocket | `sub_0093C550` 0x93C5A5 | FIONBIO (0x8004667E) on UDP |
| setsockopt | `sub_0093C550` 0x93C5D6; `sub_0093C8A0` 0x93C8D4; `sub_009D2F70` 0x9D3073 | SOL_SOCKET/SO_BROADCAST; IPPROTO_TCP/TCP_NODELAY |
| closesocket | `sub_0093C530` 0x93C53F; `sub_0094AAA0` 0x94AAAF; `sub_00966E40` 0x966E4F; `sub_0096AF50` 0x96AF6D; `sub_009A9650` 0x9A966A | |
| bind | `sub_0093C630` 0x93C689 | INADDR_ANY; checks WSAEADDRINUSE (0x2740) |
| connect | `sub_0093C740` 0x93C7B0 | |
| WSAGetLastError | `sub_0093C630` 0x93C693; `sub_0093C740` 0x93C7BB | |
| htonl | `sub_0093C630` 0x93C665 | |
| htons | `sub_0093C630` 0x93C674; `sub_0093C740` 0x93C790; `sub_0093C820` 0x93C84E; `sub_0093C8A0` 0x93C8F6 | |
| ntohs | `sub_0093C990` 0x93CA1D | |
| send | `sub_0093CB10` 0x93CB40; `sub_00966740` 0x96676C; `sub_00966820` 0x966850; `sub_0096B020` 0x96B075; `sub_0098F1B0` 0x98F305; `sub_009A74B0` 0x9A75EF; `sub_009D2F70` 0x9D309D/0x9D30BD (through `edi`) | |
| recv | `sub_0093CA40` 0x93CA9B; `sub_00966780` 0x9667DB; `sub_009D2F70` 0x9D30DC | |
| sendto | `sub_0093C820` 0x93C885; `sub_0093C8A0` 0x93C92D | UDP; no caller of either wrapper found |
| recvfrom | `sub_0093C990` 0x93C9D6 | UDP; no caller of the wrapper found |
| listen | unlifted `0x93C6C0` 0x93C6DC | no reference to `0x93C6C0` in the binary |
| accept | unlifted `0x93C6F0` 0x93C71B | no reference to `0x93C6F0` in the binary |

## When each path runs

1. **Boot, always.** The start-up function `sub_00A3CA00` (`"NuInitHardware"`,
   `"Main"`) calls `0x93C3E0` at `0xA3D275` with the buffer `0x10D4C3F`. It
   writes "127.0.0.1" there, then WSAStartup, gethostname, gethostbyname,
   inet_ntoa (only if gethostbyname succeeds), WSACleanup. The caller ignores
   the return value (`0xA3D27A` reads an unrelated stack slot). With
   gethostbyname failing, the buffer stays "127.0.0.1". No socket is created.
2. **After a fatal error, once.** `NuError` and `NuSystemError` (`sub_00A2B690`,
   calls at `0xA2B9F6` and `0xA2BCE5`, strings `"** NuError: %s (%d) **"`,
   `"** NuSystemError: %s (%d) **"`) call `0x9C2150`, guarded by the one-shot
   flag `0x1042BB0`. If connect fails, `0x997830` returns at once
   (`0x99786A`); there is no retry.
3. **Only with `TMSERVER <host>` on the command line.** `sub_0098C6E0` scans
   the command line; `"TMSERVER"` sets `0x10D4C50 = 1` and the host pointer
   `0x1042B40` (`0x98C713`). `sub_009F3FA0` (called from boot at `0xA3D2B3`)
   builds the "Target Manager filesystem" device only when `0x10D4C50 != 0`
   (`0x9F4027`). `0x10D4C50` is in `.data` BSS, so it is 0 by default.
   **Hazard:** `0x9D2F70` retries connect forever with `Sleep(1000)`
   (`0x9D3042`–`0x9D3058`), so with networking disabled the game would hang
   if started with `TMSERVER`. Do not pass that argument.
4. **Never.** The UDP helpers (`0x93C820`, `0x93C8A0`, `0x93C990`) and the
   listen/accept helpers have no caller found by a scan of the binary for
   rel32 calls/jumps and absolute references.

## Not confirmed

- The scan finds direct calls and absolute references only. A target reached
  through a computed address (vtable built at run time) would be missed; the
  vtable at `0xD1B19C` (holds `0x98F1B0`) belongs to the TM filesystem.
- The `recvfrom`/`ntohs` argument counts are from the SDK and the wrapper's
  `ret 0x10`, not from a live caller.
