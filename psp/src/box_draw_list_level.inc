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

unsigned int psp_tex_kept_touch(unsigned int key, int mark, int *has_alpha);
void psp_tex_kept_forget(void);
extern int psp_tex_standin;
extern PSP_MAIN_SIDE unsigned long psp_tex_map_opens;
extern int gu_curr_context, gu_object_stack_depth;

/* why a draw went the old way, or why its stamp failed */
enum { LKR_OTHER, LKR_CUT, LKR_MISSING, LKR_FENCED, LKR_FULL, LKR_KINDS };
enum { LKS_TEXTURES, LKS_PASSES, LKS_DETAIL_SCALE, LKS_FOG, LKS_MODES, LKS_CONTEXT, LKS_MAP, LKS_KINDS };
/* a slot holds a draw's blocks: at most ~1,380 bytes for base, lightmap, two
detail maps and two glow maps with eight levels each, so 2 KB never overflows */
#define LK_SLOT_LEAST 2048

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

static int level_kept_on(void)
{
	if (level_kept < 0)
	{
		level_kept_checking = psp_keep("levelkeptcheck") != 0;
		level_kept = level_kept_checking || psp_keep("levelkept");
		lk_bindblocks = psp_keep("bindblocks") != 0;
	}
	return level_kept;
}

/* the store, taken from the display list's end as the GE starts: the words
it holds are words the list would have held, and the list's fullest frame
must still leave 64 KB beside it on every map; "levelkeptkbN" its size,
"levelkeptslotN" a slot's bytes */
static void level_kept_carve(void)
{
	extern int psp_keep_number(const char *prefix, int fallback);
	unsigned int bytes, slot;
	int count;

	if (!level_kept_on() || !display_list || dl_store_slots())
		return;
	bytes = (unsigned int)psp_keep_number("levelkeptkb", 64);
	bytes = (bytes < 16 ? 16 : bytes > 256 ? 256 : bytes) * 1024u;
	slot = (unsigned int)psp_keep_number("levelkeptslot", LK_SLOT_LEAST);
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

/* every input a prologue's words are made from; 0 when a texture the passes
bind is in the pack but not resident (the old path's bind asks for it) */
static int lk_stamp_make(unsigned int *s, const struct level_stream *stream, unsigned int base_key,
	unsigned int lightmap_key, int alpha_tested, int detail_runs, int *base_alpha)
{
	unsigned int a;
	int k, n = 0, alpha = 0;

	a = psp_tex_kept_touch(base_key, 0, &alpha);
	if (a <= 1)
		return 0;
	*base_alpha = alpha;
	s[n++] = base_key;
	s[n++] = a;
	a = stream->lightmap ? psp_tex_kept_touch(lightmap_key, 0, NULL) : 0;
	if (a == 1)
		return 0;
	s[n++] = a ? lightmap_key : 0;
	s[n++] = a;
	for (k = 0; k < 2; k++)
	{
		a = detail_runs && detail_key[k] ? psp_tex_kept_touch(detail_key[k], 0, NULL) : 0;
		if (a == 1)
			return 0;
		s[n++] = a ? detail_key[k] : 0;
		s[n++] = a;
	}
	for (k = 0; k < 2; k++)
	{
		a = !level_glow_off && level_glow_color[k] ? psp_tex_kept_touch(level_glow_key[k], 0, NULL) : 0;
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

/* the decision, at the draw's base bind: whether its passes go the kept way
(LK_KEPT), both ways for the check (LK_CHECK), or as before (LK_OFF) */
static int level_kept_mode(struct lk_now *k, const struct level_stream *stream, unsigned int base_key,
	unsigned int lightmap_key, int alpha_tested, int cut_count)
{
	int failed;

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
	if (!lk_stamp_make(k->stamp, stream, base_key, lightmap_key, alpha_tested, k->detail_runs, &k->base_alpha))
	{
		lk_refused[LKR_MISSING]++;
		return LK_OFF;
	}
	k->entry = dl_entry_get(stream, base_key, k->stamp, ge_hot.stream_frame, &k->making, &failed);
	if (failed >= 0)
		lk_stamp_failed[lk_stamp_kind(failed)]++;
	if (k->entry < 0)
	{
		lk_refused[k->entry == DL_FENCED ? LKR_FENCED : LKR_FULL]++;
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

/* each texture the prologue binds, marked used as its bind would mark it */
static void lk_touch(const unsigned int *s)
{
	int k;

	for (k = 0; k < 12; k += 2)
		if (s[k + 1] > 1)
			psp_tex_kept_touch(s[k], 1, NULL);
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
		/* a block sets every state it uses: the memos name nothing, and the
		dither is written by the first blend */
		psp_tex_kept_forget();
		lit_forget(2);
		dither_now = -1;
		lk_made++;
	}
	else
	{
		lk_touch(s);
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
	platform_log("GE: levelkept: %lu level draws in the last 300 frames, %lu made and %lu CALLed (%lu blocks, %lu live words; largest prologue %lu words; %d of %d slots used at most); the old way: other %lu, cut corners %lu, a texture missing %lu, fenced %lu, store full %lu; stamps failed by textures %lu, passes %lu, detail scale %lu, fog %lu, modes %lu, pspgu %lu, map %lu",
		lk_draws, lk_made, lk_called, lk_blocks_called, lk_live_words, lk_words_most, lk_used_most, dl_store_slots(),
		lk_refused[LKR_OTHER], lk_refused[LKR_CUT], lk_refused[LKR_MISSING], lk_refused[LKR_FENCED], lk_refused[LKR_FULL],
		lk_stamp_failed[LKS_TEXTURES], lk_stamp_failed[LKS_PASSES], lk_stamp_failed[LKS_DETAIL_SCALE],
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
