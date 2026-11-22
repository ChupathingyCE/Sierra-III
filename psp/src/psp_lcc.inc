/* levelchunkcount (recon, keep word "levelchunkcount"; docs/box_draw_list.md
§12.1d (g) step 1): what Tiers 2 and 3 would hold, counted per chunk of 8, 16
and 32 triangles in the engine's list order, before either is built. Nothing
it works out reaches the picture or the game: it reads the draw's triangles
again after the classify and after the slant sort, and keeps its own records.

Per level draw it keeps an entry (keyed by the stream and base texture, as the
box's entries are) with a hash of the engine's index list, and per chunk the
records Tiers 2 and 3 would keep:
	- the view verdict (all kept, all dropped behind one plane, or mixed), its
	  slack (the nearest corner's distance from a plane the verdict rests on,
	  each plane's normal made unit), the farthest corner's distance r, and
	  the frame whose camera it was made with;
	- the buckets' slack (|d| sin of the angle to the nearer bucket edge, and
	  the corners' distance from DETAIL_FAR when the far flag is possible),
	  the far distance it was made at, and the frame;
	- each triangle's bucket as made.
Each frame a chunk's verdict is held when the list is the stored one and
m + r f + dnear + dfar + eps < slack (m the eye's move, f the Frobenius norm
of the view basis's change, both since the chunk's frame); its buckets when
m + eps < angle slack and m + dD + eps < far slack. A held chunk is checked
against this frame's own answer (a violation means the bound isn't safe); a
chunk that fails is made again from this frame. The ceiling beside each share
is the same answer as made, by any means.

The copies: the bytes the classify reserves and copies, the slant sort's copy,
the write-back of an engine list drawn as it is, and the cut corners, a frame;
and the pool a store of the sorted copies would need (each drawn entry's copy
in 512-byte pages, plus the copies replaced within the fence).

Emulator only: it calls acosf and sinf and logs from the frame's end. */

#define LCC_ENTRIES 256
#define LCC_TRIS 16384
#define LCC_MAX ANGLE_TRIANGLES
#define LCC_RING 256
#define LCC_WINDOW 100
#define LCC_SIZES 3
#define LCC_KEPT 0
#define LCC_DROPPED 1
#define LCC_MIXED 2

struct lcc_cam
{
	unsigned int frame;
	float eye[3], basis[9], guard_x, guard_y, near, far_at;
	int far_active;
};

struct lcc_chunk
{
	float vslack, r, bangle, bfar, far_d;
	unsigned short vframe, bframe;
	unsigned char verdict, plane, far_possible, made;
};

struct lcc_entry
{
	const void *stream;
	unsigned int key, last_frame, list_hash, kept_hash, bucket_hash;
	int count, at, cap, copy_bytes, used;
};

/* one 8-triangle chunk of the draw under way, as this frame sees it */
struct lcc_agg
{
	float kmin, out[6], r, bangle, bfar;
	int all_kept, any_cut, triangles, kept;
	unsigned int and_code;
};

struct lcc_table
{
	struct lcc_entry entry[LCC_ENTRIES];
	struct lcc_cam ring[LCC_RING];
	struct lcc_chunk c8[LCC_TRIS / 8], c16[LCC_TRIS / 16], c32[LCC_TRIS / 32];
	unsigned char b[LCC_SIZES][LCC_TRIS];
	struct lcc_agg agg[LCC_MAX / 8];
	unsigned char state[LCC_MAX], bucket[LCC_MAX];
};

/* a window's counts; [size][0 inside draws, 1 classified] */
struct lcc_counts
{
	unsigned long frames, draws, draws_inside, draws_skipped, triangles_skipped, triangles, kept;
	unsigned long list_stored_draws, list_stored_tris, list_last_draws, list_last_tris, drawn_last_draws;
	unsigned long v_tris[LCC_SIZES][2], v_held[LCC_SIZES][2], v_same[LCC_SIZES][2], v_viol[LCC_SIZES][2];
	unsigned long v_kept_held[LCC_SIZES], v_dropped_held[LCC_SIZES];
	unsigned long b_tris[LCC_SIZES], b_held[LCC_SIZES], b_same[LCC_SIZES], b_viol[LCC_SIZES], b_kept_held[LCC_SIZES],
		b_kept[LCC_SIZES];
	unsigned long bucketed_draws, copy_same_draws, copy_draws;
	unsigned long reserve_bytes, classify_bytes, sort_bytes, writeback_bytes, cut_bytes, show2_bytes;
	unsigned long store_need_sum, store_need_peak, store_live_sum, store_fit16, store_fit32, store_fit64, store_big_draws;
	unsigned long compactions, wipes;
	unsigned int first_frame, last_frame;
};

