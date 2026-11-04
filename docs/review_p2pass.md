# Review: P2NC, tick pass P2 (keep word `p2pass`, off by default)

Labels: [M] measured on a PSP, [E] emulator or estimate, [D] read in code or data, [U] inference.

## What it does

Inside the object walk (`objects_update`'s two header loops), a non-collideable object's partition calls, `cluster_partition_disconnect` and `cluster_partition_reconnect`, are queued and made at the walk's end, in their order, before the garbage collector. Everything else a move does stays where it was: the location lookup (PWSL), the wake, the epoch, the ME guards, the activation.

Why: c20's walk moves ~31 non-collideable machines a tick, interleaved with the other objects' updates. The partition code (`cluster_partition_reconnect` is 6.3 KB with SPHSL and RECONLIST in line) is fetched again at each move and evicts the walk's own code. Queued, it runs once a tick with its lines warm.

## Why it is exact

- **The non-collideable partition has pools of its own** (`cluster_partition_new` makes each partition's two reference lists, cluster_partitions.c). The queue makes the same calls with the same arguments in the same order, so the lists, their order and the pools end as before [D].
- **Each queued reconnect keeps what it reads:** the sphere's centre and radius and the location, copied when it is queued.
- **Nothing in the walk reads the partition with a move outstanding.** `cluster_get_first_noncollideable_object` (every list read: area damage, a player's nearby objects, drawing, AI perception) makes the queue first, and so does `object_get_first_cluster` for a non-collideable object. `noncollideable_object_cluster_partition` has no other user [D].
- **An object moved again** (a disconnect while its reconnect is queued, which a deletion also does) makes the queue first, so a reconnect always lands on a live object before its next disconnect. A deferred disconnect keeps the chain in the queue, not in the object, so a deleted object's disconnect needs nothing from its datum.
- The flush takes the same ME guards a move in place takes (`psp_aime_handback`, `psp_merays_settle`).

## Key lines (psp_fixups.py, `FIXES_P2PASS`)

- `psp_p2q_disconnect` / `psp_p2q_reconnect`: queue the call when the walk is open; a reconnect right after its own disconnect shares the entry. The disconnect clears the object's pointer, as the disconnect in place leaves it.
- `psp_p2q_flush(why)`: makes the queue in order. `why` counts the causes in the `p2pass:` log line (walk end, a list read, an object moved again, full, an object's own clusters read).
- `psp_p2q_begin` beside `psp_fvp_begin`, `psp_p2q_end` before `psp_nms_close`: the window is the walk.
- The queue is 64 entries (~2.4 KB of `.bss`); a full queue is made at once.

## Proof

- **Exact [E]:** `p2pass` against the default, one build: play and both seeds identical at every tick on b30's route (6,000), c20 hands off (6,000), a30's route (7,050), d20's swamp (3,000) and c10 hands off (3,600). On c20 the whole state is identical at every tick; elsewhere it differs only in the parts two plain runs also differ in (looping sounds, the objects' drawing writes, the HUD's unit interface, actor).
- **Flush causes [E]:** c20 180,028 moves made after their place: queues made at the walk's end 5,608, by a list read 0, an object moved again 78, full 4. a30 20,008: walk end 6,670, list reads 5, moved again 742.
- **The cache model's price [E]:** c20 hands off to game time 2,100: the tick phase 12,323 M to 11,904 M cycles (-3.4%, ~0.6 model ms a tick); I-misses -2,717 a tick (`cluster_partition_reconnect` -1,007, the node matrices -100, memcpy -91), D-misses -375 a tick. Cache work is priced on the PSP, not in the model: the PSP pair decides.
- **Where it helps:** the census (`PSP_P2CENSUS=1`, keep word `p2census`) found c20's non-collideable moves are its machines (97%), all to the same clusters. b30 and a30 move 2-3 non-collideable objects a tick (projectiles), so expect ~0 there.

## Risks

- Its `.bss` shifts the data after it by ~2.4 KB (SPHSL's 24 KB moved the drawing +2-3% on the PSP): the PSP pair shows whether this one does.
- New functions have no PGO counts: a strict profile build refuses to link until the tock's profile is counted again.
