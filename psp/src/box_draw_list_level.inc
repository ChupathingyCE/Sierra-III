/* The draw list box's first user: the level draws' kept prologues
(docs/box_draw_list.md §12.1a, Tier 1). Compiled inside psp_ge.c's unit, at
its end and only with PSP_DRAWLIST, because it reads the level draw's own
state (the slant buckets, the detail and glow records, fog, the 16-bit
copies, the dither's memo); psp_ge.c holds only the hooks §12.1a lists. The
store, the blocks, the emitter and the check's model are box_draw_list.c's.

A level draw's passes write the same binds and pass state every frame it is
drawn; what the camera decides (which triangles, their slant buckets, the cut
pieces, the uv centre's shift, the matrices from the origin) is the only part
that changes. Under "levelkept" the binds and pass state are written once as
blocks of a slot and CALLed on later frames while the stamp of everything
their words were made from holds; the rest is written each frame straight
into the list. "levelkeptcheck" writes each kept draw both ways and compares
what the GE would draw */

unsigned int psp_tex_kept_words(unsigned int key);
int psp_tex_kept_find(unsigned int key);
unsigned int psp_tex_kept_at(int index, int mark, int *has_alpha);
void psp_tex_kept_forget(void);
extern int psp_tex_standin;
extern PSP_MAIN_SIDE unsigned long psp_tex_map_opens;
extern int gu_curr_context, gu_object_stack_depth;

/* why a draw went the old way, or why its stamp failed */
enum { LKR_OTHER, LKR_CUT, LKR_MISSING, LKR_FENCED, LKR_FULL, LKR_ROOM, LKR_KINDS };
enum { LKS_TEXTURES, LKS_PASSES, LKS_DETAIL_SCALE, LKS_FOG, LKS_MODES, LKS_CONTEXT, LKS_MAP, LKS_KINDS };
/* a slot is 768 bytes by default: the largest prologue the emulator met was
752 bytes (levelkeptproof5), and one that might not fit is refused before
it's written (lk_words_most_for); the worst case, eight levels on every one of
six textures, is ~1,380 bytes */
#define LK_SLOT_LEAST 512
#define LK_SLOT_DEFAULT 768

static int level_kept = -1, level_kept_checking = 0, lk_dead = 0, lk_bindblocks = 0;
static unsigned long lk_draws, lk_made, lk_called, lk_blocks_called, lk_refused[LKR_KINDS], lk_stamp_failed[LKS_KINDS],
	lk_words_most, lk_live_words, lk_checked, lk_check_prims, lk_check_differ, lk_check_unchecked, lk_check_unsupported;
static int lk_used_most;
/* the check's differences by what differs (a register, 0x100-0x109 as
dl_check_compare names them), and the first one's values: kept as counts the
main CPU's report prints, since the ME's log keeps a line's words only */
#define LK_WHAT 0x110
static unsigned short lk_check_what[LK_WHAT];
static unsigned int lk_check_first[4];

static int level_geo_checking = 0;

static int level_kept_on(void)
{
	if (level_kept < 0)
	{
		level_geo_checking = psp_keep("levelgeocheck") != 0;
		level_kept_checking = level_geo_checking || psp_keep("levelkeptcheck") != 0;
		level_kept = level_kept_checking || psp_keep("levelkept") || psp_keep("levelkept2");
		lk_bindblocks = psp_keep("bindblocks") != 0;
	}
	return level_kept;
}

static void level_geo_carve(void);

/* the store, taken from the display list's end as the GE starts: the words
it holds are words the list would have held, and the list's fullest frame
must still leave 64 KB beside it on every map (levelkeptroom3 measures it
with the kept geometry's store beside this one); "levelkeptkbN" its size
(40 KB: 41 slots of 768 bytes and their entries), "levelkeptslotN" a slot's
bytes */
static void level_kept_carve(void)
{
	extern int psp_keep_number(const char *prefix, int fallback);
	unsigned int bytes, slot;
	int count;

	level_geo_carve();
	if (!level_kept_on() || !display_list || dl_store_slots())
		return;
	bytes = (unsigned int)psp_keep_number("levelkeptkb", 40);
	bytes = (bytes < 16 ? 16 : bytes > 256 ? 256 : bytes) * 1024u;
	slot = (unsigned int)psp_keep_number("levelkeptslot", LK_SLOT_DEFAULT);
	slot = ((slot < LK_SLOT_LEAST ? LK_SLOT_LEAST : slot > 8192 ? 8192 : slot) + 63) & ~63u;
	if (bytes > (unsigned int)display_list_size / 4)
	{
		platform_log("GE: levelkept: a %u KB store is more than a quarter of the %d KB list: off", bytes / 1024,
			display_list_size / 1024);
		level_kept = 0;
		return;
	}
	count = dl_store_carve((unsigned char *)display_list + display_list_size - bytes, bytes, slot);
	if (!count)
	{
		level_kept = 0;
		return;
	}
	display_list_size -= (int)bytes;
	platform_log("GE: levelkept%s: the store is the display list's last %u KB (the list now %d KB): %d slots of %u bytes",
		level_kept_checking ? " (with its check)" : "", bytes / 1024, display_list_size / 1024, count, slot);
}

/* the keep words and debug state the passes read lazily, read here first so
the stamp sees what the passes will */
static void lk_lazy(void)
{
	if (detail_off < 0)
		detail_off = psp_keep("nodetail");
	if (detail_far < 0)
		detail_far = !psp_keep("detailnear");
	if (detail_bias < -1.0f)
		detail_bias = psp_keep("detailbias15") ? 1.5f : psp_keep("detailbias2") ? 2.5f : 0.0f;
	if (detail_slope < 0)
		detail_slope = !psp_keep("detailauto");
	if (detail_far_old < 0)
		detail_far_old = psp_keep("detailfarold") != 0;
	if (level_glow_off < 0)
	{
		level_glow_off = psp_keep("nolevelglow");
		if (level_glow_off)
			platform_log("GE: nolevelglow: level self-illumination left out");
	}
}

static unsigned int lk_bits(float f)
{
	union { float f; unsigned int u; } bits;

	bits.f = f;
	return bits.u;
}

/* one texture's two stamp words (its key, or 0, and its address), by its
place in the pack: the place the entry kept (user, the shorts of its first
three words, with the map's generation in the fourth) when the key is the one
its stamp names (old), else looked up. 1 when in the pack but not resident */
static unsigned int lk_tex(unsigned int key, int j, const unsigned int *old, const unsigned int *user, short *tex,
	int *alpha)
{
	int index;

	if (!key)
	{
		tex[j] = -1;
		return 0;
	}
	if (old && user[3] == (unsigned int)psp_tex_map_opens && old[2 * j] == key)
		index = ((const short *)user)[j];
	else
		index = psp_tex_kept_find(key);
	tex[j] = (short)index;
	return psp_tex_kept_at(index, 0, alpha);
}

/* every input a prologue's words are made from; 0 when a texture the passes
bind is in the pack but not resident (the old path's bind asks for it). old
and user: the entry found for the draw (NULL: none), whose kept places save
the textures' lookups */
static int lk_stamp_make(unsigned int *s, short *tex, const struct level_stream *stream, unsigned int base_key,
	unsigned int lightmap_key, int alpha_tested, int detail_runs, int *base_alpha, const unsigned int *old,
	const unsigned int *user)
{
	unsigned int a;
	int k, n = 0, alpha = 0;

	a = lk_tex(base_key, 0, old, user, tex, &alpha);
	if (a <= 1)
		return 0;
	*base_alpha = alpha;
	s[n++] = base_key;
	s[n++] = a;
	a = lk_tex(stream->lightmap ? lightmap_key : 0, 1, old, user, tex, NULL);
	if (a == 1)
		return 0;
	s[n++] = a ? lightmap_key : 0;
	s[n++] = a;
	for (k = 0; k < 2; k++)
	{
		a = lk_tex(detail_runs ? detail_key[k] : 0, 2 + k, old, user, tex, NULL);
		if (a == 1)
			return 0;
		s[n++] = a ? detail_key[k] : 0;
		s[n++] = a;
	}
	for (k = 0; k < 2; k++)
	{
		a = lk_tex(!level_glow_off && level_glow_color[k] ? level_glow_key[k] : 0, 4 + k, old, user, tex, NULL);
		if (a == 1)
			return 0;
		s[n++] = a ? level_glow_key[k] : 0;
		s[n++] = a;
	}
	/* 12: which passes and how */
	s[n++] = (unsigned int)(alpha && alpha_tested) | (unsigned int)(uv_centre > 0) << 1 | (unsigned int)detail_runs << 2;
	/* 13-16 */
	s[n++] = lk_bits(detail_u[0]);
	s[n++] = lk_bits(detail_v[0]);
	s[n++] = lk_bits(detail_u[1]);
	s[n++] = lk_bits(detail_v[1]);
	/* 17-19 */
	s[n++] = lk_bits(fog_near);
	s[n++] = lk_bits(fog_far);
	s[n++] = fog_color;
	/* 20-24 */
	s[n++] = (unsigned int)tex_level_mode;
	s[n++] = lk_bits(tex_bias);
	s[n++] = lk_bits(detail_bias);
	s[n++] = (unsigned int)detail_far;
	s[n++] = (unsigned int)(dither_world & 1) | (unsigned int)(dither_all > 0) << 1;
	/* 25-30: pspgu's state the words may be made from */
	dl_gu_stamp(s + n);
	n += 6;
	/* 31: a new map's pack may give a key another texture at the same place */
	s[n++] = (unsigned int)psp_tex_map_opens;
	return n == DL_STAMP;
}

