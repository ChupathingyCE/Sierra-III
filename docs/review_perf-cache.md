# Review: perf-cache (the CPU caches: prefetch, data layout, loop phasing)

Branch `perf-cache`, off w140 (w139's code and profile). Two exact changes, each in `FIXES_CACHE` (`psp_fixups.py`), each on by default with an off word. The measurements behind them are in `cache_work.md`. Labels: [E] emulator or cache model, [D] read in code, [U] inference. Nothing here has run on a PSP yet.

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

**Evidence.** Compiled. Scoring in flight: the cache model on b30, c20 and d20 to 90 s, on against `nonodeprefetch`, with `statecrc` on both sides. Expected: most of the walk's 0.09–0.20 ms a tick [E].

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

**Evidence.** Compiled. Scoring in flight in the same batch, on against `notypehdr`, `statecrc` on both sides. At most ~0.26 ms a tick on b30 [E], less wherever the caller goes on to read the type's line anyway.

**Cost.** The keep check is a byte load and a branch on each call, on both sides of the A/B (~0.04 ms a tick on b30 [U]).

**Off word:** `notypehdr`.

## 3. Measured, not changed

- **Prefetch in the code as it is** is worth 0.19–0.38 ms a tick at best (a `cache 0x1E` at every missing load, each at its best window), 4–5% of the data stall; moving up to three memory operations out of each window would reach 1.2–2.5 ms [E]. So only reshaped loops (next-item work) open real windows.
- **Loop phasing** (P1.1's data side, the first footprints, c20's opening): `biped_update`'s single call runs more code than the 16 KB cache holds (299 lines of 256), and the calls of `unit_update` (384 lines a tick) and `object_update` (1,463) run more still, because each kind of object takes its own path. So phasing those can't keep their code resident; only `weapon_update` and `actor_look_update` fit, at few calls a tick [E]. The phasing score failed its own check (c10's node-matrix pass scores an 11.5% gain; the real batch lost 34%), so no phasing candidate is scored (`cache_work.md`, section 1).
- **The node block made with `cache 0x18`** instead of prefetched (no read at all) is not built: a line can hold parts of two nodes, and the walk's order isn't the nodes' order, so a made line could wipe a node already written this tick.

## Tests

- The cache-model batch above (both off words, b30/c20/d20, `statecrc`).
- The multiplayer build (`PSP_MULTIPLAYER=1`) compiles: in flight.
- If either wins in the model: a PSP-3000 keepsets A/B (`- | nonodeprefetch notypehdr`, repeat 2) on b30's route.