static int lcc_on = -1;
static struct lcc_table *lcc;
static struct lcc_counts lcc_now, lcc_ready;
static int lcc_ready_full = 0;
static unsigned int lcc_frame = 0xFFFFFFFFu;
static int lcc_entries_used = 0, lcc_pool_used = 0;
static unsigned long lcc_replaced[2]; /* copies replaced this frame and the frame before, bytes */
static unsigned long lcc_store_peak_run = 0;
static float lcc_theta[ANGLE_BUCKETS - 1];
static struct lcc_entry *lcc_cur;
static int lcc_cur_inside, lcc_cur_same_list;
static unsigned int lcc_cur_kept_hash, lcc_cur_prev_frame;

static void lcc_start(void)
{
	int k;

	if (!psp_keep("levelchunkcount"))
	{
		lcc_on = 0;
		return;
	}
	lcc = psp_recon_table("levelchunkcount", sizeof(*lcc));
	lcc_on = lcc != NULL;
	/* the bucket edges as angles: bucket > k when 1 / cos^2 > f_k^2 sqrt 2 */
	for (k = 0; k < ANGLE_BUCKETS - 1; k++)
		lcc_theta[k] = acosf(sqrtf(1.0f / (angle_factor[k] * angle_factor[k] * 1.41421356f)));
	if (lcc_on)
		platform_log("levelchunkcount: on, %lu KB of tables (%d entries, %d triangles)",
			(unsigned long)(sizeof(*lcc) + 1023) / 1024, LCC_ENTRIES, LCC_TRIS);
}

static unsigned int lcc_hash(const unsigned short *words, long count)
{
	unsigned int hash = 2166136261u;
	long k;

	for (k = 0; k < count; k++)
		hash = (hash ^ words[k]) * 16777619u;
	return hash ^ (unsigned int)count;
}

static struct lcc_chunk *lcc_chunks(int size)
{
	return size == 0 ? lcc->c8 : size == 1 ? lcc->c16 : lcc->c32;
}

/* a ring frame still holds the camera it was made with */
static const struct lcc_cam *lcc_cam_of(unsigned short made)
{
	const struct lcc_cam *cam = &lcc->ring[made & (LCC_RING - 1)];

	if ((unsigned short)cam->frame != made || lcc_frame - cam->frame >= LCC_RING)
		return NULL;
	return cam;
}

/* the store's need for the frame just ended: every entry drawn within the
fence keeps its copy, and a copy replaced within it keeps its old place */
static void lcc_frame_end(void)
{
	unsigned long live = 0, need;
	int k;

	for (k = 0; k < lcc_entries_used; k++)
		if (lcc->entry[k].used && lcc_frame - lcc->entry[k].last_frame < DL_FENCE)
			live += (unsigned long)lcc->entry[k].copy_bytes;
	need = live + lcc_replaced[0] + lcc_replaced[1];
	lcc_now.store_live_sum += live;
	lcc_now.store_need_sum += need;
	if (need > lcc_now.store_need_peak)
		lcc_now.store_need_peak = need;
	if (need > lcc_store_peak_run)
		lcc_store_peak_run = need;
	lcc_now.store_fit16 += need <= 16 * 1024;
	lcc_now.store_fit32 += need <= 32 * 1024;
	lcc_now.store_fit64 += need <= 64 * 1024;
	lcc_replaced[1] = lcc_replaced[0];
	lcc_replaced[0] = 0;
	lcc_now.frames++;
	lcc_now.last_frame = lcc_frame;
	if (lcc_now.frames >= LCC_WINDOW && !lcc_ready_full)
	{
		lcc_ready = lcc_now;
		lcc_ready_full = 1;
		memset(&lcc_now, 0, sizeof(lcc_now));
	}
}