static int lk_stamp_kind(int word)
{
	return word < 12 ? LKS_TEXTURES : word == 12 ? LKS_PASSES : word < 17 ? LKS_DETAIL_SCALE : word < 20 ? LKS_FOG :
		word < 25 ? LKS_MODES : word < 31 ? LKS_CONTEXT : LKS_MAP;
}

/* the most words a draw's blocks can take, from the stamp: each bind's
(psp_tex_kept_words) and each block's state at its most (the calls in
level_kept_passes, with pspgu's words a call: the fog and the blend 3, the
material colour 3, the offset and scale 2, the rest 1) and its RET */
static unsigned int lk_words_most_for(const struct lk_now *k)
{
	const unsigned int *s = k->stamp;
	unsigned int words = 0, details = 0;
	int lightmap = s[3] > 1, glows = (s[9] > 1) + (s[11] > 1), j;

	for (j = 0; j < 12; j += 2)
		if (s[j + 1] > 1)
			words += psp_tex_kept_words(s[j]);
	words += 5 + 1; /* the base's state */
	words += 3 + (lightmap ? 13 : 0) + 1; /* the base's close, the lightmap's state */
	if (k->detail_runs)
	{
		details = (s[5] > 1) + (s[7] > 1);
		words += 8 + (details ? details : 1) * (15 + 1);
	}
	if (glows)
		words += 12 + 14 + 2;
	if (glows || k->detail_runs || lightmap)
		words += 16 + 1; /* the last close */
	return words;
}

/* the decision, at the draw's base bind: whether its passes go the kept way
(LK_KEPT), both ways for the check (LK_CHECK), or as before (LK_OFF) */
static int level_kept_mode(struct lk_now *k, struct level_stream *stream, unsigned int base_key,
	unsigned int lightmap_key, int alpha_tested, int cut_count)
{
	int failed, found;

	k->mode = LK_OFF;
	if (!level_kept_on() || !dl_store_slots() || lk_dead)
		return LK_OFF;
	lk_draws++;
	if (psp_pass_mode || psp_tex_standin || ge_skip || ge_reads_on > 0 || (PSP_DITHERSHIFT && dither_shift > 0) ||
		(lk_bindblocks && !PSP_ME_WORK()) || gu_curr_context != GU_DIRECT || gu_object_stack_depth)
	{
		lk_refused[LKR_OTHER]++;
		return LK_OFF;
	}
	/* the cut pieces are drawn from the corners made once a draw; a draw
	whose corners didn't fit makes them again in each pass (list_take) */
	if (cut_count > 0 && (cut_count != cut_vertices_count || !cut_vertices_prepared[0] ||
		(stream->lightmap && !cut_vertices_prepared[1])))
	{
		lk_refused[LKR_CUT]++;
		return LK_OFF;
	}
	lk_lazy();
	k->detail_runs = !detail_off && (detail_key[0] || detail_key[1]) && angle_near_any;
	found = dl_entry_find(stream, base_key, stream->lk_hint);
	if (!lk_stamp_make(k->stamp, k->tex, stream, base_key, lightmap_key, alpha_tested, k->detail_runs, &k->base_alpha,
		found >= 0 ? dl_entry_stamp(found) : NULL, found >= 0 ? dl_entry_user(found) : NULL))
	{
		lk_refused[LKR_MISSING]++;
		return LK_OFF;
	}
	k->entry = dl_entry_get(stream, base_key, k->stamp, ge_hot.stream_frame, found, &k->making, &failed);
	if (failed >= 0)
		lk_stamp_failed[lk_stamp_kind(failed)]++;
	if (k->entry < 0)
	{
		lk_refused[k->entry == DL_FENCED ? LKR_FENCED : LKR_FULL]++;
		return LK_OFF;
	}
	stream->lk_hint = (short)k->entry;
	if (k->making && lk_words_most_for(k) > dl_store_slot_bytes() / 4)
	{
		lk_refused[LKR_ROOM]++;
		return LK_OFF;
	}
	k->mode = level_kept_checking ? LK_CHECK : LK_KEPT;
	if (k->mode == LK_CHECK)
	{
		/* the old form first, from here, with the GE handed nothing until
		both forms are compared and the old one taken back out */
		dl_gu_save(&k->saved);
		k->dither = dither_now;
		k->blend = blend_now;
		k->from = gu_list->current;
		k->pending[0] = pending_start;
		k->pending[1] = pending_end;
		k->jumped = show3_part_jumped;
		dl_stall_held++;
		/* both forms start from the GE's state unknown, so each sets all it uses */
		psp_tex_kept_forget();
		lit_forget(2);
		dither_now = -1;
	}
	return k->mode;
}

/* a draw as the draw macro makes it (its VRAM copy, if any), straight into the list */
static void lk_draw(int vtype, int count, const void *indices, const void *vertices)
{
	if (vm_on > 0)
		vertices = vm_redirect(vertices, count);
	dl_draw(GU_TRIANGLES, vtype, count, indices, vertices);
}

/* draw_pass, with the cut pieces always from the corners made once a draw
(level_kept_mode refuses a draw without them) */
static void lk_draw_pass(const struct ge_vertex *stream, const unsigned short *indices, int index_count, int cut_count,
	int lightmap)
{
	if (index_count > 0 && compact_now && (lightmap ? compact_now->compact_lightmap : compact_now->compact_base))
	{
		compact_begin(lightmap);
		sceGumUpdateMatrix();
		lk_draw(GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_INDEX_16BIT | GU_TRANSFORM_3D, index_count, indices,
			lightmap ? (const void *)compact_now->compact_lightmap : (const void *)compact_now->compact_base);
		compact_end();
	}
	else if (index_count > 0)
	{
		sceGumUpdateMatrix();
		lk_draw(GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D, index_count, indices, stream);
	}
	if (cut_count > 0)
	{
		sceGumUpdateMatrix();
		lk_draw(GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_3D, cut_count, NULL, cut_vertices_prepared[lightmap ? 1 : 0]);
	}
}

/* draw_pass_angled */
static void lk_draw_pass_angled(const struct ge_vertex *stream, const unsigned short *indices, int index_count,
	int cut_count, int lightmap, float slope, int near_only)
{
	int b, buckets = near_only ? ANGLE_BUCKETS : SORT_BUCKETS;
	int compact = compact_now && compact_now->compact_base && !lightmap;

	if (!angle_bucketed || slope <= 0.0f || index_count <= 0)
	{
		lk_draw_pass(stream, indices, index_count, cut_count, lightmap);
		return;
	}
	if (compact)
		compact_begin(0);
	sceGumUpdateMatrix();
	for (b = 0; b < buckets; b++)
	{
		if (!angle_count[b])
			continue;
		dl_slope(slope * angle_factor[b % ANGLE_BUCKETS]);
		if (compact)
			lk_draw(GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_INDEX_16BIT | GU_TRANSFORM_3D, (int)angle_count[b] * 3,
				indices + 3 * angle_first[b], compact_now->compact_base);
		else
			lk_draw(GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D, (int)angle_count[b] * 3,
				indices + 3 * angle_first[b], stream);
	}
	if (compact)
		compact_end();
	dl_slope(slope);
	if (cut_count > 0)
		lk_draw_pass(stream, NULL, 0, cut_count, lightmap);
}

