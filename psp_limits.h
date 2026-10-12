/*
PSP_LIMITS.H

The PSP's copy of the engine's halo_port_limits.h (psp_build.py
force-includes it, and it takes that header's include guard, so the
engine's own is never read): the session limits and the netcode's layout,
for either engine base. PSP_CHUPATHINGYCE (psp_build.py) marks the
ChupathingyCE base, whose game settings record and netcode version differ.

The Xbox game allows 16 players on at most 4 machines (up to 4 players each
on split screen). The native builds allow 128 players on up to 128
machines; split screen stays at 4 players per machine.

128 is the largest session that fits the game's existing records: player,
machine and team indices are stored in signed chars (0..127 with NONE), and
a finishing place in 7 bits.
*/

#ifndef __HALO_PORT_LIMITS_H
#define __HALO_PORT_LIMITS_H

/* a PSP value, or the other ports' (psp_fixups.py) */
#ifdef HALO_PSP
#define HALO_PSP_OR(psp, other) (psp)
#else
#define HALO_PSP_OR(psp, other) (other)
#endif
/* a 32-bit value at an address that may not be a multiple of 4. The PSP's
CPU faults on unaligned word loads and stores (x86 doesn't care, and
PPSSPP's JIT forgives them; its interpreter reports them); a packed
struct makes GCC use the unaligned-safe lwl/lwr and swl/swr pairs
(psp_fixups.py) */
#ifdef HALO_PSP
struct psp_unaligned_ulong { unsigned long value; } __attribute__((packed));
#define PSP_UNALIGNED_ULONG(address) (((struct psp_unaligned_ulong *)(address))->value)
#else
#define PSP_UNALIGNED_ULONG(address) (*(unsigned long *)(address))
#endif
/* build 116: the same for a float, and for three in a row (a point or a
vector) copied to an aligned place. Bungie's compressed animations can hold
their float tables at an address 2 past a multiple of 4: b115 faulted on a10's
opening in animation_get_keyframe_translation (psp_fixups.py, FIXES_116) */
#ifdef HALO_PSP
struct psp_unaligned_real { float value; } __attribute__((packed));
#define PSP_UNALIGNED_REAL(address) (((const struct psp_unaligned_real *)(address))->value)
#else
#define PSP_UNALIGNED_REAL(address) (*(const float *)(address))
#endif
/* build 116: a see-through shader's fade, packed above its blend function in
the short the queue already carries (psp_fixups.py psp_transparent_map,
psp_ge.c): bits 0-3 the blend function, 4-5 the framebuffer fade mode (none,
fade when perpendicular, fade when parallel), 6 "don't fade with active
camouflage", 7-14 how much the fade source takes away (0: none, 255: all).
A plain blend function (nothing above bit 3) is full strength, no fade */
#define PSP_SEETHROUGH_PACK(blend, mode, fade, keep_with_camouflage) \
	((short)(((blend) & 15) | (((mode) & 3) << 4) | ((keep_with_camouflage) ? 64 : 0) | \
	((255 - (int)((((fade) < 0.0f ? 0.0f : (fade) > 1.0f ? 1.0f : (fade)) * 255.0f) + 0.5f)) << 7)))
#define PSP_SEETHROUGH_BLEND(packed) ((packed) & 15)
#define PSP_SEETHROUGH_MODE(packed) (((packed) >> 4) & 3)
#define PSP_SEETHROUGH_KEEP_WITH_CAMOUFLAGE(packed) (((packed) & 64) != 0)
#define PSP_SEETHROUGH_FADE(packed) ((float)(255 - (((packed) >> 7) & 255)) / 255.0f)
#define PSP_UNALIGNED_REAL3(to, from) do { \
		const char *psp_from_ = (const char *)(from); \
		float *psp_to_ = (float *)(to); \
		psp_to_[0] = PSP_UNALIGNED_REAL(psp_from_); \
		psp_to_[1] = PSP_UNALIGNED_REAL(psp_from_ + 4); \
		psp_to_[2] = PSP_UNALIGNED_REAL(psp_from_ + 8); \
	} while (0)
/* the AI debug records (ai_debug.c) are 6.6 MB for 256 actors and 3.7 MB of
path storage; gameplay only writes them, so on the PSP every actor shares
one record (psp_fixups.py) */
#ifdef HALO_PSP
#define PSP_DEBUG_INDEX(index) (0 * (index))
#define PSP_DEBUG_COUNT(count) 1
#else
#define PSP_DEBUG_INDEX(index) (index)
#define PSP_DEBUG_COUNT(count) (count)
#endif

/* ---------- session limits */

#define HALO_PORT_MAXIMUM_NETWORK_PLAYERS 16
#define HALO_PORT_MAXIMUM_NETWORK_MACHINES 4

/* a host polls its listening socket and one socket per machine; the Xbox's
Winsock headers default to 64 (the prefix headers define FD_SETSIZE from
this before any of them is read) */
#define HALO_PORT_FD_SETSIZE 256

/* ---------- memory capacity for these limits (game state and pools) */

/* capacity: Xbox values (psp_limits.h) */
#include "psp_capacity.h"

