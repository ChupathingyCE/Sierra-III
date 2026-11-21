/* Three counts that size bakes of level-only answers (PSP_RECON builds only;
the lab's new_ground_bake brief, its Ruling). Each is a keep word, off by
default, and reads only what the frame already worked out, so play and the
pictures are the same with it on. psp_fixups.py's FIXES_BAKECOUNTS puts the
hooks in the engine's text; all of them run on the main CPU.

"pvscount": how much an eye-region potentially visible set would take from
  the portal walk. The engine already gates each portal on the map's
  per-cluster PVS, so the walk only enters clusters that pass it, and a
  cluster it enters is drawn. What a finer set (keyed by the eye's cluster
  and a cell of the eye's position, "pvscountdir" adding the view's octant)
  could refuse is the portal hulls the walk works out for neighbours it then
  doesn't enter. The set is learned from the run itself: each cell's union
  of the clusters its views drew. That is a lower bound on a baked set (only
  the eyes the run took), so the refusals it counts are an upper bound on a
  real bake's; the views a cell are printed so the reader can judge how well
  sampled it is. The ME's copy of the walk leaves the hooks out (mevis's
  copies map them to nothing): run with "mevislandoff" so every view is
  walked here. "pvscellN": the cell in world units (2); "pvscountkbN": the
  cells' pool (64 KB).

"shadowcount": the blob shadows' casters by type, and how many cast with
  the same volume (the shadow matrix and radius bit for bit) as their last
  cast, against structure_render_shadow's calls and the receiver surfaces
  each finds. The BSP doesn't move, so the same volume finds the same
  surfaces: SHQ's exact memo is the "as its last cast" share, and a bake at
  load the run's never-moved casters.

"lightcount": the dynamic lights' passes and their light draws (the GE's
  level triangles a light reaches, replayed on the ME) by whether the light's
  sphere and clusters are as on its last pass, and by what carries the
  light. A lit-surface list kept for a still light is exact (lightplan's
  key); the run's never-moved lights are what a bake at load could hold.

Each logs a line every 300 of its own frames, the window's counts, then the
run so far from its tables. */

#ifdef PSP_RECON

#include <math.h>
#include <stdio.h>
#include <string.h>

int psp_keep(const char *name);
int psp_keep_number(const char *prefix, int fallback);
void *psp_recon_table(const char *census, unsigned long bytes);
void platform_psp_log(const char *format, ...);
extern long game_time_get(void);

#define BAKE_WINDOW 300

static unsigned long bake_hash(const unsigned long *words, long count, unsigned long hash)
{
	while (count-- > 0)
		hash = (hash ^ *words++) * 16777619u;
	return hash;
}