/* each texture the prologue binds, marked used as its bind would mark it,
by its place in the pack */
static void lk_touch(const unsigned int *s, const short *tex)
{
	int k;

	for (k = 0; k < 6; k++)
		if (s[2 * k + 1] > 1)
			psp_tex_kept_at(tex[k], 1, NULL);
}

/* the passes of psp_ge_draw_level_body's textured draw, their binds and
state as blocks and the rest straight into the list. The blocks' order is
fixed by the stamp (which passes the draw has, whether its detail passes
run), so the state each block starts from is the same on every frame that
CALLs it as on the frame that made it */
static void level_kept_passes(struct lk_now *k, const struct level_stream *stream, const unsigned short *draw,
	long kept_count, int cut_count, unsigned int base_key, unsigned int lightmap_key, int alpha_tested, float base_slope)
{
	struct dl_run run;
	const unsigned int *s = k->stamp;
	int lightmap = s[3] > 1, glows = (s[9] > 1) + (s[11] > 1), glow_begun = 0, which, pass, user[2];
	int alpha = k->base_alpha && alpha_tested;
	unsigned int *live_start = gu_list->current;

	dl_run_start(&run, k->entry, k->making, stream, base_key, k->stamp, ge_hot.stream_frame);
	if (run.making)
	{
		unsigned int *places = dl_entry_user(k->entry);

		/* the textures' places, for the next frames' stamps */
		memcpy(places, k->tex, sizeof(k->tex));
		places[3] = (unsigned int)psp_tex_map_opens;
		/* a block sets every state it uses: the memos name nothing, and the
		dither is written by the first blend */
		psp_tex_kept_forget();
		lit_forget(2);
		dither_now = -1;
		lk_made++;
	}
	else
	{
		lk_touch(s, k->tex);
		lk_called++;
	}

	/* pass 1: the base texture */
	if (dl_block_begin(&run))
	{
		psp_tex_bind(base_key, NULL);
		sceGuEnable(GU_TEXTURE_2D);
		sceGuTexFunc(GU_TFX_REPLACE, alpha ? GU_TCC_RGBA : GU_TCC_RGB);
		sceGuTexWrap(GU_REPEAT, GU_REPEAT);
		if (alpha)
		{
			sceGuAlphaFunc(GU_GREATER, 0x7F, 0xFF);
			sceGuEnable(GU_ALPHA_TEST);
		}
	}
	dl_block_end(&run);
	if (uv_centre)
		uv_shift(1.0f, 1.0f);
	lk_draw_pass_angled(stream->base, draw, (int)kept_count * 3, cut_count, 0, base_slope, 0);
	textured_draws++;

	/* pass 2: the lightmap */
	if (dl_block_begin(&run))
	{
		sceGuDisable(GU_ALPHA_TEST);
		if (uv_centre)
			sceGuTexOffset(0.0f, 0.0f);
		if (lightmap)
		{
			psp_tex_bind_lightmap(lightmap_key);
			sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
			sceGuTexWrap(GU_CLAMP, GU_CLAMP);
			sceGuDepthFunc(GU_EQUAL);
			sceGuDepthMask(GU_TRUE);
			sceGuFog(fog_near, fog_far, 0xFFFFFFFF);
			sceGuBlendFunc(GU_ADD, GU_DST_COLOR, GU_FIX, 0, 0);
			ge_blend_dithered();
		}
	}
	dl_block_end(&run);
	if (lightmap)
		lk_draw_pass(stream->lightmap, draw, (int)kept_count * 3, cut_count, 1);

	/* passes 3 and 4: the detail maps */
	if (k->detail_runs)
	{
		float far = fog_far < DETAIL_FAR ? fog_far : DETAIL_FAR;
		float near = fog_near < DETAIL_NEAR ? fog_near : DETAIL_NEAR;
		int first = 1;

		for (pass = 0; pass < 2; pass++)
		{
			int detail_width = 0, detail_height = 0;
			float detail_scale, detail_pass_slope = 0.0f;

			if (!first && s[5 + 2 * pass] <= 1)
				continue;
			if (dl_block_begin(&run))
			{
				if (first)
				{
					if (lightmap)
					{
						ge_blend(0);
						sceGuFog(fog_near, fog_far, fog_color);
						sceGuDepthMask(GU_FALSE);
						sceGuDepthFunc(GU_GEQUAL);
					}
					sceGuTexLevelMode(tex_level_mode, tex_bias + detail_bias);
				}
				if (s[5 + 2 * pass] > 1)
				{
					psp_tex_bind(detail_key[pass], NULL);
					sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
					sceGuTexWrap(GU_REPEAT, GU_REPEAT);
					sceGuTexScale(detail_u[pass], detail_v[pass]);
					sceGuDepthFunc(GU_EQUAL);
					sceGuDepthMask(GU_TRUE);
					sceGuFog(near, far > near ? far : near + 1.0f, detail_neutral(detail_key[pass]));
					sceGuBlendFunc(GU_ADD, GU_DST_COLOR, GU_SRC_COLOR, 0, 0);
					ge_blend(1);
					ge_layer_dither();
				}
			}
			dl_block_end(&run);
			first = 0;
			if (s[5 + 2 * pass] <= 1)
				continue;
			psp_tex_size(detail_key[pass], &detail_width, &detail_height);
			detail_scale = sqrtf(fabsf(detail_u[pass] * detail_v[pass]));
			if (detail_slope)
				detail_pass_slope = texture_levels_by_distance(stream->uv_per_unit, detail_width, detail_height,
					detail_scale, tex_bias + detail_bias);
			pass_scale_u = detail_u[pass];
			pass_scale_v = detail_v[pass];
			if (uv_centre)
				uv_shift(detail_u[pass], detail_v[pass]);
			if (detail_by_distance)
			{
				int detail_count = (int)kept_count * 3, b;

				if (angle_bucketed && !detail_far_old)
				{
					long near_triangles = 0;

					for (b = 0; b < ANGLE_BUCKETS; b++)
						near_triangles += angle_count[b];
					detail_count = (int)near_triangles * 3;
				}
				dl_slope(detail_pass_slope);
				lk_draw_pass(stream->base, draw, detail_count, cut_count, 0);
			}
			else
				lk_draw_pass_angled(stream->base, draw, (int)kept_count * 3, cut_count, 0, detail_pass_slope, 1);
			detail_passes++;
		}
		pass_scale_u = pass_scale_v = 1.0f;
	}
	else if (!detail_off && (detail_key[0] || detail_key[1]))
		detail_passes_skipped_far++;

	/* the glow, then the closing state of the last pass drawn */
	for (which = 0; which < 2; which++)
	{
		if (s[9 + 2 * which] <= 1)
			continue;
		if (dl_block_begin(&run))
		{
			if (!glow_begun)
			{
				if (k->detail_runs)
				{
					ge_blend(0);
					sceGuTexLevelMode(tex_level_mode, tex_bias);
					sceGuTexScale(1.0f, 1.0f);
					sceGuTexOffset(0.0f, 0.0f);
					sceGuFog(fog_near, fog_far, fog_color);
					sceGuDepthMask(GU_FALSE);
					sceGuDepthFunc(GU_GEQUAL);
				}
				else if (lightmap)
				{
					ge_blend(0);
					sceGuFog(fog_near, fog_far, fog_color);
					sceGuDepthMask(GU_FALSE);
					sceGuDepthFunc(GU_GEQUAL);
				}
			}
			psp_tex_bind(level_glow_key[which], NULL);
			if (!glow_begun)
			{
				sceGuEnable(GU_TEXTURE_2D);
				sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGB);
				sceGuTexWrap(GU_REPEAT, GU_REPEAT);
				sceGuDisable(GU_ALPHA_TEST);
				sceGuDepthFunc(GU_EQUAL);
				sceGuDepthMask(GU_TRUE);
				sceGuFog(fog_near, fog_far, 0xFF000000);
				sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0xFFFFFF, 0xFFFFFF);
				ge_blend(1);
			}
		}
		dl_block_end(&run);
		if (!glow_begun && uv_centre)
			uv_shift(1.0f, 1.0f);
		glow_begun = 1;
		sceGuColor(0xFF000000 | level_glow_color[which]);
		lk_draw_pass_angled(stream->base, draw, (int)kept_count * 3, cut_count, 0, base_slope, 0);
		level_glow_passes++;
	}
	if (glows || k->detail_runs || lightmap)
	{
		if (dl_block_begin(&run))
		{
			ge_blend(0);
			if (glows)
			{
				if (uv_centre)
					sceGuTexOffset(0.0f, 0.0f);
			}
			else if (k->detail_runs)
			{
				sceGuTexLevelMode(tex_level_mode, tex_bias);
				sceGuTexScale(1.0f, 1.0f);
				sceGuTexOffset(0.0f, 0.0f);
			}
			sceGuFog(fog_near, fog_far, fog_color);
			sceGuDepthMask(GU_FALSE);
			sceGuDepthFunc(GU_GEQUAL);
			if (glows)
				sceGuColor(0xFFFFFFFF);
		}
		dl_block_end(&run);
	}

	/* the dither's and blend's memo kept with the slot, as pspgu's state is */
	user[0] = dither_now;
	user[1] = blend_now;
	if (!dl_run_finish(&run, user))
	{
		platform_log("GE: levelkept: a slot outgrew its room or a kept draw CALLed other than its blocks: off");
		lk_dead = 1;
	}
	dither_now = user[0];
	blend_now = user[1];
	if (run.making && dl_run_words(&run) > lk_words_most)
		lk_words_most = dl_run_words(&run);
	/* the GE's texture is the prologue's last bind */
	psp_tex_kept_forget();
	lk_blocks_called += (unsigned long)run.n;
	lk_live_words += (unsigned long)(gu_list->current - live_start) - 2u * (unsigned long)run.n;
	if (dl_entries_used() > lk_used_most)
		lk_used_most = dl_entries_used();
	dl_stall_pass();
}

