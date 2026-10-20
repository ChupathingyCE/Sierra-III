# Review: perf-cache (the CPU caches: prefetch, data layout, loop phasing)

Branch `perf-cache`, off w140 (w139's code and profile). Two exact changes, each in `FIXES_CACHE` (`psp_fixups.py`), each on by default with an off word. The measurements behind them are in `cache_work.md`. Labels: [E] emulator or cache model, [D] read in code, [U] inference. [M] measured on a PSP. **Scored in the cache model: `typehdr` wins a little and stays on; `nodeprefetch` loses, so its default should flip to off (an on word) unless the hardware A/B says otherwise.**

## 1. `nodeprefetch`: the node walk's lines asked for one node ahead

**Why.** In the cache model the node walk holds the tick's top data-miss sites on b30, c20 and d20: each node's matrix is rewritten every tick into a block the rest of the tick has pushed out of the 16 KB data cache, so `matrix4x3_rotation_from_quaternion`'s first stores wait a whole miss for each of the matrix's lines (52 bytes: one or two 64-byte lines), and `object_compute_node_matrices`' read of the node's links in the model tag misses too. 917 misses a tick on b30, 683 on c20, 430 on d20 [E].

**Key lines** (`objects.c`, `object_compute_node_matrices`):

```c
static __inline__ void psp_node_lines_prefetch(real_matrix4x3 *object_nodes, struct model *model, short node_index)
{
	PSP_LINE_PREFETCH(&object_nodes[node_index]);
	PSP_LINE_PREFETCH(&object_nodes[node_index].position.z);
	if (model->nodes.address)
		PSP_LINE_PREFETCH(&((struct model_node *)model->nodes.address)[node_index].next_sibling_node_index);
}
...
	if (psp_node_prefetch)
	{
		while (psp_node_asked<node_count-1 && psp_node_asked<node_index+1)
			psp_node_lines_prefetch(object_nodes, model, node_stack[++psp_node_asked]);
	}
```

The walk is a queue: each node done appends its sibling and first child. At the end of each node, every queued node up to the one after next that hasn't been asked for yet gets its three lines asked for (`cache 0x1E`, which never stalls); the root's are asked for before the walk. So each node is asked for once, about one node's compute ahead, more than the ~72 cycles a line takes to land. The queue's entries were range-checked when they were pushed; a tag block without data is skipped, so no prefetch names an address outside memory.

**Evidence** [E]. The cache model with timing, `statecrc` on both sides, c20 and d20 to 90 s (2,700 ticks) and b30's route to 85 s. **Exact:** play and both seeds are identical at every tick on c20 and d20; b30 is identical to tick 1,325 and then drifts, as it does on any change. **Cost, not gain:** the tick is 0.090 ms slower on c20 and 0.054 ms on d20 (b30 +0.26, indicative). The walk's two sites do nearly vanish from the miss list (c20: 707 → 352 and 413 → 0 misses a tick; data misses −734 a tick; the D-miss stall falls 0.16 ms), but the demand loads then wait ~33 of a fill's ~72 cycles (+0.15 ms), because the prefetch is only about one node's compute (~40 cycles) ahead, and each prefetch costs ~12 cycles of its own work. The tick below includes the state hash, the same on every side (`cache_work.md` section 2).

**Verdict.** A loser in the model. Flip the default to off (an on word) or drop it; the hardware A/B (below) can overturn this, since it rests on the model's fill timing. Asking two nodes ahead would give the fill time, at the price of more lines held [U]; not built.

**Cost.** Three cache ops and a compare a node; nothing else changes. Exact: a prefetch changes no value.

**Off word:** `nonodeprefetch`.

## 2. `typehdr`: the object lookup's type check reads the header's type

**Why.** `object_get_and_verify_type` (every `object_get`) checks the object's type against what the caller expects. It read the type from the object's own data, a line the caller often never touches otherwise: on b30 that one load missed ~1,200 times a tick (18% of its runs), the tick's most-missed load, with no window for a prefetch because its address comes from the load just before it [E].

**Key lines** (`objects.c`):

```c
	struct object_header_datum *psp_header = object_header_get(object_index);
	struct object_datum *result = psp_header->datum;
	const int psp_type = PSP_KEEP_ONCE("notypehdr") ? result->object.type : psp_header->type;
	...
		TEST_FLAG(valid_type_flags, psp_type),
```

The header, whose line the lookup has just read for the data pointer, holds the same type: `object_new` writes both from the definition, and nothing writes either again [D]. So the check passes and fails exactly as before.

**Evidence** [E]. The same runs. **Exact:** play and both seeds identical at every tick of c20 and d20 (b30 as above). The tick is **0.032 ms faster on c20 and 0.044 ms on d20** (b30 0.108, indicative), about 0.2% of a tick without the state hash [U]. The lookup's misses fall by half or more (c20 775 → 400 a tick, d20 836 → 326, b30 2,410 → 998), but 71%, 60% and 46% of the removed misses reappear in the callers, so the tick loses only 107, 205 and 765 data misses: below the ≤0.26 ms the site's count allowed.

**Cost.** The keep check is a byte load and a branch on each call, on both sides of the A/B (~0.04 ms a tick on b30 [U]).

**Off word:** `notypehdr`.

## 3. Measured, not changed

- **Prefetch in the code as it is** is worth 0.19–0.38 ms a tick at best (a `cache 0x1E` at every missing load, each at its best window), 4–5% of the data stall; moving up to three memory operations out of each window would reach 1.2–2.5 ms [E]. So only reshaped loops (next-item work) open real windows.
- **Loop phasing** (P1.1's data side, the first footprints, c20's opening): `biped_update`'s single call runs more code than the 16 KB cache holds (299 lines of 256), and the calls of `unit_update` (384 lines a tick) and `object_update` (1,463) run more still, because each kind of object takes its own path. So phasing those can't keep their code resident; only `weapon_update` and `actor_look_update` fit, at few calls a tick [E]. The phasing score failed its own check (c10's node-matrix pass scores an 11.5% gain; the real batch lost 34%), so no phasing candidate is scored (`cache_work.md`, section 1).
- **The node block made with `cache 0x18`** instead of prefetched (no read at all) is not built: a line can hold parts of two nodes, and the walk's order isn't the nodes' order, so a made line could wipe a node already written this tick.

## Tests

- The cache-model batch (both off words, b30/c20/d20, `statecrc`): done, above. The state hash's whole-state differences (contrail point, lights, object looping sounds, objects) are what drawing writes and sit on both sides of every pair.
- The multiplayer build (`PSP_MULTIPLAYER=1`) compiles (a full build on a build runner succeeded).
- **The PSP-3000 A/B [M]** (b30's route to `exitat300`, `- | nonodeprefetch notypehdr`, two runs a side, interleaved; all four ran to the end, game time ~8,990, no crash). The off side switches both levers off, so this reads the two together; gaps below are off minus on, so a positive gap means the levers help. **The two levers together are worth nothing the PSP can see, as the model predicted** (it put `typehdr` at +0.03–0.1 ms and `nodeprefetch` at −0.05–0.09 ms a tick, a net near zero against a spread of ±0.3 ms a tick). Where play is identical on both sides (game time 300–1,300): tick 45.6 ms both, −0.1 ms; frame 192.2 against 192.7 ms, +0.2%. Cinematic (150–570): tick +0.3 ms (+0.7%), frame +0.1 ms. Beach (2,700–6,000): tick −0.2 ms, frame −0.4 ms. Bunker (6,000–8,100): tick −0.1 ms, frame +0.6 ms. All in the noise. Flight (300–2,000): tick +0.5 ms (+1.0%), frame +2.1 ms (+1.0%), in the noise. **Landing (2,000–2,700) is the one stretch the tool calls real**: frame +15.4 ms (+7.5%), drawing +11.3 ms, tick +1.7 ms (+4.0%), in favour of the levers on. It isn't read as the levers' doing: drawing is 66% of that gap and these levers touch only the tick; b30's play drifts after tick 1,325 on any change, and in this stretch the descent seems to sit at different poses at the same game time [U] (the two runs of one side differ by 5.7 ms in the frame, and in 100-tick slices at game time 2,100–2,200 one run is 14–19 ms a frame faster than the other on the same side); and the stretches around it show nothing beyond the noise. If it is real it is `typehdr` (the model says `nodeprefetch` loses), and two runs a side can't say. Drawing is 0.3–0.9 ms (0.6–1.7%) slower with the levers off in four of five stretches, flagged real in flight and bunker; the type check is also read in drawing's callers, so a small gain there from `typehdr` is possible [U]. **Recommendation:** `nodeprefetch` default off (an on word; the model says it loses, and the PSP shows no gain from the pair that it could be hiding behind `typehdr`), `typehdr` stays on. A split of the sides (one lever each) would need more repeats than two to resolve effects of 0.1 ms in a 30–50 ms tick; it is not worth hardware time unless a lever grows. Not done in this branch (docs only).