static void lcc_new_frame(unsigned int frame)
{
	struct lcc_cam *cam;
	int k;

	if (lcc_frame != 0xFFFFFFFFu)
		lcc_frame_end();
	lcc_frame = frame;
	if (!lcc_now.frames)
		lcc_now.first_frame = frame;
	cam = &lcc->ring[frame & (LCC_RING - 1)];
	cam->frame = frame;
	for (k = 0; k < 3; k++)
	{
		cam->eye[k] = camera_eye[k];
		cam->basis[k] = camera_right[k];
		cam->basis[3 + k] = camera_up[k];
		cam->basis[6 + k] = camera_forward[k];
	}
	cam->guard_x = guard_x;
	cam->guard_y = guard_y;
	cam->near = NEAR_CLIP;
	cam->far_at = far_cut_at;
	cam->far_active = far_cut_active;
}

/* the pool packed down to the entries drawn in the last 8 frames */
static void lcc_compact(void)
{
	int order[LCC_ENTRIES], n = 0, k, j, next = 0;

	for (k = 0; k < lcc_entries_used; k++)
		if (lcc->entry[k].used && lcc_frame - lcc->entry[k].last_frame <= 8)
			order[n++] = k;
		else
			lcc->entry[k].used = 0;
	for (k = 1; k < n; k++)
		for (j = k; j > 0 && lcc->entry[order[j - 1]].at > lcc->entry[order[j]].at; j--)
		{
			int t = order[j];

			order[j] = order[j - 1];
			order[j - 1] = t;
		}
	for (k = 0; k < n; k++)
	{
		struct lcc_entry *e = &lcc->entry[order[k]];

		if (e->at != next)
		{
			int s;

			for (s = 0; s < LCC_SIZES; s++)
			{
				memmove(lcc->b[s] + next, lcc->b[s] + e->at, (size_t)e->cap);
				memmove(lcc_chunks(s) + next / (8 << s), lcc_chunks(s) + e->at / (8 << s),
					(size_t)(e->cap / (8 << s)) * sizeof(struct lcc_chunk));
			}
			e->at = next;
		}
		next += e->cap;
	}
	lcc_pool_used = next;
	lcc_now.compactions++;
}

static int lcc_take(int cap)
{
	if (lcc_pool_used + cap > LCC_TRIS)
		lcc_compact();
	if (lcc_pool_used + cap > LCC_TRIS)
	{
		int k;

		for (k = 0; k < lcc_entries_used; k++)
			lcc->entry[k].used = 0;
		lcc_pool_used = 0;
		lcc_now.wipes++;
	}
	lcc_pool_used += cap;
	return lcc_pool_used - cap;
}

static struct lcc_entry *lcc_entry_for(const void *stream, unsigned int key, int count, int *stored)
{
	struct lcc_entry *e = NULL, *free_one = NULL;
	int k, cap;

	*stored = 0;
	for (k = 0; k < lcc_entries_used; k++)
	{
		if (lcc->entry[k].used && lcc->entry[k].stream == stream && lcc->entry[k].key == key)
		{
			e = &lcc->entry[k];
			break;
		}
		if (!lcc->entry[k].used && !free_one)
			free_one = &lcc->entry[k];
	}
	if (e && count <= e->cap)
	{
		*stored = 1;
		return e;
	}
	cap = ((count + count / 4) + 31) & ~31;
	if (cap > LCC_MAX)
		cap = (count + 31) & ~31;
	if (e)
		e->used = 0; /* its old place is let go at the next compaction */
	else if (!free_one)
	{
		if (lcc_entries_used < LCC_ENTRIES)
			free_one = &lcc->entry[lcc_entries_used++];
		else
		{
			lcc_compact();
			for (k = 0; k < lcc_entries_used && !free_one; k++)
				if (!lcc->entry[k].used)
					free_one = &lcc->entry[k];
			if (!free_one)
				return NULL;
		}
	}
	if (!free_one)
		free_one = e;
	memset(free_one, 0, sizeof(*free_one));
	free_one->at = lcc_take(cap);
	free_one->stream = stream;
	free_one->key = key;
	free_one->cap = cap;
	free_one->used = 1;
	free_one->last_frame = lcc_frame - 1000;
	{
		int s;

		for (s = 0; s < LCC_SIZES; s++)
			memset(lcc_chunks(s) + free_one->at / (8 << s), 0, (size_t)(cap / (8 << s)) * sizeof(struct lcc_chunk));
	}
	return free_one;
}