/* ---------- struct network_game layout

The game settings record (struct network_game) is declared separately in
several networking and interface units; its layout follows from the limits.
Every copy checks its offsets against these values. The Xbox values (4
machines, 16 players) are 0x226 and 0x434. */

#define HALO_PORT_NETWORK_MACHINE_SIZE 0x44
#define HALO_PORT_NETWORK_PLAYER_SIZE 0x20
#define HALO_PORT_NETWORK_GAME_MACHINES_OFFSET 0x114
#define HALO_PORT_NETWORK_GAME_PLAYER_COUNT_OFFSET \
	(HALO_PORT_NETWORK_GAME_MACHINES_OFFSET + HALO_PORT_MAXIMUM_NETWORK_MACHINES * HALO_PORT_NETWORK_MACHINE_SIZE)
#define HALO_PORT_NETWORK_GAME_PLAYERS_OFFSET (HALO_PORT_NETWORK_GAME_PLAYER_COUNT_OFFSET + 2)
#define HALO_PORT_NETWORK_GAME_PLAYERS_END \
	(HALO_PORT_NETWORK_GAME_PLAYERS_OFFSET + HALO_PORT_MAXIMUM_NETWORK_PLAYERS * HALO_PORT_NETWORK_PLAYER_SIZE)
#define HALO_PORT_NETWORK_GAME_RANDOM_SEED_OFFSET (HALO_PORT_NETWORK_GAME_PLAYERS_END + 2)
#ifdef PSP_CHUPATHINGYCE
/* (ChupathingyCE: then the gametype's PC options, struct game_variant_options:
0x1C bytes) */
#define HALO_PORT_NETWORK_GAME_VARIANT_OPTIONS_OFFSET (HALO_PORT_NETWORK_GAME_PLAYERS_END + 0xA)
#define HALO_PORT_NETWORK_GAME_LOCAL_DATA_OFFSET (HALO_PORT_NETWORK_GAME_PLAYERS_END + 0x26)
#define HALO_PORT_NETWORK_GAME_SIZE (HALO_PORT_NETWORK_GAME_PLAYERS_END + 0x2A)
#else
#define HALO_PORT_NETWORK_GAME_LOCAL_DATA_OFFSET (HALO_PORT_NETWORK_GAME_PLAYERS_END + 0xA)
#define HALO_PORT_NETWORK_GAME_SIZE (HALO_PORT_NETWORK_GAME_PLAYERS_END + 0xE)
#endif

/* ---------- system link protocol

The native builds' messages differ from the Xbox game's (longer arrays, the
game settings record in fragments), so they search for games with their own
protocol version and never see the Xbox game's, or it theirs. */

#define HALO_PORT_NETWORK_GAME_MESSAGE_VERSION 2

/* The native builds' network code has a version of its own (an unsigned
16-bit number): machines of different versions cannot play together, and a
client does not join a host of another version, but tells the player which
is newer (network_client_manager.c). A host advertises it, with its netcode,
in its game's advertisement's reserved bytes (network_server_message_handler.c),
which hosts built before there was a version send as zeros: version 0.
Raise it with any change to what the machines send each other. */
#ifdef PSP_CHUPATHINGYCE
#define HALO_PORT_NETWORK_VERSION 24
/* ... the versions whose hosts a client joins (ChupathingyCE: delta.h's
table, DELTA_LEGACY_VERSIONS); code asks the functions, not the numbers */
#define HALO_PORT_NETWORK_VERSION_MINIMUM 11
#define HALO_PORT_NETWORK_VERSION_MAXIMUM 24
int delta_legacy_announce(void);
int delta_legacy_minimum(void);
int delta_legacy_maximum(void);
#else
#define HALO_PORT_NETWORK_VERSION 9
#endif
/* ... the advertisement's reserved bytes: the version (a little-endian word),
then flags */
#define HALO_PORT_ADVERTISED_VERSION_OFFSET 0
#define HALO_PORT_ADVERTISED_FLAGS_OFFSET 2
/* ... the host plays the distributed netcode (always, since the lockstep
netcode was removed; hosts of version 4 built before then may not) */
#define HALO_PORT_ADVERTISED_DISTRIBUTED_FLAG 0x01
/* ... the game is under way (loading, playing or over), not in its lobby
(ChupathingyCE) */
#define HALO_PORT_ADVERTISED_IN_PROGRESS_FLAG 0x02

/* a message header's 12-bit length allows messages of up to 0xFFF bytes,
header included; the per-tick update of 128 players is 3,857 */
#define HALO_PORT_MAXIMUM_NETWORK_MESSAGE_SIZE 0x1000

/* the packet codec's limit on a decoded or encoded packet; the per-tick
update of 128 players decodes to 0x1010 bytes */
#define HALO_PORT_NETWORK_PACKET_SIZE 0x1100

/* the game settings record (HALO_PORT_NETWORK_GAME_SIZE, 13,092 bytes at 128
machines and players) does not fit one message; it is sent in pieces of
this many bytes (4 pieces), each 3,594 bytes on the wire */
#define HALO_PORT_NETWORK_GAME_SETTINGS_FRAGMENT_SIZE 0xE00

#endif /* __HALO_PORT_LIMITS_H */
