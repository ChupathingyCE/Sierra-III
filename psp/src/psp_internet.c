/* psp_internet.c - internet play's platform answers on the PSP (mpb1)

The multiplayer build compiles the engine's p2p layer as it is
(psp_build.py's INTERNET_UNITS): the public games' listings
(p2p_lobby.c, through MQTT brokers, p2p_signal.c) and the invite tunnel
(p2p.c). Sockets and names come from psp_net.c. This file answers the rest:

- random bytes: internet play makes its keys from posix_random_bytes (an
  X25519 key a run, nonces for every signalling message). The campaign
  build's answer (psp_platform.c) is a Mersenne Twister seeded from the
  32-bit clock, fine for gameplay and no good for keys: anyone who saw the
  public key could try every seed. Here the bytes are ChaCha20's keystream
  under a key hashed (BLAKE2b, Monocypher's, which the p2p layer already
  links) from this PSP's OpenPSID, its Wi-Fi MAC address, the clock since
  boot and the real-time clock, both in microseconds; each call hashes the
  clock in again. The OpenPSID is never sent, so someone who sees the
  traffic can't rebuild the key from what they see. Still not a hardware
  generator [U]: KIRK's (the security chip's) would be, but it is reached
  only from kernel mode
- Discord: the desktop shows a hosted game in Discord; the PSP has none
- whether a file is on the Memory Stick, for the ONLINE GAMES screen's map
  check (psp_online.c: a game unit, whose stdio goes through the engine's
  file layer, which found none of the maps)
- the tunnel thread's priority (psp_internet_thread_priority) */

#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <pspopenpsid.h>
#include <psprtc.h>
#include <pspwlan.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "monocypher.h"

/* posix.h's types on a 32-bit system (port/linux/src/posix.h) */
typedef unsigned long posix_ulong;

/* ---------- random bytes */

static struct
{
	pthread_mutex_t lock;
	int seeded;
	unsigned char key[32];
	unsigned long long counter;
} random_state = { PTHREAD_MUTEX_INITIALIZER, 0, { 0 }, 0 };

/* the key, hashed from what this PSP alone knows and the clocks (the
previous key in it too once seeded, so each call moves it on) */
static void random_mix(void)
{
	struct
	{
		unsigned char previous[32];
		PspOpenPSID psid;
		unsigned char mac[8];
		SceInt64 since_boot;
		u64 real_time;
		unsigned int stack_address;
	} pool;

	memset(&pool, 0, sizeof(pool));
	memcpy(pool.previous, random_state.key, sizeof(pool.previous));
	if (!random_state.seeded)
	{
		sceOpenPSIDGetOpenPSID(&pool.psid);
		sceWlanGetEtherAddr(pool.mac);
	}
	pool.since_boot = sceKernelGetSystemTimeWide();
	sceRtcGetCurrentTick(&pool.real_time);
	pool.stack_address = (unsigned int)(unsigned long)&pool;
	crypto_blake2b(random_state.key, sizeof(random_state.key), (const unsigned char *)&pool, sizeof(pool));
	crypto_wipe(&pool, sizeof(pool));
	random_state.seeded = 1;
}

void posix_random_bytes(void *buffer, posix_ulong size)
{
	static const unsigned char nonce[8] = { 'S', 'i', 'e', 'r', 'r', 'a', '-', '3' };

	pthread_mutex_lock(&random_state.lock);
	random_mix();
	memset(buffer, 0, size);
	random_state.counter = crypto_chacha20_djb(buffer, buffer, size, random_state.key, nonce, random_state.counter);
	pthread_mutex_unlock(&random_state.lock);
}

/* ---------- no Discord (p2p_discord.c) */

void p2p_discord_update(void)
{
}

void p2p_discord_user(char *id, int id_size, char *name, int name_size)
{
	if (id && id_size > 0)
		id[0] = 0;
	if (name && name_size > 0)
		name[0] = 0;
}

void p2p_discord_set_hosting(const char *secret, int player_count, int maximum_player_count)
{
	(void)secret; (void)player_count; (void)maximum_player_count;
}

/* (the desktop keeps a Discord name's allowed characters; with no Discord,
the text as given, cut to fit) */
void p2p_discord_sanitize(char *destination, int size, const char *source, int name)
{
	(void)name;
	if (destination && size > 0)
		snprintf(destination, (size_t)size, "%s", source ? source : "");
}

/* ---------- the tunnel's thread

p2p.c does all of internet play's network work on one thread: the tunnel,
its KCP streams, the signalling and the lobby. It starts at 0x5c, far below
the game thread's 0x20, and the PSP runs a thread only while every higher
one waits.
- In a game the PSP can't keep up with, the game thread runs one to two and
  a half seconds of catch-up ticks without waiting, and the tunnel can go
  that long without running: the host's messages stopped within seconds of
  the join, and the client gave up (docs/milestone_first_public_game.md). So
  in a game the thread runs at 0x1f: above the game thread, below the
  vertical blank's 0x1e. It waits in select() between packets and KCP's
  clock, so there it takes only what it needs.
- In the menus it stays at 0x5c. While ONLINE GAMES browses, the thread
  checks thousands of signed listings; above the game thread it kept the
  menus' start-up thread (saved profiles and playlists, slow on the stick)
  from ever finishing, and the menus never ran the screen (sierra-hw
  38939812478, its A/B with tunnelslow).
"tunnelslow" keeps it at 0x5c in games too, for comparing */
#define PSP_TUNNEL_PRIORITY 0x1f
int psp_keep(const char *name);
void platform_log(const char *format, ...);

static SceUID tunnel_thread = -1;
static int tunnel_priority = -1;

/* the thread, as it starts (p2p.c's #priority fixup) */
void psp_internet_thread_priority(void)
{
	SceKernelThreadInfo info;

	memset(&info, 0, sizeof(info));
	info.size = sizeof(info);
	sceKernelReferThreadStatus(0, &info);
	tunnel_thread = sceKernelGetThreadId();
	tunnel_priority = info.currentPriority;
	platform_log("internet play's thread (%s): priority 0x%x; 0x%x in a game%s", info.name, tunnel_priority,
		PSP_TUNNEL_PRIORITY, psp_keep("tunnelslow") ? " (not: tunnelslow)" : "");
}

/* the thread's priority now, for the ONLINE GAMES test's line; -1 before it starts */
int psp_internet_thread_priority_now(void)
{
	SceKernelThreadInfo info;

	if (tunnel_thread < 0)
		return -1;
	memset(&info, 0, sizeof(info));
	info.size = sizeof(info);
	return sceKernelReferThreadStatus(tunnel_thread, &info) < 0 ? -1 : info.currentPriority;
}

/* in a game (1: the screen joined one) or browsing the menus (0) */
void psp_internet_tunnel_priority(int in_game)
{
	int priority = in_game && !psp_keep("tunnelslow") ? PSP_TUNNEL_PRIORITY : tunnel_priority;

	if (tunnel_thread < 0 || priority < 0)
		return;
	if (sceKernelChangeThreadPriority(tunnel_thread, priority) < 0)
		platform_log("internet play's thread: the change to 0x%x was refused", priority);
}

/* ---------- the Memory Stick */

int psp_file_exists(const char *path)
{
	SceIoStat stat;

	return sceIoGetstat(path, &stat) >= 0 && FIO_S_ISREG(stat.st_mode);
}