static float lcc_camera_move(const struct lcc_cam *then, float *basis_change)
{
	float m = 0.0f, f = 0.0f;
	int k;

	for (k = 0; k < 3; k++)
	{
		float d = camera_eye[k] - then->eye[k];
		float a = camera_right[k] - then->basis[k], b = camera_up[k] - then->basis[3 + k],
			c = camera_forward[k] - then->basis[6 + k];

		m += d * d;
		f += a * a + b * b + c * c;
	}
	*basis_change = sqrtf(f);
	return sqrtf(m);
}

/* after the classify: the list's identity and the view verdicts */
static void lcc_classified(const struct level_stream *stream, unsigned int base_key, const unsigned short *engine_indices,
	long engine_triangles, const unsigned short *indices, long triangle_count, int all_inside)
{
	struct lcc_entry *e;
	unsigned int hash;
	int stored, same_list, chunks8, j, s;
	float norm[6];

	lcc_cur = NULL;
	if (lcc_on < 0)
		lcc_start();
	if (lcc_on <= 0)
		return;
	if (ge_hot.stream_frame != lcc_frame)
		lcc_new_frame(ge_hot.stream_frame);
	lcc_now.draws++;
	if (triangle_count > LCC_MAX || triangle_count <= 0)
	{
		lcc_now.draws_skipped++;
		lcc_now.triangles_skipped += (unsigned long)(triangle_count > 0 ? triangle_count : 0);
		return;
	}
	hash = lcc_hash(engine_indices, engine_triangles * 3);
	e = lcc_entry_for(stream, base_key, (int)triangle_count, &stored);
	if (!e)
	{
		lcc_now.draws_skipped++;
		lcc_now.triangles_skipped += (unsigned long)triangle_count;
		return;
	}
	same_list = stored && e->list_hash == hash && e->count == (int)triangle_count;
	lcc_now.draws_inside += all_inside != 0;
	lcc_now.triangles += (unsigned long)triangle_count;
	lcc_now.drawn_last_draws += e->last_frame == lcc_frame - 1;
	if (same_list)
	{
		lcc_now.list_stored_draws++;
		lcc_now.list_stored_tris += (unsigned long)triangle_count;
		if (e->last_frame == lcc_frame - 1)
		{
			lcc_now.list_last_draws++;
			lcc_now.list_last_tris += (unsigned long)triangle_count;
		}
	}
	norm[0] = 1.0f;
	norm[1] = norm[2] = sqrtf(1.0f + guard_x * guard_x);
	norm[3] = norm[4] = sqrtf(1.0f + guard_y * guard_y);
	norm[5] = 1.0f;
	/* this frame's answer, 8 triangles at a time */
	chunks8 = (int)((triangle_count + 7) / 8);
	for (j = 0; j < chunks8; j++)
	{
		struct lcc_agg *a = &lcc->agg[j];
		long t, end = (j + 1) * 8L < triangle_count ? (j + 1) * 8L : triangle_count;
		int p;

		a->kmin = 1e30f;
		for (p = 0; p < 6; p++)
			a->out[p] = 1e30f;
		a->r = 0.0f;
		a->all_kept = 1;
		a->any_cut = 0;
		a->triangles = (int)(end - j * 8L);
		a->kept = 0;
		a->and_code = CODE_PLANES | CODE_PAST;
		for (t = j * 8L; t < end; t++)
		{
			const unsigned short *triangle = indices + 3 * t;
			int bits[3], corner, planes, split;

			for (corner = 0; corner < 3; corner++)
			{
				float c[3], length;

				level_to_camera(&stream->base[triangle[corner]].x, c);
				bits[corner] = outside(c) | far_cut_code(c[2]);
				a->and_code &= (unsigned int)bits[corner];
				length = sqrtf(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
				if (length > a->r)
					a->r = length;
				for (p = 0; p < 6; p++)
				{
					float d;

					if (p == 5 && !far_cut_active)
						continue;
					d = plane_distance(c, p) / norm[p];
					if (d < a->kmin)
						a->kmin = d;
					if (-d < a->out[p])
						a->out[p] = -d;
				}
			}
			planes = (bits[0] | bits[1] | bits[2]) & CODE_PLANES;
			split = ((bits[0] | bits[1] | bits[2]) & CODE_FAR) == CODE_FAR;
			if (!planes && !split)
			{
				lcc->state[t] = LCC_KEPT;
				a->kept++;
			}
			else
			{
				a->all_kept = 0;
				if (bits[0] & bits[1] & bits[2] & (CODE_PLANES | CODE_PAST))
					lcc->state[t] = LCC_DROPPED;
				else
				{
					lcc->state[t] = LCC_MIXED;
					a->any_cut = 1;
				}
			}
		}
		lcc_now.kept += (unsigned long)a->kept;
	}
	/* each size's chunks against their records */
	for (s = 0; s < LCC_SIZES; s++)
	{
		int per = 1 << s, chunks = (chunks8 + per - 1) / per, cls = all_inside ? 0 : 1;
		struct lcc_chunk *records = lcc_chunks(s) + e->at / (8 << s);

		for (j = 0; j < chunks; j++)
		{
			struct lcc_chunk *rec = &records[j];
			struct lcc_agg sum;
			int k, p, verdict, plane = 0, held = 0;
			float slack;

			sum = lcc->agg[j * per];
			for (k = 1; k < per && j * per + k < chunks8; k++)
			{
				const struct lcc_agg *a = &lcc->agg[j * per + k];

				sum.kmin = a->kmin < sum.kmin ? a->kmin : sum.kmin;
				for (p = 0; p < 6; p++)
					sum.out[p] = a->out[p] < sum.out[p] ? a->out[p] : sum.out[p];
				sum.r = a->r > sum.r ? a->r : sum.r;
				sum.all_kept &= a->all_kept;
				sum.any_cut |= a->any_cut;
				sum.triangles += a->triangles;
				sum.kept += a->kept;
				sum.and_code &= a->and_code;
			}
			if (sum.all_kept)
			{
				verdict = LCC_KEPT;
				slack = sum.kmin;
			}
			else if (!sum.any_cut && sum.and_code)
			{
				verdict = LCC_DROPPED;
				while (!(sum.and_code & (1u << plane)))
					plane++;
				slack = sum.out[plane];
			}
			else
			{
				verdict = LCC_MIXED;
				slack = -1.0f;
			}
			lcc_now.v_tris[s][cls] += (unsigned long)sum.triangles;
			if (same_list && rec->made)
			{
				const struct lcc_cam *then = lcc_cam_of(rec->vframe);

				if (rec->verdict == verdict && verdict != LCC_MIXED)
					lcc_now.v_same[s][cls] += (unsigned long)sum.triangles;
				if (then && rec->verdict != LCC_MIXED && then->guard_x == guard_x && then->guard_y == guard_y &&
					then->far_active == far_cut_active)
				{
					float f, m = lcc_camera_move(then, &f);
					float bound = m + rec->r * f + fabsf(NEAR_CLIP - then->near) +
						(far_cut_active ? fabsf(far_cut_at - then->far_at) : 0.0f) + 1e-4f + rec->r * 4e-6f;

					held = rec->vslack > bound;
				}
				if (held)
				{
					lcc_now.v_held[s][cls] += (unsigned long)sum.triangles;
					if (rec->verdict == LCC_KEPT)
						lcc_now.v_kept_held[s] += (unsigned long)sum.triangles;
					else
						lcc_now.v_dropped_held[s] += (unsigned long)sum.triangles;
					if (verdict != rec->verdict || (verdict == LCC_DROPPED && !(sum.and_code & (1u << rec->plane))))
						lcc_now.v_viol[s][cls] += (unsigned long)sum.triangles;
				}
			}
			if (!held)
			{
				rec->vslack = slack;
				rec->r = sum.r;
				rec->verdict = (unsigned char)verdict;
				rec->plane = (unsigned char)plane;
				rec->vframe = (unsigned short)lcc_frame;
			}
			/* a list that isn't the stored one leaves no bucket record valid */
			if (!same_list)
				rec->made = 0;
			rec->made |= 1;
		}
	}
	e->list_hash = hash;
	e->count = (int)triangle_count;
	{
		/* the kept set, for whether the drawn copy is the frame before's */
		unsigned int kh = 2166136261u;
		long t;

		for (t = 0; t < triangle_count; t++)
			kh = (kh ^ lcc->state[t]) * 16777619u;
		lcc_cur_kept_hash = kh;
	}
	lcc_cur_same_list = same_list;
	lcc_cur_prev_frame = e->last_frame;
	e->last_frame = lcc_frame;
	lcc_cur = e;
	lcc_cur_inside = all_inside;
}

/* one triangle's bucket slack: |d| sin(the angle to the nearer bucket edge),
less an angle for the floats; and the corners' distance from DETAIL_FAR */
static void lcc_bucket_slack(const struct ge_vertex *base, const unsigned short *triangle, int bucket, int far_possible,
	float far_d, float *angle_slack, float *far_slack)
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
		*angle_slack = 1e30f;
	else if (dd <= 0.0f)
		*angle_slack = 0.0f;
	else
	{
		float dot = n[0] * d[0] + n[1] * d[1] + n[2] * d[2], c2 = dot * dot / (nn * dd), theta, room = 1e30f;

		if (c2 > 1.0f)
			c2 = 1.0f;
		theta = acosf(sqrtf(c2));
		if (b > 0 && theta - lcc_theta[b - 1] < room)
			room = theta - lcc_theta[b - 1];
		if (b < ANGLE_BUCKETS - 1 && lcc_theta[b] - theta < room)
			room = lcc_theta[b] - theta;
		room -= 1e-4f;
		*angle_slack = room <= 0.0f ? 0.0f : room >= 1.5707963f ? sqrtf(dd) : sqrtf(dd) * sinf(room);
	}
	*far_slack = 1e30f;
	if (far_possible)
	{
		const struct ge_vertex *corner[3] = { a, p, q };
		int k;

		for (k = 0; k < 3; k++)
		{
			float x = corner[k]->x - ex, y = corner[k]->y - ey, z = corner[k]->z - ez;
			float gap = fabsf(sqrtf(x * x + y * y + z * z) - far_d);

			if (gap < *far_slack)
				*far_slack = gap;
		}
		*far_slack -= 1e-4f + far_d * 1e-6f;
		if (*far_slack < 0.0f)
			*far_slack = 0.0f;
	}
}

