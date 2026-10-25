/* Closed-loop input for unattended runs (runtime/src/hp_nav.c). Off by
 * default. Player 1 is the GameObject at guest *(u32 *)0xF71DC0 (player 2 at
 * 0xF71DC4); its position is three floats at +0x13F0 (x, y, z). Evidence:
 * the level-script functions PlayerXPos/PlayerYPos/PlayerZPos (name table
 * 0xE0B250..0xE0B268) are 0x450680/0x4506A0/0x4506C0, which load
 * [0xF71DC0]+0x13F0/+0x13F4/+0x13F8; Player2Active (0x450DA0) tests
 * [0xF71DC4].
 *
 * HPRECOMP_POS_LOG=1 logs both players and the character player 1 last
 * switched away from (K) once per second, and each change of the player 1
 * object (a character switch).
 *
 * HPRECOMP_NAV_SCRIPT=<file> runs steps in order; ms count from the first
 * keyboard GetDeviceState. One step per line, # starts a comment:
 *   start <ms>               wait until that time
 *   ready                    wait until player 1's x or z is non-zero
 *                            (gameplay; cutscenes leave it at 0, 0)
 *   goto <x> <z> <r> <ms> [ymin]
 *                            hold W/A/S/D toward (x, z) until within r (or
 *                            y rises above ymin), or give up after ms;
 *                            recalibrates the camera when the last
 *                            calibration is over 5 s old, unless basis is set
 *   approach <r> <ms>        goto the character last switched away from
 *   basis <wx> <wz> <dx> <dz> fix the world (x, z) directions of W and D and
 *                            stop calibrating
 *   hold <KEY> <ms>          hold a key (input_script key names)
 *   tap <KEY>                hold a key 100 ms
 *   wait <ms>                press nothing
 * Calibration holds W, then D, 350 ms each and solves the world (x, z)
 * direction of each key, so steering follows the camera. */
#ifndef HP_NAV_H
#define HP_NAV_H

#include <stddef.h>
#include <stdint.h>

/* Parses a nav script into the active plan. Returns 0, or -1 with
 * "line N: why" in err. */
int hp_nav_parse(const char *text, char *err, size_t err_size);

/* Per keyboard poll: logs and ORs 0x80 into keys[dik] for nav keys held. */
void hp_nav_keys(uint8_t keys[256]);

#endif