/* levelkeptcheck, after the old form's passes: the kept form written after
them, the two compared draw by draw (box_draw_list.c's model of the GE), and
the old form taken out of the list, so the GE draws the kept one */
static void level_kept_check(struct lk_now *k, const struct level_stream *stream, const unsigned short *draw,
	long kept_count, int cut_count, unsigned int base_key, unsigned int lightmap_key, int alpha_tested, float base_slope)
{
	unsigned int *old_end = gu_list->current, *kept_end, a_value = 0, b_value = 0;
	unsigned int old_bytes = (unsigned int)((old_end - k->from) * 4);
	int draws, differing = 0, what = 0, unsupported = 0, first_draw = 0;

	/* a region taken or a jump made in the old form can't be taken back */
	if (pending_start != k->pending[0] || pending_end != k->pending[1] || show3_part_jumped != k->jumped ||
		(show3_part_end && ((unsigned int)old_end & ~0x40000000u) + old_bytes + 8192u > show3_part_end))
	{
		lk_check_unchecked++;
		dl_stall_held--;
		dl_stall_pass();
		return;
	}
	dl_gu_restore(&k->saved);
	dither_now = k->dither;
	blend_now = k->blend;
	psp_tex_kept_forget();
	lit_forget(2);
	pass_scale_u = pass_scale_v = 1.0f;
	/* the words the body wrote between the base bind and the passes, which
	the old form holds and the kept form, written after it, otherwise
	wouldn't: the base's slope and level mode (texture_levels_by_distance
	writes them as it works base_slope out). Without the check they sit in
	the list just before the first CALL */
	{
		int base_width = 0, base_height = 0;

		psp_tex_size(base_key, &base_width, &base_height);
		texture_levels_by_distance(stream->uv_per_unit, base_width, base_height, 1.0f, tex_bias);
	}
	level_kept_passes(k, stream, draw, kept_count, cut_count, base_key, lightmap_key, alpha_tested, base_slope);
	kept_end = gu_list->current;
	draws = dl_check_compare(k->from, old_end, old_end, kept_end, &differing, &what, &a_value, &b_value, &unsupported,
		&first_draw);
	if (differing)
	{
		if (what >= 0 && what < LK_WHAT && lk_check_what[what] < 65535)
			lk_check_what[what]++;
		if (!lk_check_first[0])
		{
			lk_check_first[0] = 0x10000u | (unsigned int)what;
			lk_check_first[1] = a_value;
			lk_check_first[2] = b_value;
			lk_check_first[3] = (unsigned int)k->making << 16 | (unsigned int)first_draw << 8 | (unsigned int)(draws & 0xFF);
		}
	}
	lk_checked++;
	lk_check_prims += (unsigned long)draws;
	lk_check_differ += (unsigned long)differing;
	lk_check_unsupported += (unsigned long)unsupported;
	/* the kept form in the old one's place: its words name nothing in the
	list, so they move as they are */
	memmove(k->from, old_end, (unsigned int)(kept_end - old_end) * 4u);
	gu_list->current = k->from + (kept_end - old_end);
	dl_stall_held--;
	dl_stall_pass();
}

static void level_kept_report(void)
{
	if (level_kept <= 0)
		return;
	platform_log("GE: levelkept: %lu level draws in the last 300 frames, %lu made and %lu CALLed (%lu blocks, %lu live words; largest prologue %lu words; %d of %d slots used at most); the old way: other %lu, cut corners %lu, a texture missing %lu, fenced %lu, store full %lu, a slot too small %lu; stamps failed by textures %lu, passes %lu, detail scale %lu, fog %lu, modes %lu, pspgu %lu, map %lu",
		lk_draws, lk_made, lk_called, lk_blocks_called, lk_live_words, lk_words_most, lk_used_most, dl_store_slots(),
		lk_refused[LKR_OTHER], lk_refused[LKR_CUT], lk_refused[LKR_MISSING], lk_refused[LKR_FENCED], lk_refused[LKR_FULL],
		lk_refused[LKR_ROOM], lk_stamp_failed[LKS_TEXTURES], lk_stamp_failed[LKS_PASSES], lk_stamp_failed[LKS_DETAIL_SCALE],
		lk_stamp_failed[LKS_FOG], lk_stamp_failed[LKS_MODES], lk_stamp_failed[LKS_CONTEXT], lk_stamp_failed[LKS_MAP]);
	if (level_kept_checking)
	{
		int top[3] = { -1, -1, -1 }, i, j;

		for (i = 0; i < LK_WHAT; i++)
			for (j = 0; j < 3; j++)
				if (lk_check_what[i] && (top[j] < 0 || lk_check_what[i] > lk_check_what[top[j]]))
				{
					if (j < 2)
						memmove(&top[j + 1], &top[j], (size_t)(2 - j) * sizeof(top[0]));
					top[j] = i;
					break;
				}
		platform_log("GE: levelkeptcheck: %lu draws checked (%lu draw commands), %lu differing, %lu unchecked, %lu with a command the check doesn't model; most often at %03x (%u), %03x (%u), %03x (%u); the first at %03x, old %06x, kept %06x (made %u, its draw %u of %u)",
			lk_checked, lk_check_prims, lk_check_differ, lk_check_unchecked, lk_check_unsupported,
			top[0] & 0xFFF, top[0] >= 0 ? lk_check_what[top[0]] : 0, top[1] & 0xFFF, top[1] >= 0 ? lk_check_what[top[1]] : 0,
			top[2] & 0xFFF, top[2] >= 0 ? lk_check_what[top[2]] : 0, lk_check_first[0] & 0xFFF, lk_check_first[1],
			lk_check_first[2], lk_check_first[3] >> 16, (lk_check_first[3] >> 8) & 0xFF, lk_check_first[3] & 0xFF);
		memset(lk_check_what, 0, sizeof(lk_check_what));
		memset(lk_check_first, 0, sizeof(lk_check_first));
	}
	lk_draws = lk_made = lk_called = lk_blocks_called = lk_live_words = 0;
	memset(lk_refused, 0, sizeof(lk_refused));
	memset(lk_stamp_failed, 0, sizeof(lk_stamp_failed));
	lk_checked = lk_check_prims = lk_check_differ = lk_check_unchecked = lk_check_unsupported = 0;
}