static unsigned long bake_float_bits(float value)
{
	unsigned long bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static long bake_percent10(unsigned long part, unsigned long whole)
{
	return whole ? (long)((part * 1000ull + whole / 2) / whole) : 0;
}

/* ---------- pvscount */

#define PVS_MAX_WORDS 16        /* 512 clusters, the engine's cap */
#define PVS_TESTS 1024          /* hull tests a view kept until the view ends */
#define PVS_PAIRS 4096          /* (cell, neighbour) hull-test counts */

struct pvs_cell
{
	unsigned long key0, key1; /* the eye's cluster and octant; the cell's x, y, z */
	unsigned long views;
	unsigned long used;
	unsigned long bits[1];    /* the union of the clusters its views drew (words long) */
};

static int pvs_on = 0, pvs_dir = 0, pvs_words = 0;
static float pvs_cell_size = 2.0f;
static unsigned char *pvs_pool = NULL;
static unsigned long pvs_pool_bytes = 0, pvs_cell_bytes = 0, pvs_cells = 0, pvs_cells_used = 0;
static unsigned long *pvs_pairs = NULL; /* key, count */
static unsigned short pvs_tests[PVS_TESTS];
static int pvs_test_count = 0, pvs_tests_lost = 0;
/* this window's counts */
static unsigned long pvs_w_views, pvs_w_fallback, pvs_w_outside, pvs_w_walked, pvs_w_visible, pvs_w_pvs, pvs_w_tests;
/* the run's */
static unsigned long pvs_r_walked, pvs_r_visible, pvs_r_pvs, pvs_r_tests, pvs_r_views_dropped, pvs_r_tests_dropped,
	pvs_r_fallback, pvs_r_outside;

static struct pvs_cell *pvs_cell_at(unsigned long slot)
{
	return (struct pvs_cell *)(pvs_pool + slot * pvs_cell_bytes);
}

static int pvs_popcount(const unsigned long *bits, int words, long cluster_count)
{
	int count = 0, word;

	for (word = 0; word < words; word++)
	{
		unsigned long value = bits[word];

		if (word == words - 1 && (cluster_count & 31))
			value &= (1ul << (cluster_count & 31)) - 1;
		count += __builtin_popcountl(value);
	}
	return count;
}

void psp_pvscount_portal(short neighbor)
{
	if (!pvs_on)
		return;
	if (pvs_test_count < PVS_TESTS)
		pvs_tests[pvs_test_count++] = (unsigned short)neighbor;
	else
		pvs_tests_lost++;
}

static void pvs_report(void)
{
	char line[640];
	unsigned long slot, union_views = 0, union_sum = 0, refusable = 0, sampled_cells = 0;
	unsigned long w = pvs_w_walked ? pvs_w_walked : 1;

	for (slot = 0; slot < pvs_cells; slot++)
	{
		struct pvs_cell *cell = pvs_cell_at(slot);

		if (!cell->used)
			continue;
		union_views += cell->views;
		union_sum += cell->views * (unsigned long)pvs_popcount(cell->bits, pvs_words, (long)pvs_words * 32);
		if (cell->views >= 10)
			sampled_cells++;
	}
	for (slot = 0; slot < PVS_PAIRS; slot++)
	{
		unsigned long key = pvs_pairs[2 * slot];

		if (key)
		{
			struct pvs_cell *cell = pvs_cell_at((key >> 10) - 1);
			unsigned long neighbor = key & 1023;

			if (!(cell->bits[neighbor >> 5] & (1ul << (neighbor & 31))))
				refusable += pvs_pairs[2 * slot + 1];
		}
	}
	snprintf(line, sizeof(line),
		"pvscount: %d views to game time %ld | walked %lu (fallback %lu, outside %lu): visible %lu.%lu a view, "
		"the eye cluster's PVS %lu.%lu, hull tests %lu.%lu | run: walked %lu, cells %lu of %lu (%lu with 10+ views; "
		"%lu views dropped), the cell's set %lu.%lu a view against visible %lu.%lu and PVS %lu.%lu; hull tests %lu, "
		"refusable %lu (%ld.%ld%%), %lu not kept; set bytes %lu (cell %d wu%s)",
		BAKE_WINDOW, game_time_get(), pvs_w_walked, pvs_w_fallback, pvs_w_outside,
		pvs_w_visible / w, pvs_w_visible * 10 / w % 10, pvs_w_pvs / w, pvs_w_pvs * 10 / w % 10,
		pvs_w_tests / w, pvs_w_tests * 10 / w % 10,
		pvs_r_walked, pvs_cells_used, pvs_cells, sampled_cells, pvs_r_views_dropped,
		union_views ? union_sum / union_views : 0, union_views ? union_sum * 10 / union_views % 10 : 0,
		pvs_r_walked ? pvs_r_visible / pvs_r_walked : 0, pvs_r_walked ? pvs_r_visible * 10 / pvs_r_walked % 10 : 0,
		pvs_r_walked ? pvs_r_pvs / pvs_r_walked : 0, pvs_r_walked ? pvs_r_pvs * 10 / pvs_r_walked % 10 : 0,
		pvs_r_tests, refusable, bake_percent10(refusable, pvs_r_tests) / 10, bake_percent10(refusable, pvs_r_tests) % 10,
		pvs_r_tests_dropped + (unsigned long)pvs_tests_lost, pvs_cells_used * (unsigned long)pvs_words * 4,
		(int)pvs_cell_size, pvs_dir ? ", by octant" : "");
	platform_psp_log("%s", line);
	pvs_w_views = pvs_w_fallback = pvs_w_outside = pvs_w_walked = pvs_w_visible = pvs_w_pvs = pvs_w_tests = 0;
}

static int pvs_floor(float value)
{
	return (int)floorf(value / pvs_cell_size);
}

void psp_pvscount_view(long eye_cluster, const float *eye, const float *forward, const unsigned long *visible,
	const unsigned long *pvs, long cluster_count, int fallback)
{
	unsigned long key0, key1, slot, probe;
	struct pvs_cell *cell = NULL;
	int k;

	if (!pvs_on)
		return;
	pvs_w_views++;
	if (fallback)
	{
		/* the walk was cut at its budget: its set is partial, so it teaches no cell */
		pvs_w_fallback++;
		pvs_r_fallback++;
		goto done;
	}
	if (eye_cluster < 0 || !pvs || cluster_count <= 0)
	{
		pvs_w_outside++;
		pvs_r_outside++;
		goto done;
	}
	if (!pvs_words)
	{
		pvs_words = (int)((cluster_count + 31) / 32);
		if (pvs_words > PVS_MAX_WORDS)
		{
			platform_psp_log("pvscount: %ld clusters, past its %d; it stays off", cluster_count, PVS_MAX_WORDS * 32);
			pvs_on = 0;
			return;
		}
		pvs_cell_bytes = sizeof(struct pvs_cell) + (pvs_words - 1) * sizeof(unsigned long);
		pvs_cells = pvs_pool_bytes / pvs_cell_bytes;
		if (pvs_cells > 1ul << 21)
			pvs_cells = 1ul << 21; /* the pair key keeps 22 bits for the cell */
	}
	{
		int visible_count = pvs_popcount(visible, pvs_words, cluster_count);
		int pvs_count = pvs_popcount(pvs, pvs_words, cluster_count);

		pvs_w_walked++;
		pvs_r_walked++;
		pvs_w_visible += visible_count;
		pvs_r_visible += visible_count;
		pvs_w_pvs += pvs_count;
		pvs_r_pvs += pvs_count;
		pvs_w_tests += pvs_test_count;
		pvs_r_tests += pvs_test_count;
	}
	/* the cluster (9 bits), the octant (3) and z (16) in one word */
	key0 = (unsigned long)eye_cluster << 3;
	if (pvs_dir)
		key0 |= (forward[1] < 0.0f ? 4u : 0u) | (forward[0] < 0.0f ? 2u : 0u) | (fabsf(forward[1]) > fabsf(forward[0]) ? 1u : 0u);
	key0 = key0 << 16 | ((unsigned long)pvs_floor(eye[2]) & 0xFFFFu);
	key1 = ((unsigned long)pvs_floor(eye[0]) & 0xFFFFu) << 16 | ((unsigned long)pvs_floor(eye[1]) & 0xFFFFu);
	slot = bake_hash(&key0, 1, bake_hash(&key1, 1, 2166136261u)) % pvs_cells;
	for (probe = 0; probe < pvs_cells; probe++, slot = (slot + 1) % pvs_cells)
	{
		struct pvs_cell *at = pvs_cell_at(slot);

		if (!at->used)
		{
			/* a quarter of the pool kept free, so the probes stay short */
			if (pvs_cells_used * 4 >= pvs_cells * 3)
				break;
			at->used = 1;
			at->key0 = key0;
			at->key1 = key1;
			pvs_cells_used++;
			cell = at;
			break;
		}
		if (at->key0 == key0 && at->key1 == key1)
		{
			cell = at;
			break;
		}
	}
	if (!cell)
	{
		pvs_r_views_dropped++;
		pvs_r_tests_dropped += pvs_test_count;
		goto done;
	}
	cell->views++;
	for (k = 0; k < pvs_words; k++)
		cell->bits[k] |= visible[k];
	for (k = 0; k < pvs_test_count; k++)
	{
		unsigned long key = (slot + 1) << 10 | pvs_tests[k], at = bake_hash(&key, 1, 2166136261u) % PVS_PAIRS;
		unsigned long tries;

		for (tries = 0; tries < PVS_PAIRS / 2; tries++, at = (at + 1) % PVS_PAIRS)
			if (pvs_pairs[2 * at] == key || !pvs_pairs[2 * at])
				break;
		if (tries == PVS_PAIRS / 2)
		{
			pvs_r_tests_dropped++;
			continue;
		}
		pvs_pairs[2 * at] = key;
		pvs_pairs[2 * at + 1]++;
	}
done:
	pvs_test_count = 0;
	if (pvs_w_views >= BAKE_WINDOW)
		pvs_report();
}

/* ---------- shadowcount */

#define SHADOW_SLOTS 1024
#define SHADOW_GROUPS 4

struct shadow_caster
{
	long object;
	unsigned long first, last; /* its volume's hash at its first cast and its last */
	unsigned long calls, surfaces;
	unsigned char used, group, moved;
};

static const char *const shadow_group_names[SHADOW_GROUPS] = { "scenery", "devices", "units", "items" };
static int shadow_on = 0;
static struct shadow_caster *shadow_casters = NULL;
static struct shadow_caster *shadow_current = NULL;
static int shadow_current_same = 0, shadow_current_group = 0, shadow_open = 0;
static unsigned long shadow_frames, shadow_untracked, shadow_tracked;
static unsigned long shadow_w_calls[SHADOW_GROUPS], shadow_w_surfaces[SHADOW_GROUPS], shadow_w_same_calls[SHADOW_GROUPS],
	shadow_w_same_surfaces[SHADOW_GROUPS], shadow_w_new[SHADOW_GROUPS], shadow_w_most;
static unsigned long shadow_frame_calls;

int psp_shadowcount_on(void)
{
	return shadow_on;
}

static int shadow_group(int type)
{
	if (type == 6)
		return 0;                 /* scenery */
	if (type >= 7 && type <= 9)
		return 1;                 /* machines, controls, light fixtures */
	if (type == 0 || type == 1)
		return 2;                 /* bipeds, vehicles */
	return 3;                     /* weapons, equipment, garbage, projectiles, the rest */
}

static void shadow_report(void)
{
	char line[900];
	int length, g;
	unsigned long k, calls = 0, still_calls = 0, surfaces = 0, still_surfaces = 0, still_casters = 0;
	unsigned long g_calls[SHADOW_GROUPS] = { 0 }, g_still[SHADOW_GROUPS] = { 0 };

	length = snprintf(line, sizeof(line), "shadowcount: %d frames to game time %ld | calls by group "
		"(calls/surfaces, of them as their last cast calls/surfaces, first casts) [most in a frame %lu]:",
		BAKE_WINDOW, game_time_get(), shadow_w_most);
	for (g = 0; g < SHADOW_GROUPS; g++)
	{
		length += snprintf(line + length, sizeof(line) - length, " %s %lu/%lu same %lu/%lu new %lu;",
			shadow_group_names[g], shadow_w_calls[g], shadow_w_surfaces[g], shadow_w_same_calls[g],
			shadow_w_same_surfaces[g], shadow_w_new[g]);
		shadow_w_calls[g] = shadow_w_surfaces[g] = shadow_w_same_calls[g] = shadow_w_same_surfaces[g] = shadow_w_new[g] = 0;
	}
	for (k = 0; k < SHADOW_SLOTS; k++)
	{
		struct shadow_caster *caster = &shadow_casters[k];

		if (!caster->used)
			continue;
		calls += caster->calls;
		surfaces += caster->surfaces;
		g_calls[caster->group] += caster->calls;
		if (!caster->moved)
		{
			still_casters++;
			still_calls += caster->calls;
			still_surfaces += caster->surfaces;
			g_still[caster->group] += caster->calls;
		}
	}
	length += snprintf(line + length, sizeof(line) - length,
		" | run: casters %lu (untracked calls %lu), never moved %lu: calls %lu of %lu (%ld.%ld%%), surfaces %lu of %lu "
		"(%ld.%ld%%); never-moved calls by group",
		shadow_tracked, shadow_untracked, still_casters, still_calls, calls,
		bake_percent10(still_calls, calls) / 10, bake_percent10(still_calls, calls) % 10, still_surfaces, surfaces,
		bake_percent10(still_surfaces, surfaces) / 10, bake_percent10(still_surfaces, surfaces) % 10);
	for (g = 0; g < SHADOW_GROUPS && length < (int)sizeof(line) - 40; g++)
		length += snprintf(line + length, sizeof(line) - length, " %s %lu/%lu", shadow_group_names[g], g_still[g], g_calls[g]);
	platform_psp_log("%s", line);
	shadow_w_most = 0;
}

void psp_shadowcount_frame(void)
{
	if (!shadow_on)
		return;
	if (shadow_frame_calls > shadow_w_most)
		shadow_w_most = shadow_frame_calls;
	shadow_frame_calls = 0;
	if (++shadow_frames % BAKE_WINDOW == 0)
		shadow_report();
}

void psp_shadowcount_begin(long object_index, int type, const unsigned long *matrix, long words, float radius)
{
	unsigned long radius_bits = bake_float_bits(radius);
	unsigned long volume = bake_hash(&radius_bits, 1, bake_hash(matrix, words, 2166136261u));
	unsigned long slot = bake_hash((const unsigned long *)&object_index, 1, 2166136261u) % SHADOW_SLOTS, probe;
	int g = shadow_group(type);

	if (!shadow_on)
		return;
	shadow_frame_calls++;
	shadow_open = 1;
	shadow_current = NULL;
	shadow_current_group = g;
	shadow_current_same = 0;
	shadow_w_calls[g]++;
	for (probe = 0; probe < SHADOW_SLOTS; probe++, slot = (slot + 1) % SHADOW_SLOTS)
	{
		struct shadow_caster *caster = &shadow_casters[slot];

		if (!caster->used)
		{
			if (shadow_tracked * 4 >= SHADOW_SLOTS * 3)
				break;
			memset(caster, 0, sizeof(*caster));
			caster->used = 1;
			caster->object = object_index;
			caster->group = (unsigned char)g;
			caster->first = caster->last = volume;
			shadow_tracked++;
			shadow_w_new[g]++;
			shadow_current = caster;
			break;
		}
		if (caster->object == object_index)
		{
			shadow_current_same = caster->last == volume;
			if (volume != caster->first)
				caster->moved = 1;
			caster->last = volume;
			shadow_current = caster;
			break;
		}
	}
	if (!shadow_current)
		shadow_untracked++;
	else
		shadow_current->calls++;
	if (shadow_current_same)
		shadow_w_same_calls[g]++;
}

/* structure_render_shadow's receivers for the cast just begun */
void psp_shadowcount_surfaces(long surface_count)
{
	if (!shadow_on || !shadow_open)
		return;
	shadow_open = 0;
	shadow_w_surfaces[shadow_current_group] += surface_count;
	if (shadow_current_same)
		shadow_w_same_surfaces[shadow_current_group] += surface_count;
	if (shadow_current)
		shadow_current->surfaces += surface_count;
}

/* ---------- lightcount */

#define LIGHT_SLOTS 256
#define LIGHT_GROUPS 4
#define LIGHT_STATES 3 /* still (as its last pass), moved, new */

struct light_seen
{
	long light;
	unsigned long first, last;
	unsigned long passes, draws, triangles;
	unsigned char used, group, moved;
};

static const char *const light_group_names[LIGHT_GROUPS] = { "fixtures", "units", "items", "effects" };
static const char *const light_state_names[LIGHT_STATES] = { "still", "moved", "new" };
static int light_on = 0;
static struct light_seen *light_seen = NULL;
static struct light_seen *light_current = NULL;
static int light_current_group = -1, light_current_state = 0;
static unsigned long light_frames, light_tracked, light_untracked, light_w_whole, light_w_outside;
static unsigned long light_w[LIGHT_GROUPS][LIGHT_STATES][3]; /* passes, draws, triangles */

int psp_lightcount_on(void)
{
	return light_on;
}

static int light_group(int type)
{
	if (type >= 6)
		return 0;                 /* scenery, machines, controls, light fixtures, the rest that stand */
	if (type == 0 || type == 1)
		return 1;                 /* bipeds, vehicles */
	if (type >= 2 && type <= 4)
		return 2;                 /* weapons, equipment, garbage */
	return 3;                     /* projectiles, and lights no object carries (effects) */
}

static void light_report(void)
{
	char line[1100];
	int length, g, s;
	unsigned long k, draws = 0, triangles = 0, still_draws = 0, still_triangles = 0, still_lights = 0;
	unsigned long g_draws[LIGHT_GROUPS] = { 0 }, g_still[LIGHT_GROUPS] = { 0 };

	length = snprintf(line, sizeof(line), "lightcount: %d frames to game time %ld | passes/draws/triangles by carrier "
		"and state (still: sphere and clusters as its last pass), %lu without clusters, %lu draws outside a pass:",
		BAKE_WINDOW, game_time_get(), light_w_whole, light_w_outside);
	for (g = 0; g < LIGHT_GROUPS; g++)
	{
		length += snprintf(line + length, sizeof(line) - length, " %s", light_group_names[g]);
		for (s = 0; s < LIGHT_STATES; s++)
			length += snprintf(line + length, sizeof(line) - length, " %s %lu/%lu/%lu", light_state_names[s],
				light_w[g][s][0], light_w[g][s][1], light_w[g][s][2]);
		length += snprintf(line + length, sizeof(line) - length, ";");
	}
	memset(light_w, 0, sizeof(light_w));
	light_w_whole = light_w_outside = 0;
	for (k = 0; k < LIGHT_SLOTS; k++)
	{
		struct light_seen *seen = &light_seen[k];

		if (!seen->used)
			continue;
		draws += seen->draws;
		triangles += seen->triangles;
		g_draws[seen->group] += seen->draws;
		if (!seen->moved)
		{
			still_lights++;
			still_draws += seen->draws;
			still_triangles += seen->triangles;
			g_still[seen->group] += seen->draws;
		}
	}
	length += snprintf(line + length, sizeof(line) - length,
		" | run: lights %lu (untracked passes %lu), never moved %lu: draws %lu of %lu (%ld.%ld%%), triangles %lu of %lu "
		"(%ld.%ld%%); never-moved draws by carrier",
		light_tracked, light_untracked, still_lights, still_draws, draws,
		bake_percent10(still_draws, draws) / 10, bake_percent10(still_draws, draws) % 10, still_triangles, triangles,
		bake_percent10(still_triangles, triangles) / 10, bake_percent10(still_triangles, triangles) % 10);
	for (g = 0; g < LIGHT_GROUPS && length < (int)sizeof(line) - 40; g++)
		length += snprintf(line + length, sizeof(line) - length, " %s %lu/%lu", light_group_names[g], g_still[g], g_draws[g]);
	platform_psp_log("%s", line);
}

/* at the diffuse pass's start and end: draws past them belong to no light */
void psp_lightcount_frame(int start)
{
	if (!light_on)
		return;
	light_current = NULL;
	light_current_group = -1;
	if (start && ++light_frames % BAKE_WINDOW == 0)
		light_report();
}

void psp_lightcount_light(long light_index, int type, const float *center, float radius, short cluster_count,
	const short *clusters)
{
	unsigned long radius_bits = bake_float_bits(radius), center_bits[3], key, count = (unsigned long)cluster_count;
	unsigned long slot = bake_hash((const unsigned long *)&light_index, 1, 2166136261u) % LIGHT_SLOTS, probe;
	int g = light_group(type), k;

	if (!light_on)
		return;
	memcpy(center_bits, center, sizeof(center_bits));
	key = bake_hash(&radius_bits, 1, bake_hash(center_bits, 3, 2166136261u));
	if (!clusters)
	{
		/* the first person's supersized light: the whole visible level, no cluster list */
		light_w_whole++;
		count = 0;
	}
	key = bake_hash(&count, 1, key);
	for (k = 0; k < (int)count; k++)
	{
		unsigned long cluster = (unsigned long)(unsigned short)clusters[k];

		key = bake_hash(&cluster, 1, key);
	}
	light_current = NULL;
	light_current_group = g;
	light_current_state = 2;
	for (probe = 0; probe < LIGHT_SLOTS; probe++, slot = (slot + 1) % LIGHT_SLOTS)
	{
		struct light_seen *seen = &light_seen[slot];

		if (!seen->used)
		{
			if (light_tracked * 4 >= LIGHT_SLOTS * 3)
				break;
			memset(seen, 0, sizeof(*seen));
			seen->used = 1;
			seen->light = light_index;
			seen->group = (unsigned char)g;
			seen->first = seen->last = key;
			light_tracked++;
			light_current = seen;
			break;
		}
		if (seen->light == light_index)
		{
			light_current_state = seen->last == key ? 0 : 1;
			if (key != seen->first)
				seen->moved = 1;
			seen->last = key;
			light_current = seen;
			break;
		}
	}
	if (light_current)
		light_current->passes++;
	else
		light_untracked++;
	light_w[g][light_current_state][0]++;
}

/* each light draw the pass records (psp_environment_light) */
void psp_lightcount_draw(long triangle_count)
{
	if (!light_on)
		return;
	if (light_current_group < 0)
	{
		light_w_outside++;
		return;
	}
	light_w[light_current_group][light_current_state][1]++;
	light_w[light_current_group][light_current_state][2] += triangle_count;
	if (light_current)
	{
		light_current->draws++;
		light_current->triangles += triangle_count;
	}
}

/* ---------- the tables, right after keep.txt is read (psp_recon_tables_start) */

void psp_bakecounts_start(void)
{
	if (psp_keep("pvscount"))
	{
		pvs_pool_bytes = (unsigned long)psp_keep_number("pvscountkb", 64) * 1024;
		pvs_cell_size = (float)psp_keep_number("pvscell", 2);
		if (pvs_cell_size < 1.0f)
			pvs_cell_size = 1.0f;
		pvs_dir = psp_keep("pvscountdir");
		pvs_pool = psp_recon_table("pvscount", pvs_pool_bytes);
		pvs_pairs = pvs_pool ? psp_recon_table("pvscount pairs", PVS_PAIRS * 2 * sizeof(unsigned long)) : NULL;
		pvs_on = pvs_pool && pvs_pairs;
		if (pvs_on && psp_keep("mevisland"))
			platform_psp_log("pvscount: mevisland is on, so the ME walks most views and only the main CPU's are counted: "
				"add mevislandoff");
	}
	if (psp_keep("shadowcount"))
	{
		shadow_casters = psp_recon_table("shadowcount", SHADOW_SLOTS * sizeof(*shadow_casters));
		shadow_on = shadow_casters != NULL;
	}
	if (psp_keep("lightcount"))
	{
		light_seen = psp_recon_table("lightcount", LIGHT_SLOTS * sizeof(*light_seen));
		light_on = light_seen != NULL;
	}
}

#endif