/* after the slant sort: the buckets per chunk, the copies' bytes and the
store. kept_copy: the classify copied the kept triangles; sorted: the slant
sort made its copy */
static void lcc_sorted(const struct level_stream *stream, const unsigned short *indices, long triangle_count,
	long kept_count, int kept_copy, int sorted, int cut_count, int far_possible, int show2_copied)
{
	struct lcc_entry *e = lcc_cur;
	int fp = detail_all_far > 0 ? 0 : far_possible, chunks8, j, s, copy_bytes = 0, copy_same;
	float far_d = fp ? sqrtf(detail_far_squared) : 0.0f;
	unsigned int bh = 2166136261u;

	if (!e)
		return;
	lcc_cur = NULL;
	if (kept_copy)
	{
		lcc_now.reserve_bytes += (unsigned long)((triangle_count * 6 + 3) & ~3);
		lcc_now.classify_bytes += (unsigned long)(kept_count * 6);
	}
	else
		lcc_now.writeback_bytes += (unsigned long)(triangle_count * 6);
	if (sorted)
		lcc_now.sort_bytes += (unsigned long)(kept_count * 6);
	if (show2_copied)
		lcc_now.show2_bytes += (unsigned long)(kept_count * 6);
	lcc_now.cut_bytes += (unsigned long)cut_count * sizeof(struct ge_vertex) * (stream->lightmap ? 2 : 1);
	if (angle_bucketed)
	{
		long t;

		lcc_now.bucketed_draws++;
		chunks8 = (int)((triangle_count + 7) / 8);
		for (j = 0; j < chunks8; j++)
		{
			struct lcc_agg *a = &lcc->agg[j];
			long end = (j + 1) * 8L < triangle_count ? (j + 1) * 8L : triangle_count;

			a->bangle = a->bfar = 1e30f;
			for (t = j * 8L; t < end; t++)
			{
				float angle_slack, far_slack;
				int bucket = angle_bucket_one(stream->base, indices + 3 * t, level_eye[0], level_eye[1], level_eye[2], fp,
					detail_far_squared);

				lcc->bucket[t] = (unsigned char)bucket;
				if (lcc->state[t] == LCC_KEPT)
					bh = (bh ^ (unsigned int)bucket) * 16777619u;
				lcc_bucket_slack(stream->base, indices + 3 * t, bucket, fp, far_d, &angle_slack, &far_slack);
				if (angle_slack < a->bangle)
					a->bangle = angle_slack;
				if (far_slack < a->bfar)
					a->bfar = far_slack;
			}
		}
		for (s = 0; s < LCC_SIZES; s++)
		{
			int per = 1 << s, chunks = (chunks8 + per - 1) / per, size = 8 << s;
			struct lcc_chunk *records = lcc_chunks(s) + e->at / size;
			unsigned char *stored = lcc->b[s] + e->at;

			for (j = 0; j < chunks; j++)
			{
				struct lcc_chunk *rec = &records[j];
				long first = (long)j * size, end = first + size < triangle_count ? first + size : triangle_count;
				float bangle = 1e30f, bfar = 1e30f;
				int k, held = 0, same = 1, kept = 0;

				for (k = 0; k < per && j * per + k < chunks8; k++)
				{
					const struct lcc_agg *a = &lcc->agg[j * per + k];

					bangle = a->bangle < bangle ? a->bangle : bangle;
					bfar = a->bfar < bfar ? a->bfar : bfar;
				}
				for (t = first; t < end; t++)
				{
					kept += lcc->state[t] == LCC_KEPT;
					if (stored[t] != lcc->bucket[t])
						same = 0;
				}
				lcc_now.b_tris[s] += (unsigned long)(end - first);
				lcc_now.b_kept[s] += (unsigned long)kept;
				/* made == 3: the buckets were made with this list */
				if (rec->made == 3)
				{
					const struct lcc_cam *then = lcc_cam_of(rec->bframe);

					if (same)
						lcc_now.b_same[s] += (unsigned long)(end - first);
					if (then && rec->far_possible == fp)
					{
						float f, m = lcc_camera_move(then, &f);

						held = m + 1e-4f < rec->bangle && m + fabsf(far_d - rec->far_d) + 1e-4f < rec->bfar;
					}
					if (held)
					{
						lcc_now.b_held[s] += (unsigned long)(end - first);
						lcc_now.b_kept_held[s] += (unsigned long)kept;
						if (!same)
							for (t = first; t < end; t++)
								lcc_now.b_viol[s] += stored[t] != lcc->bucket[t];
					}
				}
				if (!held)
				{
					for (t = first; t < end; t++)
						stored[t] = lcc->bucket[t];
					rec->bangle = bangle;
					rec->bfar = bfar;
					rec->far_d = far_d;
					rec->far_possible = (unsigned char)fp;
					rec->bframe = (unsigned short)lcc_frame;
					rec->made = 3;
				}
			}
		}
	}
	/* the drawn copy (the sorted one, or the classify's unsorted one), as a
	store would hold it: 512-byte pages, draws over 2,048 kept triangles out */
	if ((sorted || kept_copy) && kept_count > 0)
	{
		if (kept_count > 2048)
			lcc_now.store_big_draws++;
		else
			copy_bytes = (int)(((kept_count * 6) + 511) & ~511);
	}
	/* same list, same kept set and same kept buckets as the frame before */
	copy_same = lcc_cur_prev_frame == lcc_frame - 1 && lcc_cur_same_list && e->kept_hash == lcc_cur_kept_hash &&
		e->bucket_hash == bh && e->copy_bytes == copy_bytes;
	if (copy_bytes)
	{
		lcc_now.copy_draws++;
		lcc_now.copy_same_draws += copy_same;
	}
	if (!copy_same && e->copy_bytes && lcc_cur_prev_frame == lcc_frame - 1)
		lcc_replaced[0] += (unsigned long)e->copy_bytes;
	e->copy_bytes = copy_bytes;
	e->kept_hash = lcc_cur_kept_hash;
	e->bucket_hash = bh;
}