/* ---------- Tier 2, the kept geometry (docs/box_draw_list.md §12.1d (a), slice
L2; keep word "levelgeo", "levelkept2" with Tier 1): a level draw's slant
buckets and its sorted index copy kept between frames.

Per draw (keyed by the stream and base texture, its own entry and stamp) it
keeps each triangle's bucket in the engine's order, and per chunk of 8
triangles the kept set's byte and the buckets' slack: the room the eye has to
move before any of the chunk's kept triangles could cross a bucket's edge
(|d| sin of the angle to the nearer edge, less a margin for the floats) or its
far flag could change (the corners' distance from the detail fade). A chunk
whose kept set is the same and whose eye has moved less than its slack keeps
its buckets without a triangle looked at; the others are bucketed again by
angle_bucket_one, the same expressions as the slant sort. When no bucket and
no kept triangle changed, the sorted copy the GE read the frame before is
drawn again from the pool by address; else it's written again from the stored
buckets (a count and a write, no float work for the chunks that held) into
fresh pages, the old ones given back only DL_FENCE frames after their last use.

The list's identity is the main CPU's (psp_level_list_version,
psp_fixups.py's FIXES_DRAWLIST): the engine builds a material's list from its
surfaces' visible bits only, so an equal version is an equal list, and a draw
with no version keeps nothing. The classify writes a bit a triangle in place
of its copy of the kept triangles ("one copy"): the sorted copy is the draw's
only one. Exact: a held bucket is only reused while its slack proves it
can't have changed ("levelbucketmarginN" widens the edges by N% of a bucket:
0, exact, by default). "levelgeocheck" buckets and sorts each such draw the
old way again and compares, and draws the old form's GE words from that copy
for levelkeptcheck's comparison.

Its data: the entries, and the per-triangle buckets and chunk records (read
only by the CPU that writes the list), and the sorted copies' pages (read by
the GE), all carved from the display list's end beside Tier 1's store:
"levelgeokbN" the copies' pool (64 KB), "levelgeomekbN" the buckets' (32 KB).
No VFPU instruction and no system call: the ME runs it */

#define LG_ENTRIES 128
#define LG_STAMP 11
#define LG_RING 64
#define LG_CHUNK 8
#define LG_COPY_MOST 2048
#define LG_COPY_PAGE 512
#define LG_ME_PAGE 128
enum { LG_NONE, LG_COPY, LG_ENGINE };
enum { LGR_FULL, LGR_ME, LGR_UNLISTED, LGR_EYE, LGR_KINDS };

struct lg_chunk
{
	float bangle, bfar, far_d;
	unsigned short frame;
	unsigned char mask, state; /* state: 1 made, 2 with the far flag possible */
};
typedef char lg_chunk_is_16[sizeof(struct lg_chunk) == 16 ? 1 : -1];

struct lg_entry
{
	const void *stream; /* NULL: free */
	unsigned int key, last_frame, list_version, stamp[LG_STAMP];
	unsigned short *copy, *retired;
	unsigned int copy_bytes, retired_bytes, retired_frame;
	unsigned char *me; /* chunk records, then a bucket a triangle */
	unsigned int me_bytes;
	unsigned short totals[SORT_BUCKETS];
	unsigned char drawn, used;
};

static int level_geo = -1, lg_margin = 0;
static struct lg_entry *lg_table;
static struct dl_pool lg_copies, lg_me;
static unsigned int lg_ring_frame[LG_RING];
static float lg_ring_eye[LG_RING][3];
/* the bucket edges' cosines and sines, below and above (the margin widens them) */
static float lg_lo_c[ANGLE_BUCKETS - 1], lg_lo_s[ANGLE_BUCKETS - 1], lg_hi_c[ANGLE_BUCKETS - 1], lg_hi_s[ANGLE_BUCKETS - 1];
static unsigned char lg_bits[ANGLE_TRIANGLES / 8];
static unsigned long lg_draws, lg_whole, lg_written_pool, lg_written_list, lg_engine, lg_old, lg_refused[LGR_KINDS],
	lg_chunks, lg_chunks_held, lg_triangles, lg_triangles_held, lg_one_copy, lg_copies_peak_kb,
	lg_me_peak_kb, lg_checked, lg_check_buckets, lg_check_copy, lg_check_runs, lg_check_kept;

static int level_geo_on(void)
{
	if (level_geo < 0)
	{
		level_geo = psp_keep("levelgeo") || psp_keep("levelkept2") || psp_keep("levelgeocheck");
		level_kept_on();
	}
	return level_geo;
}

/* 2^x for the margin's edges, with no library call (the ME may be the one
that asks first) */
static float lg_exp2(float x)
{
	float term = 1.0f, sum = 1.0f, y = x * 0.69314718f;
	int k;

	for (k = 1; k < 12; k++)
	{
		term *= y / (float)k;
		sum += term;
	}
	return sum;
}

static void lg_edges(void)
{
	extern int psp_keep_number(const char *prefix, int fallback);
	float widen;
	int k;

	lg_margin = psp_keep_number("levelbucketmargin", 0);
	if (lg_margin < 0 || lg_margin > 100)
		lg_margin = 0;
	widen = lg_margin ? lg_exp2((float)lg_margin / 100.0f) : 1.0f;
	for (k = 0; k < ANGLE_BUCKETS - 1; k++)
	{
		/* bucket > k while 1 / cos^2 > f_k^2 sqrt 2: angle_bucket_one's product */
		float edge = angle_factor[k] * angle_factor[k] * 1.41421356f, lo = edge / widen, hi = edge * widen;

		lg_lo_c[k] = 1.0f / sqrtf(lo);
		lg_lo_s[k] = sqrtf(1.0f - 1.0f / lo);
		lg_hi_c[k] = 1.0f / sqrtf(hi);
		lg_hi_s[k] = sqrtf(1.0f - 1.0f / hi);
	}
}

static void level_geo_carve(void)
{
	extern int psp_keep_number(const char *prefix, int fallback);
	unsigned int copies, me, table, bytes;
	unsigned char *at;

	if (!level_geo_on() || !display_list || lg_table)
		return;
	copies = (unsigned int)psp_keep_number("levelgeokb", 64);
	copies = (copies < 16 ? 16 : copies > 256 ? 256 : copies) * 1024u;
	me = (unsigned int)psp_keep_number("levelgeomekb", 32);
	me = (me < 8 ? 8 : me > 64 ? 64 : me) * 1024u;
	table = ((unsigned int)LG_ENTRIES * sizeof(struct lg_entry) + 63) & ~63u;
	bytes = table + me + copies;
	if (bytes > (unsigned int)display_list_size / 4)
	{
		platform_log("GE: levelgeo: a %u KB store is more than a quarter of the %d KB list: off", bytes / 1024,
			display_list_size / 1024);
		level_geo = 0;
		return;
	}
	at = (unsigned char *)display_list + display_list_size - bytes;
	lg_table = (struct lg_entry *)at;
	memset(lg_table, 0, table);
	dl_pool_carve(&lg_me, at + table, me, LG_ME_PAGE);
	dl_pool_carve(&lg_copies, at + table + me, copies, LG_COPY_PAGE);
	sceKernelDcacheWritebackInvalidateRange(at, bytes);
	display_list_size -= (int)bytes;
	lg_edges();
	/* the edges, the pools' bounds and the table's place, as the CPU that
	replays (the ME) will read them */
	sceKernelDcacheWritebackAll();
	platform_log("GE: levelgeo%s: its store is the display list's last %u KB (the list now %d KB): %d entries, the buckets' pool %u KB, the copies' %u KB%s",
		level_geo_checking ? " (with its check)" : "", bytes / 1024, display_list_size / 1024, LG_ENTRIES, me / 1024,
		copies / 1024, lg_margin ? ", with a bucket margin" : "");
	if (lg_margin)
		platform_log("GE: levelgeo: levelbucketmargin%d: a held bucket may be up to %d%% of a bucket off its edge (not exact)",
			lg_margin, lg_margin);
}

/* whether this draw goes the kept geometry's way: decided before the
classify, which then writes a bit a triangle. The slant sort's own
conditions, and the ones its store can't follow (the ME's slant split's
part is worked out here instead: the same function, the same bits) */
static int level_geo_wanted(long triangle_count, unsigned int list_version)
{
	if (!level_geo_on() || !lg_table)
		return 0;
	if (angle_buckets < 0)
	{
		angle_buckets = !psp_keep("noangle");
		detail_all_far = psp_keep("detailallfar");
	}
	if (mip_slope < 0)
		mip_slope = !psp_keep("mipauto");
	if (!angle_buckets || !mip_slope || tex_level_mode == GU_TEXTURE_CONST || triangle_count <= 0 ||
		triangle_count > ANGLE_TRIANGLES || show2_copy_indices || psp_pass_mode)
		return 0;
	if (!list_version)
	{
		lg_refused[LGR_UNLISTED]++;
		return 0;
	}
	return 1;
}

static void lg_free_data(struct lg_entry *e)
{
	dl_pool_give(&lg_me, e->me, e->me_bytes);
	e->me = NULL;
	e->me_bytes = 0;
}

