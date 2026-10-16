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
its KCP streams, the signalling and the lobby. pspdev's pthreads start at
0x3c, below the game thread's 0x20, and the PSP runs a thread only while
every higher one waits. In a game the PSP can't keep up with, the game
thread runs one to two and a half seconds of catch-up ticks without waiting,
and the tunnel can go that long without running: in a 12-player public
game the host's messages stopped within seconds of the join, and the
client gave up (docs/milestone_first_public_game.md). 0x1f: above the game
thread and below the vertical blank's 0x1e. The thread waits in select()
between packets and KCP's 10 ms clock, so it takes only what it needs.
"tunnelslow" leaves it at pspdev's default, for comparing */
#define PSP_TUNNEL_PRIORITY 0x1f
int psp_keep(const char *name);
void platform_log(const char *format, ...);

void psp_internet_thread_priority(void)
{
	SceKernelThreadInfo info;

	memset(&info, 0, sizeof(info));
	info.size = sizeof(info);
	sceKernelReferThreadStatus(0, &info);
	if (psp_keep("tunnelslow"))
	{
		platform_log("internet play's thread: priority 0x%x left as it is (tunnelslow)", info.currentPriority);
		return;
	}
	if (sceKernelChangeThreadPriority(0, PSP_TUNNEL_PRIORITY) < 0)
		platform_log("internet play's thread: priority 0x%x; the change to 0x%x was refused", info.currentPriority, PSP_TUNNEL_PRIORITY);
	else
		platform_log("internet play's thread (%s): priority 0x%x, now 0x%x (above the game thread's: tunnelslow undoes it)",
			info.name, info.currentPriority, PSP_TUNNEL_PRIORITY);
}

/* ---------- the Memory Stick */

int psp_file_exists(const char *path)
{
	SceIoStat stat;

	return sceIoGetstat(path, &stat) >= 0 && FIO_S_ISREG(stat.st_mode);
}