/* from the frame's end on the main CPU: a finished window's lines */
static void lcc_poll(void)
{
	extern long psp_game_time_peek(void);
	const struct lcc_counts *w = &lcc_ready;
	unsigned long f;
	int s;
	char line[700];
	int n;

	if (lcc_on <= 0 || !lcc_ready_full)
		return;
	f = w->frames ? w->frames : 1;
	platform_log("levelchunkcount: window frames %u-%u, tick %ld: draws %lu a frame (%lu inside, %lu skipped with %lu triangles); "
		"triangles %lu a frame, kept %lu; list as stored %lu%% of draws %lu%% of triangles, as the frame before %lu%% / %lu%%; "
		"drawn the frame before %lu%% of draws",
		w->first_frame, w->last_frame, psp_game_time_peek(), w->draws / f, w->draws_inside / f, w->draws_skipped,
		w->triangles_skipped, w->triangles / f, w->kept / f,
		w->draws ? w->list_stored_draws * 100 / w->draws : 0, w->triangles ? w->list_stored_tris * 100 / w->triangles : 0,
		w->draws ? w->list_last_draws * 100 / w->draws : 0, w->triangles ? w->list_last_tris * 100 / w->triangles : 0,
		w->draws ? w->drawn_last_draws * 100 / w->draws : 0);
	for (s = 0; s < LCC_SIZES; s++)
	{
		unsigned long ci = w->v_tris[s][0], cc = w->v_tris[s][1];

		n = snprintf(line, sizeof(line),
			"levelchunkcount: chunks of %d: view held %lu.%lu%% of classified draws' triangles (same verdict %lu.%lu%%, %lu violations), "
			"%lu.%lu%% of inside draws' (same %lu.%lu%%, %lu violations), held kept %lu dropped %lu a frame; "
			"buckets held %lu.%lu%% of bucketed triangles (same %lu.%lu%%, %lu violations), %lu.%lu%% of kept",
			8 << s, cc ? w->v_held[s][1] * 100 / cc : 0, cc ? w->v_held[s][1] * 1000 / cc % 10 : 0,
			cc ? w->v_same[s][1] * 100 / cc : 0, cc ? w->v_same[s][1] * 1000 / cc % 10 : 0, w->v_viol[s][1],
			ci ? w->v_held[s][0] * 100 / ci : 0, ci ? w->v_held[s][0] * 1000 / ci % 10 : 0,
			ci ? w->v_same[s][0] * 100 / ci : 0, ci ? w->v_same[s][0] * 1000 / ci % 10 : 0, w->v_viol[s][0],
			w->v_kept_held[s] / f, w->v_dropped_held[s] / f,
			w->b_tris[s] ? w->b_held[s] * 100 / w->b_tris[s] : 0, w->b_tris[s] ? w->b_held[s] * 1000 / w->b_tris[s] % 10 : 0,
			w->b_tris[s] ? w->b_same[s] * 100 / w->b_tris[s] : 0, w->b_tris[s] ? w->b_same[s] * 1000 / w->b_tris[s] % 10 : 0,
			w->b_viol[s], w->b_kept[s] ? w->b_kept_held[s] * 100 / w->b_kept[s] : 0,
			w->b_kept[s] ? w->b_kept_held[s] * 1000 / w->b_kept[s] % 10 : 0);
		if (n > 0)
			platform_log("%s", line);
	}
	platform_log("levelchunkcount: copies a frame: classify reserve %lu B, classify copy %lu B, slant sort copy %lu B, "
		"write-back %lu B, cut corners %lu B, geshow2 copy %lu B; drawn copies %lu a frame, %lu%% the frame before's",
		w->reserve_bytes / f, w->classify_bytes / f, w->sort_bytes / f, w->writeback_bytes / f, w->cut_bytes / f,
		w->show2_bytes / f, w->copy_draws / f, w->copy_draws ? w->copy_same_draws * 100 / w->copy_draws : 0);
	platform_log("levelchunkcount: store of the drawn copies: live %lu B a frame, need %lu B a frame (peak %lu, the run's %lu), "
		"frames fitting 16 KB %lu%%, 32 KB %lu%%, 64 KB %lu%%; draws over 2,048 kept %lu; pool compactions %lu, wipes %lu",
		w->store_live_sum / f, w->store_need_sum / f, w->store_need_peak, lcc_store_peak_run,
		w->store_fit16 * 100 / f, w->store_fit32 * 100 / f, w->store_fit64 * 100 / f, w->store_big_draws, w->compactions,
		w->wipes);
	lcc_ready_full = 0;
}
#define LCC(call) call