/* the copies' pages given back: the retired ones past the fence, and with
all a free or evicted entry's (only past the fence: its last draw's frame) */
static void lg_free_copies(struct lg_entry *e, unsigned int frame, int all)
{
	if (e->retired && frame - e->retired_frame >= DL_FENCE)
	{
		dl_pool_give(&lg_copies, e->retired, e->retired_bytes);
		e->retired = NULL;
	}
	if (all && e->copy)
	{
		dl_pool_give(&lg_copies, e->copy, e->copy_bytes);
		e->copy = NULL;
		e->drawn = LG_NONE;
	}
}

/* room in a pool: the pages of the entries drawn longest ago, past the fence */
static int lg_reclaim(struct dl_pool *pool, unsigned int frame, const struct lg_entry *keep)
{
	struct lg_entry *oldest = NULL;
	int i;

	for (i = 0; i < LG_ENTRIES; i++)
	{
		struct lg_entry *e = &lg_table[i];

		if (pool == &lg_copies)
			lg_free_copies(e, frame, 0);
		if (e == keep || !e->stream || frame - e->last_frame < DL_FENCE ||
			!(pool == &lg_copies ? e->copy != NULL : e->me != NULL))
			continue;
		if (!oldest || e->last_frame - oldest->last_frame > 0x80000000u)
			oldest = e;
	}
	if (!oldest)
		return 0;
	if (pool == &lg_copies)
		lg_free_copies(oldest, frame, 1);
	else
	{
		lg_free_data(oldest);
		oldest->list_version = 0;
	}
	return 1;
}

static void *lg_take(struct dl_pool *pool, unsigned int bytes, unsigned int frame, const struct lg_entry *keep)
{
	void *at;
	int tries;

	for (tries = 0; tries < 8; tries++)
	{
		if ((at = dl_pool_take(pool, bytes)) != NULL)
			return at;
		if (!lg_reclaim(pool, frame, keep))
			break;
	}
	return NULL;
}

static int lg_window(const void *stream, unsigned int key)
{
	unsigned int h = ((unsigned int)stream >> 4) * 2654435761u ^ key * 40503u;

	return (int)((h >> 8) % LG_ENTRIES);
}

/* the entry for (stream, key): the stream's hint, then a window of DL_PROBE;
taking a free one, or the one drawn longest ago past the fence, when none */
static struct lg_entry *lg_entry_for(struct level_stream *stream, unsigned int key, unsigned int frame)
{
	struct lg_entry *victim = NULL;
	int i, at, hint = stream->geo_hint;

	if (hint >= 0 && hint < LG_ENTRIES && lg_table[hint].stream == stream && lg_table[hint].key == key)
		return &lg_table[hint];
	for (i = 0, at = lg_window(stream, key); i < DL_PROBE; i++, at = at + 1 < LG_ENTRIES ? at + 1 : 0)
	{
		struct lg_entry *e = &lg_table[at];

		if (e->stream == stream && e->key == key)
		{
			stream->geo_hint = (short)at;
			return e;
		}
	}
	for (i = 0, at = lg_window(stream, key); i < DL_PROBE; i++, at = at + 1 < LG_ENTRIES ? at + 1 : 0)
	{
		struct lg_entry *e = &lg_table[at];

		if (!e->stream)
		{
			victim = e;
			break;
		}
		if (frame - e->last_frame >= DL_FENCE && (!victim || e->last_frame - victim->last_frame > 0x80000000u))
			victim = e;
	}
	if (!victim)
		return NULL;
	if (victim->stream)
	{
		lg_free_data(victim);
		lg_free_copies(victim, frame, 1);
		if (victim->retired)
		{
			/* its last draw was past the fence, and so was the retiring */
			dl_pool_give(&lg_copies, victim->retired, victim->retired_bytes);
			victim->retired = NULL;
		}
	}
	memset(victim, 0, sizeof(*victim));
	victim->stream = stream;
	victim->key = key;
	stream->geo_hint = (short)(victim - lg_table);
	return victim;
}

/* the eye in the world at frame, as kept in the ring (0 when it's gone) */
static const float *lg_eye_at(unsigned short frame16, unsigned int frame)
{
	unsigned int then = frame - (unsigned short)(frame - frame16);
	int at = (int)(then & (LG_RING - 1));

	if (frame - then >= LG_RING || lg_ring_frame[at] != then)
		return NULL;
	return lg_ring_eye[at];
}

/* one kept triangle's buckets' slack in the stream's frame: |d| sin of the
angle to the nearer edge of its bucket (d the eye to its centre, n its
normal; the bucket is the angle between them), worked out from the angle's
cosine and sine with the edges' (no library call), less 2e-4 of a radian's
sine for the floats; and its corners' distance from the detail fade */
static void lg_slack(const struct ge_vertex *base, const unsigned short *triangle, int bucket, int far_possible,
	float far_d, float *bangle, float *bfar)
{
	const struct ge_vertex *a = &base[triangle[0]], *p = &base[triangle[1]], *q = &base[triangle[2]];
	float ex = level_eye[0], ey = level_eye[1], ez = level_eye[2];
	float e1[3] = { p->x - a->x, p->y - a->y, p->z - a->z }, e2[3] = { q->x - a->x, q->y - a->y, q->z - a->z };
	float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
	float d[3] = { a->x - ex + (e1[0] + e2[0]) * (1.0f / 3.0f), a->y - ey + (e1[1] + e2[1]) * (1.0f / 3.0f),
		a->z - ez + (e1[2] + e2[2]) * (1.0f / 3.0f) };
	float nn = n[0] * n[0] + n[1] * n[1] + n[2] * n[2], dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
	int b = bucket % ANGLE_BUCKETS;

	if (nn <= 0.0f)
		*bangle = 1e30f; /* no normal: bucket 0 wherever the eye is */
	else if (dd <= 0.0f)
		*bangle = 0.0f;
	else
	{
		float dot = n[0] * d[0] + n[1] * d[1] + n[2] * d[2];
		float x[3] = { n[1] * d[2] - n[2] * d[1], n[2] * d[0] - n[0] * d[2], n[0] * d[1] - n[1] * d[0] };
		float inverse = 1.0f / sqrtf(nn * dd), c = fabsf(dot) * inverse;
		float s = sqrtf(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]) * inverse, room = 1.0f, r;

		if (b > 0 && (r = s * lg_lo_c[b - 1] - c * lg_lo_s[b - 1]) < room)
			room = r;
		if (b < ANGLE_BUCKETS - 1 && (r = lg_hi_s[b] * c - lg_hi_c[b] * s) < room)
			room = r;
		room -= 2e-4f;
		*bangle = room <= 0.0f ? 0.0f : sqrtf(dd) * room;
	}
	*bfar = 1e30f;
	if (far_possible)
	{
		const struct ge_vertex *corner[3] = { a, p, q };
		int k;

		for (k = 0; k < 3; k++)
		{
			float cx = corner[k]->x - ex, cy = corner[k]->y - ey, cz = corner[k]->z - ez;
			float gap = fabsf(sqrtf(cx * cx + cy * cy + cz * cz) - far_d);

			if (gap < *bfar)
				*bfar = gap;
		}
		*bfar -= 1e-4f + far_d * 1e-6f;
		if (*bfar < 0.0f)
			*bfar = 0.0f;
	}
}

/* angle_sort's runs and counts from a draw's bucket totals */
static void lg_runs(const unsigned short *totals)
{
	long first = 0;
	int b, used = 0;

	angle_near_any = 0;
	for (b = 0; b < SORT_BUCKETS; b++)
	{
		angle_first[b] = first;
		angle_count[b] = totals[b];
		angle_triangles[b % ANGLE_BUCKETS] += totals[b];
		if (b >= ANGLE_BUCKETS)
			angle_far_triangles += totals[b];
		else if (totals[b])
			angle_near_any = 1;
		first += totals[b];
		used += totals[b] != 0;
	}
	if (used <= 1)
		for (b = 0; b < SORT_BUCKETS; b++)
			angle_first[b] = 0;
	angle_bucketed = 1;
	angle_draws++;
}

static int lg_kept(const unsigned char *bits, long t)
{
	return !bits || (bits[t >> 3] >> (t & 7)) & 1;
}

/* the draw's kept triangles in the engine's order, as the classify's copy
was, written into the list */
static unsigned short *lg_kept_copy(const unsigned short *indices, long triangle_count, const unsigned char *bits,
	long kept_count)
{
	unsigned short *copy = (unsigned short *)list_take((int)(triangle_count * 3 * sizeof(unsigned short) + 3) & ~3);
	long t, at = 0;

	if (!copy)
		return NULL;
	for (t = 0; t < triangle_count; t++)
		if (lg_kept(bits, t))
		{
			copy[3 * at] = indices[3 * t];
			copy[3 * at + 1] = indices[3 * t + 1];
			copy[3 * at + 2] = indices[3 * t + 2];
			at++;
		}
	(void)kept_count;
	return copy;
}

/* the sorted copy, from the stored buckets: one count, one write */
static void lg_write(unsigned short *out, const unsigned short *indices, long triangle_count, const unsigned char *bits,
	const unsigned char *buckets, const unsigned short *totals)
{
	long place[SORT_BUCKETS], first = 0, t;
	int b;

	for (b = 0; b < SORT_BUCKETS; b++)
	{
		place[b] = first;
		first += totals[b];
	}
	for (t = 0; t < triangle_count; t++)
		if (lg_kept(bits, t))
		{
			long at = place[buckets[t]]++;

			out[3 * at] = indices[3 * t];
			out[3 * at + 1] = indices[3 * t + 1];
			out[3 * at + 2] = indices[3 * t + 2];
		}
}

static void lg_check(const struct level_stream *stream, const unsigned short *indices, long triangle_count,
	const unsigned char *bits, long kept_count, int far_possible, const unsigned char *buckets,
	const unsigned short *drawn, const unsigned short **reference);

/* the draw's list to draw, its slant runs set as angle_sort sets them.
indices: the engine's list (after the bake's pieces); bits: the classify's
kept set, a bit a triangle (NULL: every triangle kept); dropped: some
triangle wasn't kept. *reference: with levelgeocheck, the old way's copy in
the list, for the old form's passes */
static const unsigned short *level_geo_sort(struct level_stream *stream, unsigned int base_key,
	const unsigned short *indices, long triangle_count, const unsigned char *bits, int dropped, long kept_count,
	int far_possible, unsigned int lowest, unsigned int highest, unsigned int list_version,
	const unsigned short **reference)
{
	unsigned int frame = ge_hot.stream_frame, stamp[LG_STAMP], need;
	struct lg_entry *e;
	struct lg_chunk *chunks;
	unsigned char *buckets;
	unsigned short totals[SORT_BUCKETS];
	long chunk_count = (triangle_count + LG_CHUNK - 1) / LG_CHUNK, j, t;
	int fp = detail_all_far ? 0 : far_possible, changed, used, b, valid, k, last_frame_seen = -1;
	float far_d = fp ? sqrtf(detail_far_squared) : 0.0f, m = 0.0f;
	const unsigned short *drawn;

	*reference = NULL;
	lg_draws++;
	if (kept_count <= 0)
		goto old_way;
	/* the eye of each frame, for the chunks' moves */
	k = (int)(frame & (LG_RING - 1));
	if (lg_ring_frame[k] != frame)
	{
		lg_ring_frame[k] = frame;
		lg_ring_eye[k][0] = camera_eye[0];
		lg_ring_eye[k][1] = camera_eye[1];
		lg_ring_eye[k][2] = camera_eye[2];
	}
	else if (lg_ring_eye[k][0] != camera_eye[0] || lg_ring_eye[k][1] != camera_eye[1] || lg_ring_eye[k][2] != camera_eye[2])
	{
		/* a second eye in one frame: the ring names one eye a frame, and a
		chunk's move is measured from its frame's */
		lg_refused[LGR_EYE]++;
		goto old_way;
	}
	stamp[0] = (unsigned int)stream->source;
	stamp[1] = (unsigned int)stream->count;
	stamp[2] = (unsigned int)stream->original_count;
	stamp[3] = (unsigned int)stream->bake;
	stamp[4] = lk_bits(stream->origin[0]);
	stamp[5] = lk_bits(stream->origin[1]);
	stamp[6] = lk_bits(stream->origin[2]);
	stamp[7] = (unsigned int)tex_level_mode;
	stamp[8] = (unsigned int)triangle_count;
	stamp[9] = (unsigned int)psp_tex_map_opens;
	stamp[10] = (unsigned int)stencil_mode;
	e = lg_entry_for(stream, base_key, frame);
	if (!e)
	{
		lg_refused[LGR_FULL]++;
		goto old_way;
	}
	lg_free_copies(e, frame, 0);
	valid = e->me && e->list_version == list_version && !memcmp(e->stamp, stamp, sizeof(stamp));
	need = (unsigned int)(chunk_count * sizeof(struct lg_chunk) + triangle_count);
	if (!valid)
	{
		/* a new list or stream: nothing of the old one holds */
		if (!e->me || e->me_bytes < need || e->me_bytes > 2 * need + 1024)
		{
			lg_free_data(e);
			e->me = lg_take(&lg_me, need, frame, e);
			if (!e->me)
			{
				e->list_version = 0;
				e->last_frame = frame;
				lg_refused[LGR_ME]++;
				goto old_way;
			}
			e->me_bytes = need;
		}
		memset(e->me, 0, chunk_count * sizeof(struct lg_chunk));
		memcpy(e->stamp, stamp, sizeof(stamp));
		e->list_version = list_version;
		lg_free_copies(e, frame, 0);
		if (e->copy && !e->retired)
		{
			e->retired = e->copy;
			e->retired_bytes = e->copy_bytes;
			e->retired_frame = frame;
			e->copy = NULL;
		}
		e->drawn = LG_NONE;
	}
	e->last_frame = frame;
	chunks = (struct lg_chunk *)e->me;
	buckets = e->me + chunk_count * sizeof(struct lg_chunk);
	changed = e->drawn == LG_NONE;
	lg_triangles += (unsigned long)triangle_count;
	for (j = 0; j < chunk_count; j++)
	{
		struct lg_chunk *c = &chunks[j];
		long first = j * LG_CHUNK, end = first + LG_CHUNK < triangle_count ? first + LG_CHUNK : triangle_count;
		unsigned int mask = bits ? bits[j] : 0xFFu;
		int held = 0;

		if (end - first < LG_CHUNK)
			mask &= (1u << (end - first)) - 1u;
		lg_chunks++;
		if ((c->state & 1) && c->mask == mask && ((c->state >> 1) & 1) == (unsigned int)fp)
		{
			if (!mask)
				held = 1;
			else
			{
				if (last_frame_seen != (int)c->frame)
				{
					const float *then = lg_eye_at(c->frame, frame);

					last_frame_seen = (int)c->frame;
					if (then)
					{
						float dx = camera_eye[0] - then[0], dy = camera_eye[1] - then[1], dz = camera_eye[2] - then[2];

						m = sqrtf(dx * dx + dy * dy + dz * dz);
					}
					else
						m = 1e30f;
				}
				held = m + 1e-4f < c->bangle && m + fabsf(far_d - c->far_d) + 1e-4f < c->bfar;
			}
		}
		if (held)
		{
			lg_chunks_held++;
			lg_triangles_held += (unsigned long)(end - first);
			continue;
		}
		/* bucketed again, from this frame */
		{
			float bangle = 1e30f, bfar = 1e30f;

			for (t = first; t < end; t++)
			{
				float sa, sf;
				int bucket;

				if (!((mask >> (t - first)) & 1u))
					continue;
				bucket = angle_bucket_one(stream->base, indices + 3 * t, level_eye[0], level_eye[1], level_eye[2], fp,
					detail_far_squared);
				if (!(c->state & 1) || c->mask != mask || buckets[t] != bucket)
					changed = 1;
				buckets[t] = (unsigned char)bucket;
				lg_slack(stream->base, indices + 3 * t, bucket, fp, far_d, &sa, &sf);
				if (sa < bangle)
					bangle = sa;
				if (sf < bfar)
					bfar = sf;
			}
			if (!(c->state & 1) || c->mask != mask)
				changed = 1;
			c->bangle = bangle;
			c->bfar = bfar;
			c->far_d = far_d;
			c->frame = (unsigned short)frame;
			c->mask = (unsigned char)mask;
			c->state = (unsigned char)(1 | fp << 1);
		}
	}
	if (!changed)
	{
		/* the same kept set and buckets as the copy held: drawn as it is */
		lg_whole++;
		lg_runs(e->totals);
		if (e->drawn == LG_ENGINE)
		{
			ge_writeback(indices, triangle_count * 3 * sizeof(unsigned short));
			drawn = indices;
		}
		else
			drawn = e->copy;
		goto done;
	}
	memset(totals, 0, sizeof(totals));
	for (t = 0; t < triangle_count; t++)
		if (lg_kept(bits, t))
			totals[buckets[t]]++;
	for (b = used = 0; b < SORT_BUCKETS; b++)
		used += totals[b] != 0;
	memcpy(e->totals, totals, sizeof(totals));
	/* the copy this one replaces, given back past the fence */
	lg_free_copies(e, frame, 0);
	if (e->copy && !e->retired)
	{
		e->retired = e->copy;
		e->retired_bytes = e->copy_bytes;
		e->retired_frame = frame;
		e->copy = NULL;
	}
	e->drawn = LG_NONE;
	if (used <= 1 && !dropped)
	{
		/* one bucket and nothing left out: the engine's own list, as the slant sort leaves it */
		ge_writeback(indices, triangle_count * 3 * sizeof(unsigned short));
		drawn = indices;
		e->drawn = LG_ENGINE;
		lg_engine++;
	}
	else
	{
		unsigned int bytes = (unsigned int)kept_count * 3 * sizeof(unsigned short);
		unsigned short *out = NULL;

		if (!e->copy && kept_count <= LG_COPY_MOST)
			out = lg_take(&lg_copies, bytes, frame, e);
		if (out)
		{
			lg_write(out, indices, triangle_count, bits, buckets, totals);
			dl_writeback(out, bytes);
			e->copy = out;
			e->copy_bytes = bytes;
			e->drawn = LG_COPY;
			lg_written_pool++;
		}
		else
		{
			/* the pool full, the old copy not yet past the fence, or a large
			draw: the copy in the list, as the slant sort writes it */
			out = (unsigned short *)list_take((int)(bytes + 3) & ~3);
			if (!out)
				goto old_way;
			lg_write(out, indices, triangle_count, bits, buckets, totals);
			lg_written_list++;
		}
		drawn = out;
	}
	lg_runs(totals);
done:
	if (dropped)
		lg_one_copy++;
	if (lg_copies.used * LG_COPY_PAGE / 1024 > lg_copies_peak_kb)
		lg_copies_peak_kb = lg_copies.used * LG_COPY_PAGE / 1024;
	if (lg_me.used * LG_ME_PAGE / 1024 > lg_me_peak_kb)
		lg_me_peak_kb = lg_me.used * LG_ME_PAGE / 1024;
	if (level_geo_checking)
		lg_check(stream, indices, triangle_count, bits, kept_count, fp, buckets, drawn, reference);
	return drawn;

old_way:
	/* as the old path: the classify's copy (made now from the bits) or the
	engine's list written back, then the slant sort */
	lg_old++;
	if (dropped)
	{
		unsigned short *copy = lg_kept_copy(indices, triangle_count, bits, kept_count);

		if (!copy)
			return NULL;
		return angle_sort(stream->base, copy, kept_count, far_possible, lowest, highest);
	}
	ge_writeback(indices, triangle_count * 3 * sizeof(unsigned short));
	return angle_sort(stream->base, indices, kept_count, far_possible, lowest, highest);
}

/* levelgeocheck: the draw bucketed and sorted the old way again, from
nothing kept, and compared with what the kept geometry drew: each kept
triangle's bucket, the sorted copy by content, the runs; and the classify's
bits against the triangles' codes worked out afresh (the one copy). The old
way's copy goes into the list for the old form's passes, so levelkeptcheck
compares the GE's words of the two forms too */
static void lg_check(const struct level_stream *stream, const unsigned short *indices, long triangle_count,
	const unsigned char *bits, long kept_count, int far_possible, const unsigned char *buckets,
	const unsigned short *drawn, const unsigned short **reference)
{
	unsigned short totals[SORT_BUCKETS];
	unsigned short *copy;
	long t, kept = 0, place[SORT_BUCKETS], first = 0;
	int b, buckets_differ = 0, used = 0;

	lg_checked++;
	memset(totals, 0, sizeof(totals));
	for (t = 0; t < triangle_count; t++)
	{
		int bucket;

		if (bits)
		{
			/* the triangle's own codes, not the classify's memo of each vertex's */
			const unsigned short *triangle = indices + 3 * t;
			int corner, all = 0;

			for (corner = 0; corner < 3; corner++)
			{
				float point[3];

				level_to_camera(&stream->base[triangle[corner]].x, point);
				all |= outside(point) | far_cut_code(point[2]);
			}
			if (!(all & CODE_PLANES) && (all & CODE_FAR) != CODE_FAR ? !lg_kept(bits, t) : lg_kept(bits, t))
				lg_check_kept++;
		}
		if (!lg_kept(bits, t))
			continue;
		kept++;
		bucket = angle_bucket_one(stream->base, indices + 3 * t, level_eye[0], level_eye[1], level_eye[2], far_possible,
			detail_far_squared);
		if (lg_margin == 0 && bucket != buckets[t])
			buckets_differ++;
		totals[bucket]++;
	}
	if (kept != kept_count)
		lg_check_kept++;
	lg_check_buckets += (unsigned long)buckets_differ;
	for (b = 0; b < SORT_BUCKETS; b++)
	{
		used += totals[b] != 0;
		place[b] = first;
		first += totals[b];
		if (angle_count[b] != totals[b])
			lg_check_runs++;
	}
	copy = (unsigned short *)list_take((int)(kept * 3 * sizeof(unsigned short) + 3) & ~3);
	if (!copy)
		return;
	for (t = 0; t < triangle_count; t++)
		if (lg_kept(bits, t))
		{
			long at = place[angle_bucket_one(stream->base, indices + 3 * t, level_eye[0], level_eye[1], level_eye[2],
				far_possible, detail_far_squared)]++;

			copy[3 * at] = indices[3 * t];
			copy[3 * at + 1] = indices[3 * t + 1];
			copy[3 * at + 2] = indices[3 * t + 2];
		}
	if (lg_margin == 0 && memcmp(copy, drawn, (size_t)kept * 3 * sizeof(unsigned short)))
		lg_check_copy++;
	for (b = 0; b < SORT_BUCKETS && used > 1; b++)
		if (angle_first[b] != place[b] - totals[b])
			lg_check_runs++;
	*reference = copy;
}

static void level_geo_report(void)
{
	int i, used = 0;

	if (level_geo <= 0 || !lg_table)
		return;
	for (i = 0; i < LG_ENTRIES; i++)
		used += lg_table[i].stream != NULL;
	platform_log("GE: levelgeo: %lu draws in the last 300 frames: %lu drawn whole from the copy held, %lu copies written to the pool and %lu to the list, %lu the engine's list, %lu the old way (no entry free %lu, the buckets' pool full %lu, no list version %lu, a second eye %lu); %lu of %lu chunks held (%lu of %lu triangles); %lu with something left out (one copy); entries %d of %d; the copies' pool %u KB used, %lu KB at most (%u refused), the buckets' %u KB, %lu at most (%u refused)",
		lg_draws, lg_whole, lg_written_pool, lg_written_list, lg_engine, lg_old, lg_refused[LGR_FULL], lg_refused[LGR_ME],
		lg_refused[LGR_UNLISTED], lg_refused[LGR_EYE], lg_chunks_held, lg_chunks, lg_triangles_held, lg_triangles, lg_one_copy, used,
		LG_ENTRIES, lg_copies.used * LG_COPY_PAGE / 1024, lg_copies_peak_kb, lg_copies.refused,
		lg_me.used * LG_ME_PAGE / 1024, lg_me_peak_kb, lg_me.refused);
	if (level_geo_checking)
		platform_log("GE: levelgeocheck: %lu draws checked: %lu buckets differing, %lu copies differing, %lu runs differing, %lu kept sets differing",
			lg_checked, lg_check_buckets, lg_check_copy, lg_check_runs, lg_check_kept);
	lg_draws = lg_whole = lg_written_pool = lg_written_list = lg_engine = lg_old = 0;
	memset(lg_refused, 0, sizeof(lg_refused));
	lg_chunks = lg_chunks_held = lg_triangles = lg_triangles_held = lg_one_copy = 0;
	lg_copies_peak_kb = lg_me_peak_kb = 0;
	lg_copies.refused = lg_me.refused = 0;
	lg_checked = lg_check_buckets = lg_check_copy = lg_check_runs = lg_check_kept = 0;
}
