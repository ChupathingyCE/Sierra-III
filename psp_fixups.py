#!/usr/bin/env python3
"""PSP source changes, applied to the build mirror after it is refreshed.

Each change is an exact text replacement that must match once, so a change
upstream makes the build stop here instead of silently building something
else. Every replacement is wrapped in #ifdef HALO_PSP, so the files still
build unchanged for the other ports.
usage: psp_fixups.py TREE"""
import os, sys

TREE = sys.argv[1]
# build 108: PSP_FIXUPS_CHECK=1 lists every anchor that doesn't match once
# (the tree is left as it was) rather than stopping at the first: what a new
# upstream would cost (docs/overnight_b108.md, section 7)
CHECK_ONLY = os.environ.get("PSP_FIXUPS_CHECK", "") == "1"


# the engine base being patched: ChupathingyCE has the 64-bit address helpers
BASE = "chupathingyce" if os.path.exists(os.path.join(TREE, "source/cseries/xbox_address.h")) else "halo-ce-universal"


class Either:
    """one change, written for each engine base the port builds on while it
    moves from halo-ce-universal to ChupathingyCE (docs/chupa_rebase.md):
    universal=(old, new) and chupa=(old, new), each applied only on its own
    base and required to match there exactly once. None: nothing to change on
    that base. When the move is done the universal halves go"""
    def __init__(self, universal=None, chupa=None):
        self.pair = universal if BASE == "halo-ce-universal" else chupa

FIXES = {
# the GE renderer's first slice (psp_ge.c): the level, from the lightmap pass
"source/rasterizer/rasterizer.c": [
("""#include "rasterizer_models.h"
""",
 """#include "rasterizer_models.h"
#ifdef HALO_PSP
#include "rasterizer/rasterizer_model_types.h" /* the node matrices, for the GE (psp_fixups.py) */
#include "shaders/shaders.h"
#include "rasterizer/rasterizer_lights.h"

/* the drawn object's function values (its A to D outs), for its
see-through parts' animated and numeric maps (build 25; psp_fixups.py) */
static real psp_animation_values[4];
/* the object's change colours (team and armour colours; build 31) */
static real_rgb_color psp_change_colors[4];
static boolean psp_change_colors_valid = FALSE;
static long psp_unique_identifier = 0;
static boolean psp_animation_valid = FALSE;
unsigned int psp_tex_bitmap_key(long tag_index, int bitmap);
int psp_tex_bitmap_count(long tag_index);

/* a see-through shader as the GE draws it: one texture (the first map of a
numeric shader, at the digit the object's value gives; otherwise the map
that spans the surface, the smallest scale), its scale and offset with the
map's animation applied at this frame's time, and the blend function. Tag
offsets from the shader definitions (rasterizer_xbox_transparent_geometry.c) */
static unsigned int psp_transparent_map(struct shader const *shader, short permutation, boolean object,
	float *u_scale, float *v_scale, float *u_offset, float *v_offset, short *blend)
{
	char const *definition = (char const *)shader;
	short type = shader ? *(short const *)(definition + 36) : -1;
	unsigned int key = 0;

	*u_scale = *v_scale = 1.0f;
	*u_offset = *v_offset = 0.0f;
	*blend = 0;
	/* build 107: a first map that is a cube map (the shader's "first map
	type": a reflection) is passed over for the next one, which the GE can
	draw. b30's ocean ("water simple": a reflection cube, then three ripple
	maps) had no map at all and wasn't drawn: the white water Sheepdog01 saw (b106) */
	short first_map_type = (type == 5 || type == 6) ? *(short const *)(definition + 42) : 0;
	long map_count = (type == 5 || type == 6) ? *(long const *)(definition + 84) : 0;

	if ((type == 5 || type == 6) && map_count > (first_map_type != 0 ? 1 : 0))
	{
		/* generic and chicago: maps of 100 and 220 bytes */
		int chicago = type == 6, stride = chicago ? 220 : 100, scale_at = chicago ? 84 : 4;
		char const *maps = *(char const *const *)(definition + 88) + (first_map_type != 0 ? stride : 0), *map = maps;
		int map_at = chicago ? 120 : 40, animation_at = chicago ? 164 : 44, count, index, bitmap = 0;

		count = map_count - (first_map_type != 0 ? 1 : 0);
		if (*(unsigned char const *)(definition + 41) & 0x80)
		{
			/* numeric: digit "permutation" of the counter, as the Xbox works it out */
			int frames = psp_tex_bitmap_count(*(long const *)(map + map_at));

			if (frames > 0 && object && psp_animation_valid)
			{
				int limit = *(unsigned char const *)(definition + 40), digit;
				int value = (int)floorf(limit * psp_animation_values[frames == 8 ? 3 : 0] + 0.5f);

				value = value < 0 ? 0 : value > limit ? limit : value;
				for (digit = 0; digit < permutation; digit++)
					value /= frames;
				bitmap = value % frames;
			}
		}
		else
		{
			for (index = 1; index < count && index < 4; index++)
			{
				char const *candidate = maps + stride * index;
				float scale = *(float const *)(candidate + scale_at), best = *(float const *)(map + scale_at);

				if (scale != 0.0f && (best == 0.0f || scale <= best))
					map = candidate;
			}
		}
		key = psp_tex_bitmap_key(*(long const *)(map + map_at), bitmap);
		{
			struct render_animation animation;
			real_vector4d u_transform, v_transform;
			float map_u = *(float const *)(map + scale_at), map_v = *(float const *)(map + scale_at + 4);

			animation.colors = NULL;
			animation.values = psp_animation_values;
			shader_texture_animation_evaluate((struct shader_texture_animation const *)(map + animation_at),
				object && psp_animation_valid ? &animation : NULL, map_u != 0.0f ? map_u : 1.0f,
				map_v != 0.0f ? map_v : 1.0f, *(float const *)(map + scale_at + 8), *(float const *)(map + scale_at + 12),
				*(float const *)(map + scale_at + 16), global_frame_parameters.game_time_sec, &u_transform,
				&v_transform);
			/* the GE scales and shifts (no rotation) */
			*u_scale = u_transform.n[0] != 0.0f ? u_transform.n[0] : 1.0f;
			*v_scale = v_transform.n[1] != 0.0f ? v_transform.n[1] : 1.0f;
			/* textures repeat: only the fraction matters, and the GE's floats
			are short */
			*u_offset = u_transform.n[3] - floorf(u_transform.n[3]);
			*v_offset = v_transform.n[3] - floorf(v_transform.n[3]);
		}
		*blend = *(short const *)(definition + 44);
		{
			/* build 116: the framebuffer fade, as the Xbox applies it
			(rasterizer_xbox_transparent_geometry.c): the fade source's value
			(one of the object's A-D outputs: a Pelican's thrust, a light's
			brightness), the fade mode (by the angle the surface is seen at)
			and, for chicago, whether active camouflage leaves it alone. Until
			b115 every see-through part drew at full strength: the white
			thruster cones and the solid light cones (hardware_b115.md).
			Keep word "seethroughflat" draws them as b115 did */
			short fade_mode = *(short const *)(definition + 46), fade_source = *(short const *)(definition + 48);
			int keep_with_camouflage = chicago && (*(unsigned short const *)(definition + 96) & 1);
			float fade = 1.0f;

			if (fade_source > 0 && fade_source <= 4 && object && psp_animation_valid)
				fade = psp_animation_values[fade_source - 1];
			if (fade_mode < 0 || fade_mode > 2)
				fade_mode = 0;
			if (!PSP_KEEP_ONCE("seethroughflat"))
				*blend = PSP_SEETHROUGH_PACK(*blend, fade_mode, fade, keep_with_camouflage);
		}
	}
	else if (type == 7)
		key = psp_tex_bitmap_key(*(long const *)(definition + 88), 0);
	else if (type == 8)
	{
		key = psp_tex_bitmap_key(*(long const *)(definition + 356), 0);
		*u_scale = *(float const *)(definition + 340);
		*v_scale = *u_scale;
	}
	return key;
}
#endif
"""),
("""	_rasterizer_window_begin(parameters);
	return;""",
 """	_rasterizer_window_begin(parameters);
#ifdef HALO_PSP
	{
		/* the GE's frame starts from the first window's camera (psp_ge.c; psp_fixups.py) */
		extern void psp_ge_window_begin(const float *, const float *, const float *, float, float, float);
		extern void psp_ge_window_target(int);

		psp_ge_window_target((int)parameters->rasterizer_target); /* build 108: 0 the screen */
		psp_ge_window_begin(&parameters->camera.position.x, &parameters->camera.forward.i, &parameters->camera.up.i,
			parameters->camera.vertical_field_of_view, parameters->camera.z_near, parameters->camera.z_far);
	}
#endif
	return;"""),
("""	_rasterizer_model_begin(parameters, is_dynamic);
	return;""",
 """	_rasterizer_model_begin(parameters, is_dynamic);
#ifdef HALO_PSP
	{
		/* the model's node matrices, for the GE (psp_ge.c; psp_fixups.py) */
		extern void psp_ge_model_begin(const float *, int, float, float, const float *, int, const float *, const float *);
		struct render_lighting const *lighting = &parameters->lighting;

		/* and its lighting: the ambient colour and up to two distant lights (build 22) */
		psp_ge_model_begin((const float *)parameters->skinning.node_matrices, parameters->skinning.node_matrix_count,
			parameters->base_map_scale.i, parameters->base_map_scale.j, &lighting->ambient_color.red,
			lighting->distant_light_count, &lighting->distant_lights[0].color.red,
			&lighting->distant_lights[1].color.red);
		{
			/* build 117: the object's point lights (muzzle flashes, plasma,
			the flashlight), up to two, as the Xbox's model vertex shader
			takes them (the "lights" permutation, rasterizer_xbox_models.c).
			Each light's reach is worked out once for the model, as the light
			pass does per vertex (psp_ge.c, light_reach): the falloff
			(1 - d^2/r^2)^2 and a spotlight's cone between its falloff and
			cutoff cosines, both at the point of the model's sphere nearest the
			light (the engine picks a light when its sphere touches the
			object's: find_point_light_for_object), so a Warthog lit at one
			end isn't left dark; the GE then shades each vertex by its angle to
			the light (psp_ge_model_point_light) */
			extern void psp_ge_model_point_light(const float *position, unsigned int color);
			short k;

			for (k = 0; k < lighting->point_light_count && k < MAXIMUM_RENDERED_POINT_LIGHTS; k++)
			{
				long index = lighting->point_light_indices[k];
				struct rasterizer_light_submit_parameters const *light;
				char const *definition;
				real dx, dy, dz, d2, distance, nearest, fade, scale;
				int r, g, b;

				if (index < 0 || index >= rasterizer_lights.light_count)
					continue;
				light = &rasterizer_lights.lights[index];
				if (light->radius <= 0.0f)
					continue;
				dx = light->position.x - parameters->centroid.x;
				dy = light->position.y - parameters->centroid.y;
				dz = light->position.z - parameters->centroid.z;
				d2 = dx * dx + dy * dy + dz * dz;
				distance = square_root(d2);
				nearest = distance - parameters->radius;
				if (nearest < 0.0f)
					nearest = 0.0f;
				fade = 1.0f - nearest * nearest / (light->radius * light->radius);
				if (fade <= 0.0f)
					continue;
				fade *= fade;
				/* the definition's cone cosines at +28 and +32 (object_lights.c),
				against the sphere: the angle to its centre less the angle it
				spans as seen from the light */
				definition = (char const *)light->definition;
				if (definition && *(float const *)(definition + 32) > -0.99f && distance > parameters->radius)
				{
					real cutoff = *(float const *)(definition + 32), falloff = *(float const *)(definition + 28);
					real cosine = -(dx * light->forward.i + dy * light->forward.j + dz * light->forward.k) / distance;
					real sine = cosine < 1.0f ? square_root(1.0f - cosine * cosine) : 0.0f;
					real span_sine = parameters->radius / distance, span_cosine = square_root(1.0f - span_sine * span_sine);
					/* cos(angle - span), or 1 inside the span */
					real cone = sine <= span_sine && cosine > 0.0f ? 1.0f : cosine * span_cosine + sine * span_sine;

					if (falloff <= cutoff)
						falloff = cutoff + 0.001f;
					if (cone <= cutoff)
						continue;
					if (cone < falloff)
						fade *= (cone - cutoff) / (falloff - cutoff);
				}
				scale = fade * 255.0f;
				r = (int)(light->color.red * scale);
				g = (int)(light->color.green * scale);
				b = (int)(light->color.blue * scale);
				r = r > 255 ? 255 : r < 0 ? 0 : r;
				g = g > 255 ? 255 : g < 0 ? 0 : g;
				b = b > 255 ? 255 : b < 0 ? 0 : b;
				if (r | g | b)
					psp_ge_model_point_light(&light->position.x, 0xFF000000u | ((unsigned int)b << 16) | ((unsigned int)g << 8) | (unsigned int)r);
			}
		}
		{
			/* build 38: the first-person weapon's own near plane, as the
			Xbox's model_begin sets it (_rasterizer_geometry_first_person_bit
			is bit 7), and the model's bounding sphere, for the near plane */
			extern void psp_ge_model_shape(int, const float *, float, float, float);

			psp_ge_model_shape((int)((parameters->geometry_flags >> 7) & 1), &parameters->centroid.x,
				parameters->radius, rasterizer_globals.first_person_weapon_near_clip_distance,
				rasterizer_globals.first_person_weapon_far_clip_distance);
		}
		/* and its object's function values, for animated and numeric maps
		on its see-through parts (build 25) */
		{
			/* and active camouflage: how see-through the model is (build 30) */
			extern void psp_ge_model_effect(int type, float intensity);

			psp_ge_model_effect(parameters->effect.type, parameters->effect.intensity);
		}
		psp_animation_valid = parameters->animation.values != NULL;
		if (psp_animation_valid)
			csmemcpy(psp_animation_values, parameters->animation.values, sizeof(psp_animation_values));
		psp_unique_identifier = parameters->unique_identifier;
		psp_change_colors_valid = parameters->animation.colors != NULL;
		if (psp_change_colors_valid)
			csmemcpy(psp_change_colors, parameters->animation.colors, sizeof(psp_change_colors));
	}
#endif
	return;"""),
("""	_rasterizer_model_draw(
		shader,
		shader_permutation_index,
		triangle_buffer,
		dynamic_triangle_buffer_index,
		triangle_count,
		vertex_buffer,
		dynamic_vertex_buffer_index);
	return;""",
 """#ifdef HALO_PSP
	{
		/* once the GE draws, a model part's static geometry goes to it, in a
		warm flat color per shader (psp_fixups.py) */
		extern int psp_ge_active(void);
		extern boolean psp_model_draw(struct triangle_buffer const *, long, struct vertex_buffer const *, unsigned int,
			unsigned int, float, float, int);
		extern unsigned int psp_tex_bitmap_key(long, int);
		/* build 106: this call's time, before the GE (keys, colours) and in
		psp_model_draw (geometry locks, the GE's own work) */
		extern unsigned long psp_microseconds(void);
		extern unsigned long psp_model_draw_us[3];
		unsigned long psp_mark = psp_microseconds();
		unsigned long hash = (unsigned long)shader * 2654435761UL;
		unsigned int r = 160 + ((hash >> 8) & 0x5F), g = 70 + ((hash >> 16) & 0x5F), b = 40 + ((hash >> 24) & 0x3F);
		unsigned int base_key = 0;
		float u_scale = 1.0f, v_scale = 1.0f;
		int alpha_tested = 0;

		/* a model shader (soso): its base map, its map scale, and whether its
		alpha is a cut-out (tag offsets, from the tag definitions; build 20) */
		if (shader && *(short const *)((char const *)shader + 36) == 4)
		{
			base_key = psp_tex_bitmap_key(*(long const *)((char const *)shader + 176), 0);
			u_scale = *(float const *)((char const *)shader + 156);
			v_scale = *(float const *)((char const *)shader + 160);
			alpha_tested = !(*(unsigned short const *)((char const *)shader + 40) & 4);
			/* build 36: a shader with a detail map draws the base map baked
			with it (tools/texpack.py's model_looks), when the pack has it */
			if (base_key && *(long const *)((char const *)shader + 232) != NONE)
			{
				extern int psp_tex_has(unsigned int key);

				if (psp_tex_has(base_key | 1))
					base_key |= 1;
			}
			/* build 36: two-sided (leaves): the back faces drawn too, as the
			Xbox's second pass does; bit 1 of what goes to the GE */
			if (*(unsigned short const *)((char const *)shader + 40) & 2)
				alpha_tested |= 2;
		}
		/* build 116: an environment shader (senv) on an object (b30's locked
		door, a10's pilot chairs: devices and vehicles built with the level's
		own shaders) gets its base map and alpha cut-out, at the offsets the
		level's draws read (psp_environment_draw's hook, build 20). Until b115
		these parts drew in the flat colour above (hardware_b115.md). A
		restoration; keep word "senvflat" brings the flat colour back */
		else if (shader && *(short const *)((char const *)shader + 36) == 3 && !PSP_KEEP_ONCE("senvflat"))
		{
			base_key = psp_tex_bitmap_key(*(long const *)((char const *)shader + 148), 0);
			alpha_tested = *(unsigned short const *)((char const *)shader + 40) & 1;
		}
		{
			/* its change colour (an armour's or team's colour, by the
			multipurpose map's alpha; build 31): the object's colour A to D,
			as the shader's change colour source picks. Model shaders only:
			these offsets mean something else in other shaders (build 116) */
			extern void psp_ge_model_multipurpose(unsigned int, float const *, float const *);
			extern real periodic_function_evaluate(short, real);
			char const *definition = (char const *)shader;
			boolean model_shader = base_key && *(short const *)(definition + 36) == 4;
			short source = model_shader ? *(short const *)(definition + 76) : 0;
			long multipurpose = model_shader ? *(long const *)(definition + 200) : NONE;
			float change[3] = { 1.0f, 1.0f, 1.0f }, glow[3] = { 0.0f, 0.0f, 0.0f };

			if (source >= 1 && source <= 4 && psp_change_colors_valid)
			{
				change[0] = psp_change_colors[source - 1].red;
				change[1] = psp_change_colors[source - 1].green;
				change[2] = psp_change_colors[source - 1].blue;
			}
			/* and its self-illumination colour, as the Xbox works it out:
			between two colours by a periodic function of the time (at a
			random phase per object unless the shader says not), times an
			object colour if the shader names one */
			if (multipurpose != NONE && *(float const *)(definition + 116) != 0.0f)
			{
				unsigned long seed = (unsigned long)psp_unique_identifier;
				real phase = (*(unsigned short const *)(definition + 108) & 1) ? 0.0f : real_seed_random(&seed);
				real fraction = periodic_function_evaluate(*(short const *)(definition + 114),
					global_frame_parameters.game_time_sec / *(float const *)(definition + 116) + phase);
				float const *lower = (float const *)(definition + 120), *upper = (float const *)(definition + 132);
				short glow_source = *(short const *)(definition + 112);
				int channel;

				for (channel = 0; channel < 3; channel++)
					glow[channel] = lower[channel] + (upper[channel] - lower[channel]) * fraction;
				if (glow_source >= 1 && glow_source <= 4 && psp_change_colors_valid)
				{
					glow[0] *= psp_change_colors[glow_source - 1].red;
					glow[1] *= psp_change_colors[glow_source - 1].green;
					glow[2] *= psp_change_colors[glow_source - 1].blue;
				}
			}
			psp_ge_model_multipurpose(multipurpose != NONE ? psp_tex_bitmap_key(multipurpose, 0) : 0, change, glow);
		}
		{
			boolean drawn;
			unsigned long psp_now = psp_microseconds();

			psp_model_draw_us[0] += psp_now - psp_mark;
			psp_mark = psp_now;
			drawn = psp_ge_active() && dynamic_triangle_buffer_index == NONE && dynamic_vertex_buffer_index == NONE &&
				psp_model_draw(triangle_buffer, triangle_count, vertex_buffer, 0xFF000000 | (b << 16) | (g << 8) | r,
					base_key, u_scale, v_scale, alpha_tested);
			psp_model_draw_us[1] += psp_microseconds() - psp_mark;
			psp_model_draw_us[2]++;
			if (drawn)
				return;
		}
	}
#endif
	_rasterizer_model_draw(
		shader,
		shader_permutation_index,
		triangle_buffer,
		dynamic_triangle_buffer_index,
		triangle_count,
		vertex_buffer,
		dynamic_vertex_buffer_index);
	return;"""),
("""	_rasterizer_environment_lightmap_draw(shader, bitmap_index, dynamic_triangle_buffer_index, first_triangle_index, triangle_count, vertex_buffer);
	return;""",
 """#ifdef HALO_PSP
	extern int psp_ge_active(void);

	if (!psp_ge_active())
#endif
	_rasterizer_environment_lightmap_draw(shader, bitmap_index, dynamic_triangle_buffer_index, first_triangle_index, triangle_count, vertex_buffer);
#ifdef HALO_PSP
	{
		/* a flat color per shader, from its address (psp_fixups.py) */
		extern void psp_environment_draw(long, long, long, struct vertex_buffer const *, unsigned int,
			unsigned int, unsigned int, int);
		extern unsigned int psp_tex_bitmap_key(long, int);
		extern unsigned int psp_lightmap_key;
		unsigned long hash = (unsigned long)shader * 2654435761UL;
		unsigned int r = 96 + ((hash >> 8) & 0x7F), g = 96 + ((hash >> 16) & 0x7F), b = 80 + ((hash >> 24) & 0x5F);
		unsigned int base_key = 0;
		int alpha_tested = 0;

		/* an environment shader (senv): its base map, and whether it is alpha
		tested (tag offsets, from the tag definitions; build 20) */
		if (shader && *(short const *)((char const *)shader + 36) == 3)
		{
			base_key = psp_tex_bitmap_key(*(long const *)((char const *)shader + 148), 0);
			alpha_tested = *(unsigned short const *)((char const *)shader + 40) & 1;
		}
		{
			/* its detail and micro detail maps and their tiling (build 31;
			shader_environment_diffuse_properties at +108): a pass each
			unless combined by addition, which the GE's blend can't do */
			extern void psp_ge_level_detail(unsigned int, float, float, unsigned int, float, float);
			char const *definition = (char const *)shader;
			unsigned int keys[2] = {0, 0};
			float scales[2][2] = {{1.0f, 1.0f}, {1.0f, 1.0f}};
			int which;

			if (base_key)
				for (which = 0; which < 2; which++)
				{
					short function = *(short const *)(definition + (which ? 244 : 176));
					float scale = *(float const *)(definition + (which ? 248 : 180));
					unsigned int key = function == 2 ? 0 :
						psp_tex_bitmap_key(*(long const *)(definition + (which ? 264 : 196)), 0);

					/* a blended shader picks between two detail maps by its
					base map's alpha, which a pass of its own can't see: the
					base map holds their mix's average, and only the micro
					detail map is drawn */
					if (!which && *(short const *)(definition + 42) != 0 &&
						*(long const *)(definition + 216) != -1 &&
						*(long const *)(definition + 216) != *(long const *)(definition + 196))
						key = 0;
					if (!key || scale <= 0.0f)
						continue;
					keys[which] = key;
					scales[which][0] = scales[which][1] = scale;
					/* "rescale detail maps": by the base map's size over the
					detail map's (bitmap_data width and height at +4, +6) */
					if ((*(unsigned short const *)(definition + 108) & 1) &&
						*(short const *)(key + 4) > 0 && *(short const *)(key + 6) > 0)
					{
						scales[which][0] *= (float)*(short const *)(base_key + 4) / (float)*(short const *)(key + 4);
						scales[which][1] *= (float)*(short const *)(base_key + 6) / (float)*(short const *)(key + 6);
					}
				}
			psp_ge_level_detail(keys[0], scales[0][0], scales[0][1], keys[1], scales[1][0], scales[1][1]);
		}
		psp_environment_draw(dynamic_triangle_buffer_index, first_triangle_index, triangle_count, vertex_buffer,
			0xFF000000 | (b << 16) | (g << 8) | r, base_key, psp_lightmap_key, alpha_tested);
	}
#endif
	return;"""),
("""	_rasterizer_model_transparent_geometry_submit(
		shader,
		shader_permutation_index,
		triangle_buffer,
		dynamic_triangle_buffer_index,
		triangle_count,
		vertex_buffer,
		dynamic_vertex_buffer_index,
		centroid,
		sort_filth);
	return;""",
 """#ifdef HALO_PSP
	{
		/* see-through model parts (the sky's clouds, glass, water) go to the GE's
		own list, drawn far to near at the end of the frame (psp_ge.c; build 21;
		psp_fixups.py) */
		extern int psp_ge_active(void);
		extern void psp_ge_transparent(struct triangle_buffer const *, long, struct vertex_buffer const *,
			unsigned int, float, float, float, float, short, float const *);

		if (psp_ge_active())
		{
			float u_scale, v_scale, u_offset, v_offset;
			short blend;
			unsigned int key = psp_transparent_map(shader, shader_permutation_index, TRUE, &u_scale, &v_scale,
				&u_offset, &v_offset, &blend);

			if (sort_filth)
				*(short *)((char *)sort_filth + 8) = NONE;
			if (key && dynamic_triangle_buffer_index == NONE && dynamic_vertex_buffer_index == NONE)
				psp_ge_transparent(triangle_buffer, triangle_count, vertex_buffer, key, u_scale, v_scale, u_offset,
					v_offset, blend, &centroid->x);
			return;
		}
	}
#endif
	_rasterizer_model_transparent_geometry_submit(
		shader,
		shader_permutation_index,
		triangle_buffer,
		dynamic_triangle_buffer_index,
		triangle_count,
		vertex_buffer,
		dynamic_vertex_buffer_index,
		centroid,
		sort_filth);
	return;"""),
("""	_rasterizer_psuedo_dynamic_screen_quad_draw(parameters, vertices);
	return;""",
 """#ifdef HALO_PSP
	{
		/* the HUD's quads go to the GE (psp_ge.c; build 20; psp_fixups.py) */
		extern int psp_ge_active(void);
		extern void psp_ge_screen_quad(void const *vertices, unsigned int key, short blend, float red, float green,
			float blue, float fade, float offset_x, float offset_y, void const *meter, int wrapped, float u_scale,
			float v_scale);

		if (psp_ge_active())
		{
			/* one map: the first, except under the menus' moving "plasma",
			where the widget's own picture is the third (build 26) */
			int map = parameters->doing_plasma_effect && parameters->map[2] ? 2 : 0;
			real_rgb_color const *tint = parameters->map_tint[map];
			real const *fade = parameters->map_fade[map];

			extern void psp_ge_ui_space(int width, int height);

			/* the screen these quads are laid out on: the game's 848 x 480 in
			play, the menus' 640 x 480 (build 26) */
			psp_ge_ui_space(global_window_parameters.camera.viewport_bounds.x1 -
				global_window_parameters.camera.viewport_bounds.x0,
				global_window_parameters.camera.viewport_bounds.y1 -
				global_window_parameters.camera.viewport_bounds.y0);
			psp_ge_screen_quad(vertices, (unsigned int)parameters->map[map], parameters->framebuffer_blend_function,
				tint ? tint->red : 1.0f, tint ? tint->green : 1.0f, tint ? tint->blue : 1.0f, fade ? *fade : 1.0f,
				parameters->offset ? parameters->offset->i : 0.0f, parameters->offset ? parameters->offset->j : 0.0f,
				parameters->meter_parameters, parameters->map_wrapped[map], parameters->map_texture_scale[map].i,
				parameters->map_texture_scale[map].j);
			return;
		}
	}
#endif
	_rasterizer_psuedo_dynamic_screen_quad_draw(parameters, vertices);
	return;"""),
("""	_rasterizer_environment_lightmap_begin(lightmap_bitmap);
	return;""",
 """	_rasterizer_environment_lightmap_begin(lightmap_bitmap);
#ifdef HALO_PSP
	{
		/* the lightmap page the next surfaces use, for the GE (build 20; psp_fixups.py) */
		extern unsigned int psp_lightmap_key;

		psp_lightmap_key = (unsigned int)lightmap_bitmap;
	}
#endif
	return;"""),
],
"source/structures/structure_render.c": [
("""		rasterizer_environment_lightmaps_begin();
""",
 """#ifdef HALO_PSP
		{
			/* the GE's frame starts from this window's camera (psp_ge.c; psp_fixups.py) */
			extern void psp_ge_window_begin(const float *, const float *, const float *, float, float, float);
			struct render_camera const *camera = &global_window_parameters.camera;

			psp_ge_window_begin(&camera->position.x, &camera->forward.i, &camera->up.i,
				camera->vertical_field_of_view, camera->z_near, camera->z_far);
		}
#endif
		rasterizer_environment_lightmaps_begin();
"""),
],
# no audio output, no sound engine: with audio.enabled false the Linux layer
# only skips opening the audio device, and the game still starts sounds,
# streams them through its 512 KB sound cache from the Memory Stick and mixes
# them for nobody (3-4 ms a frame, 16-22 ms once the cache thrashes: "SOUND
# CACHE BLOWN"). Disabled, sound_new_impulse, looping_sound_new and
# sound_render do nothing
"source/sound/sound_manager.c": [
("""	sound_manager_globals.initialized = TRUE;

	return;
}""",
 """	sound_manager_globals.initialized = TRUE;
#ifdef HALO_PSP
	{
		extern int config_boolean(const char *name);

		if (!config_boolean("audio.enabled"))
			sound_manager_globals.enabled = FALSE;
	}
#endif

	return;
}"""),
],
# the sound cache's bookkeeping: 1024 pages of 4 KB is the Xbox's 4 MB. The
# PSP's sound memory is PSP_SOUND_CACHE_SIZE, and with the page count left at
# 1024 the cache handed out addresses past its end: sounds were read over the
# map-read requests that follow it on the heap, and the next read went to a
# garbage address (found replaying fire, melee and grenade in the emulator)
"source/cache/xbox_sound_cache.c": [
Either(universal=("""		"xbox sound cache",
		1024,
		12,""",
 """		"xbox sound cache",
#ifdef HALO_PSP
		PSP_SOUND_CACHE_SIZE >> 12,
#else
		1024,
#endif
		12,"""),
 # ChupathingyCE
 chupa=("""	SOUND_CACHE_PAGE_COUNT = 1024,
	SOUND_CACHE_PAGE_SIZE_BITS = 12,""",
  """#ifdef HALO_PSP
	/* the PSP's sound memory, chosen at start-up (psp_platform.c's
	psp_sound_cache_bytes): a macro, not the enum's constant (psp_fixups.py) */
#define SOUND_CACHE_PAGE_COUNT (PSP_SOUND_CACHE_SIZE >> 12)
#else
	SOUND_CACHE_PAGE_COUNT = 1024,
#endif
	SOUND_CACHE_PAGE_SIZE_BITS = 12,""")),
],
# a player on a multiplayer map loaded the campaign way (init.txt's
# map_name: there are no menus to start a game through yet). Its spawn
# points are all marked for multiplayer game types, none of which matches
# "no game engine", so no player would ever appear
"source/game/game_engine.c": [
("""	if (!match_game_type(game_engine_get_type(), 4, starting_location->game_types))
		return 0.0f;
""",
 """#ifdef HALO_PSP
	/* without a game engine any spawn point will do (psp_fixups.py) */
	if (game_engine_running() &&
		!match_game_type(game_engine_get_type(), 4, starting_location->game_types))
		return 0.0f;
#else
	if (!match_game_type(game_engine_get_type(), 4, starting_location->game_types))
		return 0.0f;
#endif
"""),
],
# the script stack: a frame's data starts 2 bytes off a word (offset 14 of
# the 16-byte frame), so the first value stored in every frame is an
# unaligned word, which the x86 Xbox tolerates and the PSP faults on (found
# with PPSSPP's interpreter running init.txt's first command)
"source/hs/hs_runtime.c": [
("""	struct hs_stack_frame *new_frame = (struct hs_stack_frame *)
		((byte *)thread->stack+thread->stack->size+sizeof(struct hs_stack_frame));
""",
 """	struct hs_stack_frame *new_frame = (struct hs_stack_frame *)
		((byte *)thread->stack+thread->stack->size+sizeof(struct hs_stack_frame));
#ifdef HALO_PSP
	/* build 89: on a word (the frame's size is no longer a multiple of 4;
	hs_stack_allocate, psp_fixups.py) */
	new_frame = (struct hs_stack_frame *)(((unsigned long)new_frame+3) & ~3UL);
#endif
"""),
("""	result = frame->data+frame->size;
	frame->size += (short)size;
""",
 """#ifdef HALO_PSP
	/* each allocation starts on a word. Build 89: the frame's size counts to
	the allocation's end, no further, and the next frame is pushed on the
	next word (hs_stack_push, below). b5-b88 also rounded the size up to 4,
	measured from data (2 bytes off a word), so every word took 8 bytes of
	the 512-byte stack instead of 4; by the retail scripts' syntax trees the
	deepest of a10 (flavor_rumble, 616 bytes), c10 (620) and b40 (520) then
	ran past it into the next script thread's datum (code review) */
	result = (void *)(((unsigned long)(frame->data+frame->size)+3) & ~3UL);
	frame->size = (short)((byte *)result+size-frame->data);
	match_hs_assert("c:\\halo\\SOURCE\\hs\\hs_runtime.c", 0x37f, thread_index,
		frame->data+frame->size<=thread->stack_data+HS_THREAD_STACK_SIZE,
		"stack overflow.");
#else
	result = frame->data+frame->size;
	frame->size += (short)size;
#endif
"""),
("""		next_expression_index = *(long *)thread->stack->data;
""",
 """#ifdef HALO_PSP
		/* the frame's first value, at the first word of its data (psp_fixups.py) */
		next_expression_index = *(long *)(((unsigned long)thread->stack->data+3) & ~3UL);
#else
		next_expression_index = *(long *)thread->stack->data;
#endif
"""),
],
"port/linux/src/xbox_kernel.c": [
("""	pthread_mutex_unlock(&handle->lock);

	exit_code = thread->start(thread->parameter);""",
 """	pthread_mutex_unlock(&handle->lock);

#ifdef HALO_PSP
	/* PSP threads start with the FPU's overflow, divide-by-zero and invalid
	operation traps enabled; the Xbox ran with every one masked, and the
	game relies on that (psp_platform.c; psp_fixups.py) */
	{
		extern void psp_fpu_mask_traps(void);

		psp_fpu_mask_traps();
	}
#endif
	exit_code = thread->start(thread->parameter);"""),
Either(universal=("""	fputs("halo-linux: ", stderr);
	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
	fputc('\\n', stderr);""",
 """#ifdef HALO_PSP
	/* each line opened, appended and closed: a crash can't lose it in an
	unwritten directory entry (psp_platform.c; psp_fixups.py) */
	{
		extern void psp_log_write(const char *text);
		char line[512];

		va_start(arguments, format);
		vsnprintf(line, sizeof(line) - 1, format, arguments);
		va_end(arguments);
		psp_log_write(line);
	}
#else
	fputs("halo-linux: ", stderr);
	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
	fputc('\\n', stderr);
#endif"""),
 # ChupathingyCE
 chupa=("""	fputs(PLATFORM_LOG_PREFIX, stderr);
	va_start(arguments, format);
#ifdef HALO_64BIT
	vsnprintf(buffer, sizeof(buffer), format, arguments);
#else
	vfprintf(stderr, format, arguments);
#endif
	va_end(arguments);
#ifdef HALO_64BIT
	fputs(buffer, stderr);
#endif
	fputc('\\n', stderr);""",
  """#ifdef HALO_PSP
	/* each line opened, appended and closed: a crash can't lose it in an
	unwritten directory entry (psp_platform.c; psp_fixups.py) */
	{
		extern void psp_log_write(const char *text);
		char line[512];

		va_start(arguments, format);
		vsnprintf(line, sizeof(line) - 1, format, arguments);
		va_end(arguments);
		psp_log_write(line);
	}
#else
	fputs(PLATFORM_LOG_PREFIX, stderr);
	va_start(arguments, format);
#ifdef HALO_64BIT
	vsnprintf(buffer, sizeof(buffer), format, arguments);
#else
	vfprintf(stderr, format, arguments);
#endif
	va_end(arguments);
#ifdef HALO_64BIT
	fputs(buffer, stderr);
#endif
	fputc('\\n', stderr);
#endif""")),
("	pthread_attr_setstacksize(&thread_attributes, stack_size > 0x100000 ? stack_size : 0x100000);",
 """#ifdef HALO_PSP
	/* 1 MB per thread doesn't fit the PSP's 24 MB; the Xbox asked 16 KB
	(psp_fixups.py) */
	pthread_attr_setstacksize(&thread_attributes, stack_size > 0x10000 ? stack_size : 0x10000);
#else
	pthread_attr_setstacksize(&thread_attributes, stack_size > 0x100000 ? stack_size : 0x100000);
#endif"""),
],
"source/rasterizer/rasterizer_debug.c": [
("	MAXIMUM_DEBUG_PRIMITIVES = 8192,",
 "	MAXIMUM_DEBUG_PRIMITIVES = PSP_DEBUG_PRIMITIVES,"),
],
"source/rasterizer/rasterizer_lights.c": [
("static byte local_lens_flare_occlusion_test_results2[MAXIMUM_LENS_FLARE_MARKERS_PER_STRUCTURE+MAXIMUM_QUEUED_LENS_FLARES][MAXIMUM_WINDOWS];",
 "static byte local_lens_flare_occlusion_test_results2[PSP_LENS_FLARE_MARKERS+MAXIMUM_QUEUED_LENS_FLARES][PSP_LENS_FLARE_WINDOWS];"),
("""		local_lens_flare_count= 0;
	}

	rasterizer_profile_end(_rasterizer_profile_lens_flare_occlusion_query);""",
 """#ifdef HALO_PSP
		/* build 107: the most flares a frame has submitted, for sizing
		local_lens_flare_parameters (1,024 a frame, 40 KB) to the maps:
		the heartbeat's "lens flares" line */
		{
			extern long psp_lens_flare_peak;

			if (local_lens_flare_count > psp_lens_flare_peak)
				psp_lens_flare_peak = local_lens_flare_count;
		}
#endif
		local_lens_flare_count= 0;
	}

	rasterizer_profile_end(_rasterizer_profile_lens_flare_occlusion_query);"""),
("		return &local_lens_flare_occlusion_test_results2[structure_lens_flare_index][window_index];",
 """#ifdef HALO_PSP
		/* build 107: sized to the maps' markers (PSP_LENS_FLARE_MARKERS, in
		psp_fixups.py's PSP sizes); one past it shares the spare result */
		if (structure_lens_flare_index >= PSP_LENS_FLARE_MARKERS + MAXIMUM_QUEUED_LENS_FLARES)
		{
			static byte spare_result;
			static boolean logged = FALSE;

			if (!logged)
			{
				logged = TRUE;
				platform_psp_log("lens flares: marker %ld is past the %d kept (PSP_LENS_FLARE_MARKERS): its flares share one result",
					structure_lens_flare_index - MAXIMUM_QUEUED_LENS_FLARES, PSP_LENS_FLARE_MARKERS);
			}
			return &spare_result;
		}
#endif
		return &local_lens_flare_occlusion_test_results2[structure_lens_flare_index][window_index % PSP_LENS_FLARE_WINDOWS];"""),
],
"port/linux/src/d3d8_resources.c": [
("""static void *allocate_resource_memory(unsigned long size)
{
	void *memory = platform_contiguous_alloc(size, D3DTEXTURE_ALIGNMENT,
		PLATFORM_ANY_PHYSICAL_ADDRESS, PAGE_READWRITE);
""",
"""#ifdef HALO_PSP
/* PSP, null renderer: nothing ever draws into the back and depth buffers or
the render targets, so they all share the start of VRAM instead of taking
~4 MB of RAM (psp_fixups.py; psp_memory.c never frees VRAM) */
static int psp_target_memory = 0;
#define PSP_VRAM_TARGETS ((void *)0x04000000)
#endif

static void *allocate_resource_memory(unsigned long size)
{
#ifdef HALO_PSP
	if (psp_target_memory && size <= 0x200000)
		return PSP_VRAM_TARGETS;
#endif
	void *memory = platform_contiguous_alloc(size, D3DTEXTURE_ALIGNMENT,
		PLATFORM_ANY_PHYSICAL_ADDRESS, PAGE_READWRITE);
"""),
("""	void *memory = allocate_resource_memory(pitch * height);

	memset(surface, 0, sizeof(*surface));""",
"""#ifdef HALO_PSP
	void *memory;

	psp_target_memory = 1;
	memory = allocate_resource_memory(pitch * height);
	psp_target_memory = 0;
#else
	void *memory = allocate_resource_memory(pitch * height);
#endif

	memset(surface, 0, sizeof(*surface));"""),
("""	(void)usage;
	(void)pool;
	return create_texture(width, height, 1, levels, format, FALSE, (D3DBaseTexture **)texture);
}""",
"""	(void)pool;
#ifdef HALO_PSP
	{
		HRESULT result;

		psp_target_memory = (usage & D3DUSAGE_RENDERTARGET) != 0;
		result = create_texture(width, height, 1, levels, format, FALSE, (D3DBaseTexture **)texture);
		psp_target_memory = 0;
		return result;
	}
#else
	(void)usage;
	return create_texture(width, height, 1, levels, format, FALSE, (D3DBaseTexture **)texture);
#endif
}"""),
("""	(void)usage;
	(void)pool;
	return create_texture(edge_length, edge_length, 1, levels, format, TRUE, (D3DBaseTexture **)texture);
}""",
"""	(void)pool;
#ifdef HALO_PSP
	{
		HRESULT result;

		psp_target_memory = (usage & D3DUSAGE_RENDERTARGET) != 0;
		result = create_texture(edge_length, edge_length, 1, levels, format, TRUE, (D3DBaseTexture **)texture);
		psp_target_memory = 0;
		return result;
	}
#else
	(void)usage;
	return create_texture(edge_length, edge_length, 1, levels, format, TRUE, (D3DBaseTexture **)texture);
#endif
}"""),
],
"source/main/main.c": [
("					main_game_render((double)main_globals.seconds_elapsed);",
 """#ifdef HALO_PSP
					{
						/* the frame's time, for the heartbeat (psp_platform.c; psp_fixups.py).
						build 106: its own clock, which "notimers" leaves on */
						extern unsigned long psp_heartbeat_clock(void);
						unsigned long psp_start = psp_heartbeat_clock();

						psp_phase(2);
						main_game_render((double)main_globals.seconds_elapsed);
						psp_frame_account(psp_heartbeat_clock() - psp_start);
					}
#else
					main_game_render((double)main_globals.seconds_elapsed);
#endif"""),
],
"source/game/game_time.c": [
("						game_tick();",
 """#ifdef HALO_PSP
						{
							/* each tick's time, for the heartbeat (psp_platform.c; psp_fixups.py).
							build 106: its own clock, which "notimers" leaves on */
							extern unsigned long psp_heartbeat_clock(void);
							unsigned long psp_start = psp_heartbeat_clock();

							psp_phase(1);
							game_tick();
							psp_phase(0);
							psp_tick_account(psp_heartbeat_clock() - psp_start);
						}
#else
						game_tick();
#endif"""),
("static struct game_time_globals_struct *game_time_globals;\n",
 """static struct game_time_globals_struct *game_time_globals;

#ifdef HALO_PSP
/* the game time in ticks, or -1 before the game state exists: read by the
PSP's heartbeat thread (psp_platform.c; psp_fixups.py) */
long psp_game_time_peek(void)
{
	return game_time_globals && game_time_globals->initialized ? game_time_globals->local_time : -1;
}
#endif
"""),
],
"source/cseries/profile.c": [
("	MAXIMUM_PROFILE_FRAMES = 256,",
 "	MAXIMUM_PROFILE_FRAMES = PSP_PROFILE_FRAMES,"),
],
"source/render/render.h": [
("	unsigned long environment_surface_flags[MAXIMUM_SURFACES_PER_STRUCTURE];",
 """#ifdef HALO_PSP
	/* a bit vector (BIT_VECTOR_TEST_FLAG); one long per surface was 32x its
	size, 512 KB (psp_fixups.py) */
	unsigned long environment_surface_flags[BIT_VECTOR_SIZE_IN_LONGS(MAXIMUM_SURFACES_PER_STRUCTURE)];
#else
	unsigned long environment_surface_flags[MAXIMUM_SURFACES_PER_STRUCTURE];
#endif"""),
],
"port/linux/src/d3d8_gl.c": [
("""		object->instruction_count = function[0] >> 16;
		object->instructions = malloc(object->instruction_count * 4 * sizeof(DWORD));
		memcpy(object->instructions, function + 1, object->instruction_count * 4 * sizeof(DWORD));""",
 """		object->instruction_count = function[0] >> 16;
#ifdef HALO_PSP
		/* build 107: the copy is read only by the GL translator
		(vertex_shader_get), which never runs on the PSP (gl_ready stays
		FALSE): every Xbox vertex shader's code twice over, about 34 KB of
		heap. The count stays, for GetVertexShaderSize */
		object->instructions = NULL;
#else
		object->instructions = malloc(object->instruction_count * 4 * sizeof(DWORD));
		memcpy(object->instructions, function + 1, object->instruction_count * 4 * sizeof(DWORD));
#endif"""),
("#define VISIBILITY_TEST_SLOTS 4096\n",
 """#ifdef HALO_PSP
/* build 107: the GL occlusion queries never run on the PSP (its lens flares
test with psp_ge_widget_occlusion; no run's coverage reached them): two
slots, not 4,096 in four arrays (64 KB). Two, not one: every use takes its
index modulo the count and then turns 0 into 1, since slot 0 is the scratch
query */
#define VISIBILITY_TEST_SLOTS 2
#else
#define VISIBILITY_TEST_SLOTS 4096
#endif
"""),
("#define MIRROR_SEGMENT_COUNT (PLATFORM_CONTIGUOUS_SIZE / MIRROR_SEGMENT_SIZE)\n#define MIRROR_PAGE_SIZE 0x1000UL\n#define MIRROR_PAGE_COUNT (PLATFORM_CONTIGUOUS_SIZE / MIRROR_PAGE_SIZE)",
 """#ifdef HALO_PSP
/* the PSP runs this file only as the null renderer, which never uploads;
400 KB of page tables would be wasted (psp_fixups.py) */
#define MIRROR_SEGMENT_COUNT 1
#define MIRROR_PAGE_SIZE 0x1000UL
#define MIRROR_PAGE_COUNT 1
#else
#define MIRROR_SEGMENT_COUNT (PLATFORM_CONTIGUOUS_SIZE / MIRROR_SEGMENT_SIZE)
#define MIRROR_PAGE_SIZE 0x1000UL
#define MIRROR_PAGE_COUNT (PLATFORM_CONTIGUOUS_SIZE / MIRROR_PAGE_SIZE)
#endif"""),
],
"port/linux/src/platform.h": [
Either(universal=("#define PLATFORM_CONTIGUOUS_BASE 0x80000000UL\n#define PLATFORM_CONTIGUOUS_SIZE 0x08000000UL /* a 128 MB development kit */",
 """#ifdef HALO_PSP
/* PSP: no address window. Addresses 0x88000000 and up are the kernel's
view of RAM, so physical and virtual addresses stay identical; every
user-RAM address counts as contiguous (psp_memory.c; psp_fixups.py). */
#define PLATFORM_CONTIGUOUS_BASE 0x00000000UL
#ifdef PSP_RECON_64MB
#define PLATFORM_CONTIGUOUS_SIZE 0x0C000000UL
#else
#define PLATFORM_CONTIGUOUS_SIZE 0x0A000000UL
#endif
#else
#define PLATFORM_CONTIGUOUS_BASE 0x80000000UL
#define PLATFORM_CONTIGUOUS_SIZE 0x08000000UL /* a 128 MB development kit */
#endif"""),
 # ChupathingyCE
 chupa=("""#define PLATFORM_CONTIGUOUS_BASE 0x80000000U
#ifdef HALO_ANDROID
/* a 128 MB development kit's: Android's guest image is linked just above it
(port/android/include/halo_android_abi.h) */
#define PLATFORM_CONTIGUOUS_SIZE 0x08000000U
#else
/* the desktop builds' (Linux, macOS, Windows): four times a 128 MB
development kit's, for their larger texture cache (halo_port_capacity.h);
port/windows/src/win32_memory_watch.c has the same */
#define PLATFORM_CONTIGUOUS_SIZE 0x20000000U
#endif""",
  """#ifdef HALO_PSP
/* PSP: no address window. Addresses 0x88000000 and up are the kernel's
view of RAM, so physical and virtual addresses stay identical; every
user-RAM address counts as contiguous (psp_memory.c; psp_fixups.py). */
#define PLATFORM_CONTIGUOUS_BASE 0x00000000U
#ifdef PSP_RECON_64MB
#define PLATFORM_CONTIGUOUS_SIZE 0x0C000000U
#else
#define PLATFORM_CONTIGUOUS_SIZE 0x0A000000U
#endif
#else
#define PLATFORM_CONTIGUOUS_BASE 0x80000000U
#ifdef HALO_ANDROID
/* a 128 MB development kit's: Android's guest image is linked just above it
(port/android/include/halo_android_abi.h) */
#define PLATFORM_CONTIGUOUS_SIZE 0x08000000U
#else
/* the desktop builds' (Linux, macOS, Windows): four times a 128 MB
development kit's, for their larger texture cache (halo_port_capacity.h);
port/windows/src/win32_memory_watch.c has the same */
#define PLATFORM_CONTIGUOUS_SIZE 0x20000000U
#endif
#endif""")),
],
"source/cache/physical_memory_map.c": [
Either(universal=("#define TAG_CACHE_BASE_ADDRESS 0x803A6000\n#define TAG_CACHE_SIZE 0x1600000\n#define TEXTURE_CACHE_SIZE 0x1600000\n#define SOUND_CACHE_SIZE 0x400000\n",
 """#ifdef HALO_PSP
/* PSP: the tag window sits near the top of user RAM, under the 1 MB left
for thread stacks; maps are built for it (invader-build -X 0x09600000
-T 0x900000). The texture and sound caches are what the null renderer and
null audio need for now. */
#define TAG_CACHE_BASE_ADDRESS 0x09600000
#define TAG_CACHE_SIZE PSP_TAG_CACHE_SIZE
#define TEXTURE_CACHE_SIZE PSP_TEXTURE_CACHE_SIZE
#define SOUND_CACHE_SIZE PSP_SOUND_CACHE_SIZE
#else
#define TAG_CACHE_BASE_ADDRESS 0x803A6000
#define TAG_CACHE_SIZE 0x1600000
#define TEXTURE_CACHE_SIZE 0x1600000
#define SOUND_CACHE_SIZE 0x400000
#endif
"""),
 # ChupathingyCE
 chupa=("""#define TAG_CACHE_BASE_ADDRESS 0x803A6000
/* (the native builds': halo_port_capacity.h) */
#define TEXTURE_CACHE_SIZE HALO_PORT_TEXTURE_CACHE_SIZE
#define SOUND_CACHE_SIZE 0x400000
""",
  """#ifdef HALO_PSP
/* PSP: the tag window sits near the top of user RAM, under the 1 MB left
for thread stacks; maps are built for it (invader-build -X 0x09600000
-T 0x900000). Its size and the texture cache's are psp_capacity.h's. */
#define TAG_CACHE_BASE_ADDRESS 0x09600000
#define TEXTURE_CACHE_SIZE HALO_PORT_TEXTURE_CACHE_SIZE
#define SOUND_CACHE_SIZE PSP_SOUND_CACHE_SIZE
#else
#define TAG_CACHE_BASE_ADDRESS 0x803A6000
/* (the native builds': halo_port_capacity.h) */
#define TEXTURE_CACHE_SIZE HALO_PORT_TEXTURE_CACHE_SIZE
#define SOUND_CACHE_SIZE 0x400000
#endif
""")),
],
"source/cache/cache_files.c": [
("""		if (cache_file_header_verify(&cache_file_globals.header, scenario_name, TRUE))
		{
			csmemset(tag_cache_base_address, 0xCD, 0x01600000);""",
 """		if (cache_file_header_verify(&cache_file_globals.header, scenario_name, TRUE))
		{
#ifdef HALO_PSP
			platform_psp_log("tags: %s, %ld bytes at file offset 0x%lx into %p",
				scenario_name, (long)cache_file_globals.header.tag_data_size,
				(unsigned long)cache_file_globals.header.tag_data_offset, tag_cache_base_address);
#endif
			csmemset(tag_cache_base_address, 0xCD, 0x01600000);"""),
("""			cache_file_globals.tag_header = tag_cache_base_address;
			match_vassert(""",
 """			cache_file_globals.tag_header = tag_cache_base_address;
#ifdef HALO_PSP
			platform_psp_log("tags: read; signature 0x%08lx, %ld tags",
				(unsigned long)cache_file_globals.tag_header->signature, (long)cache_file_globals.tag_header->tag_count);
			{
				/* a new map's tags sit where the last one's did: the GE forgets
				what it converted or copied from them (build 27) */
				extern void psp_ge_forget_map(void);
				extern void psp_tex_map_opened(const char *name);
				extern void psp_geo_map_opened(const char *name, unsigned long map_checksum, const void *first_vertex_descriptor,
					unsigned long tag_data_end, unsigned long window_end);

				psp_ge_forget_map();
				/* build 68: the geometry pack first (it takes its RAM from the
				texture cache's share), for a map built with its geometry outside.
				Build 106: and where the tags end and the window ends, which the
				animation cache's use of the window's unused part is checked by */
				psp_geo_map_opened(cache_file_globals.header.name, cache_file_globals.header.checksum,
					cache_file_globals.tag_header->vertex_buffer_count ? cache_file_globals.tag_header->vertex_buffers : NULL,
					(unsigned long)tag_cache_base_address + cache_file_globals.header.tag_data_size,
					(unsigned long)tag_cache_base_address + PSP_TAG_CACHE_SIZE);
				/* and takes up the texture pack of the map whose tags these are
				(build 20; at the tags since build 27: a map file is also opened
				for the menu's previews, without becoming the map) */
				psp_tex_map_opened(cache_file_globals.header.name);
			}
#endif
			match_vassert("""),
("XPhysicalProtect((void *)0x803A6000, 0x01600000, PAGE_READWRITE);",
 "XPhysicalProtect(physical_memory_get_tag_cache_base_address(), PSP_TAG_CACHE_SIZE, PAGE_READWRITE);"),
("XPhysicalProtect((void *)0x803A6000, 0x01600000, PAGE_READONLY);",
 "XPhysicalProtect(physical_memory_get_tag_cache_base_address(), PSP_TAG_CACHE_SIZE, PAGE_READONLY);"),
("csmemset(tag_cache_base_address, 0xCD, 0x01600000);",
 "csmemset(tag_cache_base_address, 0xCD, PSP_TAG_CACHE_SIZE);"),
("		0x01600000 - cache_file_globals.header.tag_data_size);",
 "		PSP_TAG_CACHE_SIZE - cache_file_globals.header.tag_data_size);"),
],
"source/cache/xbox_texture_cache.c": [
Either(universal=("	XBOX_TEXTURE_CACHE_PAGE_COUNT = 0x580,",
 "	XBOX_TEXTURE_CACHE_PAGE_COUNT = PSP_TEXTURE_CACHE_SIZE / (1 << 14),"),
 # ChupathingyCE: HALO_PORT_TEXTURE_CACHE_SIZE >> 14, the PSP's from psp_capacity.h
 chupa=None),
Either(universal=("	XBOX_TEXTURE_CACHE_SIZE = 0x1600000,",
 "	XBOX_TEXTURE_CACHE_SIZE = PSP_TEXTURE_CACHE_SIZE,"),
 # ChupathingyCE: HALO_PORT_TEXTURE_CACHE_SIZE (psp_capacity.h)
 chupa=None),
# the guard kept free at both ends while map copying borrows the cache; the
# PSP never copies maps (psp_fixups.py)
("	XBOX_TEXTURE_CACHE_STEAL_GUARD_SIZE = 0x104000,",
 "	XBOX_TEXTURE_CACHE_STEAL_GUARD_SIZE = PSP_TEXTURE_STEAL_GUARD,"),
],
"source/sound/sound_dsound_xbox.c": [
# (build 107: the PSP's size is chosen at start-up, so a macro, not the enum's
# constant; a directive inside the enum's braces is fine for the compiler)
("	SOUND_CACHE_SIZE = 0x400000,",
 """#ifdef HALO_PSP
#define SOUND_CACHE_SIZE PSP_SOUND_CACHE_SIZE
#else
	SOUND_CACHE_SIZE = 0x400000,
#endif"""),
],
"source/cache/cache_files_windows.c": [
("""	request->buffer = buffer;
	request->pending = TRUE;""",
 """	request->buffer = buffer;
#ifdef HALO_PSP
	psp_request_callers[request_index] = __builtin_return_address(0);
#endif
	request->pending = TRUE;"""),
Either(universal=("""			file = cached_map_file_get_handle(cache_file_globals.open_map_file_index);
			match_assert(
				"c:\\\\halo\\\\SOURCE\\\\cache\\\\cache_files_windows.c",
				1327,
				!best_request->running);""",
 """			file = cached_map_file_get_handle(cache_file_globals.open_map_file_index);
#ifdef HALO_PSP
			/* a request with no buffer: what is in the slot, and who asked */
			if (!best_request->buffer)
			{
				long slot = best_request - cache_file_globals.requests;

				platform_psp_log("cache: slot %ld pending with no buffer: size %ld offset 0x%lx event %p blocking %d running %d; last asked for by %p",
					slot, best_request->size, (unsigned long)best_request->overlapped.Offset, best_request->overlapped.hEvent,
					best_request->blocking, best_request->running, psp_request_callers[slot]);
			}
#endif
			match_assert(
				"c:\\\\halo\\\\SOURCE\\\\cache\\\\cache_files_windows.c",
				1327,
				!best_request->running);"""),
 # ChupathingyCE
 chupa=("""			file = cached_map_file_get_handle(cache_file_globals.open_map_file_index);
#ifdef HALO_CUSTOM_EDITION""",
  """			file = cached_map_file_get_handle(cache_file_globals.open_map_file_index);
#ifdef HALO_PSP
			/* a request with no buffer: what is in the slot, and who asked */
			if (!best_request->buffer)
			{
				long slot = best_request - cache_file_globals.requests;

				platform_psp_log("cache: slot %ld pending with no buffer: size %ld offset 0x%lx event %p blocking %d running %d; last asked for by %p",
					slot, best_request->size, (unsigned long)best_request->overlapped.Offset, best_request->overlapped.hEvent,
					best_request->blocking, best_request->running, psp_request_callers[slot]);
			}
#endif
#ifdef HALO_CUSTOM_EDITION""")),
("""static struct cache_file_runtime_globals cache_file_globals;""",
 """static struct cache_file_runtime_globals cache_file_globals;
#ifdef HALO_PSP
static void *psp_request_callers[MAXIMUM_SIMULTANEOUS_CACHE_REQUESTS];
#endif"""),
("""	cache_file_windows_thread_create();
	cache_files_verify_language();""",
 """#ifdef HALO_PSP
	platform_psp_log("cache: %d request slots at %p, %d bytes each", MAXIMUM_SIMULTANEOUS_CACHE_REQUESTS,
		cache_file_globals.requests, (int)sizeof(struct cache_file_request));
#endif
	cache_file_windows_thread_create();
	cache_files_verify_language();"""),
# no Z: cache on the PSP: the slots start empty...
("""	cached_map_files_delete(NUMBER_OF_CACHED_MAP_FILES);
	for (map_file_index = 0;
		map_file_index < NUMBER_OF_CACHED_MAP_FILES;
		map_file_index++)
	{
		struct cached_map_file *map_file = cached_map_file_get(map_file_index);
		long size;""",
"""#ifdef HALO_PSP
	/* PSP: no Z: cache. Maps are stored uncompressed (mapunpack.py) and a
	slot opens the map itself when it is first asked for
	(cached_map_files_find_map). */
	for (map_file_index = 0; map_file_index < NUMBER_OF_CACHED_MAP_FILES; map_file_index++)
	{
		memset(&cached_map_file_get(map_file_index)->header, 0, sizeof(struct cache_file_header));
		cached_map_file_get(map_file_index)->file = INVALID_HANDLE_VALUE;
	}
	return;
#endif
	cached_map_files_delete(NUMBER_OF_CACHED_MAP_FILES);
	for (map_file_index = 0;
		map_file_index < NUMBER_OF_CACHED_MAP_FILES;
		map_file_index++)
	{
		struct cached_map_file *map_file = cached_map_file_get(map_file_index);
		long size;"""),
# ...and a slot is the map file, opened on first use
("""			return map_file_index;
		}
	}

	return NONE;
}""",
"""			return map_file_index;
		}
	}

#ifdef HALO_PSP
	{
		static short next_slot = 0;
		char path[256];
		HANDLE file;

		cache_file_get_map_path(map_name, path);
		/* build 118: one map file open besides the open map's. A slot kept
		its file open until the slot came round again (6 slots), so the
		sixth map in one session found the PSP out of file handles (b117
		test 2, the ladder: a10.map to b40.map all open, the log's handle
		given up at b40, c20's map not opened, back to the XMB). A slot let
		go forgets its header, so its map, asked for again, opens afresh.
		Off: mapfilesopen (b117's) */
		{
			extern int psp_keep(const char *name);
			static int keep_all = -1;
			short other;

			if (keep_all < 0)
				keep_all = psp_keep("mapfilesopen");
			for (other = 0; !keep_all && other < NUMBER_OF_CACHED_MAP_FILES; other++)
			{
				struct cached_map_file *slot = cached_map_file_get(other);

				if (other == cache_file_globals.open_map_file_index)
					continue;
				if (slot->file != INVALID_HANDLE_VALUE && slot->file != NULL)
				{
					platform_psp_log("map slot %d: %s let go", other, slot->header.name);
					CloseHandle(slot->file);
				}
				slot->file = INVALID_HANDLE_VALUE;
				memset(&slot->header, 0, sizeof(struct cache_file_header));
			}
		}
		file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING,
			FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED, NULL);
		if (file != INVALID_HANDLE_VALUE)
		{
			struct cached_map_file *map_file;

			map_file_index = next_slot;
			if (map_file_index == cache_file_globals.open_map_file_index)
				map_file_index = (map_file_index + 1) % NUMBER_OF_CACHED_MAP_FILES;
			next_slot = (map_file_index + 1) % NUMBER_OF_CACHED_MAP_FILES;
			map_file = cached_map_file_get(map_file_index);
			if (map_file->file != INVALID_HANDLE_VALUE && map_file->file != NULL)
				CloseHandle(map_file->file);
			map_file->file = file;
			cached_map_file_read_header(map_file_index);
			platform_psp_log("map %s: opened in slot %d, header '%s'", path, map_file_index, map_file->header.name);
			if (map_file->header.header_signature && !_stricmp(map_name, map_file->header.name))
			{
				/* build 117: the ladder's turns go by the map that opened (psp_platform.c) */
				extern void psp_map_opened(const char *name);

				psp_map_opened(map_file->header.name);
				return map_file_index;
			}
			platform_psp_log("map %s: header missing or named '%s' (store maps uncompressed: mapunpack.py)",
				path, map_file->header.name);
		}
	}
#endif
	return NONE;
}"""),
],
}


# ---------- the AI debug records: every index of the two arrays goes
# through PSP_DEBUG_INDEX, their allocations through PSP_DEBUG_COUNT
import re
DEBUG_ARRAYS = re.compile(r"\b(actor_debug_array|actor_path_debug_array)\[")

def wrap_indices(text):
    out, position = [], 0
    for match in DEBUG_ARRAYS.finditer(text):
        start = match.end()
        if start < position:
            continue
        depth, end = 1, start
        while depth:
            depth += {"[": 1, "]": -1}.get(text[end], 0)
            end += 1
        out.append(text[position:start] + "PSP_DEBUG_INDEX(" + text[start:end - 1] + ")]")
        position = end
    out.append(text[position:])
    return "".join(out)

def ai_debug_records(tree):
    changed = 0
    for directory in ("source/ai",):
        for name in sorted(os.listdir(os.path.join(tree, directory))):
            if not name.endswith(".c"):
                continue
            path = os.path.join(tree, directory, name)
            text = open(path, errors="surrogateescape").read()
            if "PSP_DEBUG_INDEX" in text or not DEBUG_ARRAYS.search(text):
                continue
            text = wrap_indices(text)
            if name == "ai_debug.c":
                for old, new in (
                    ("sizeof(*actor_debug_array) * MAXIMUM_NUMBER_OF_ACTORS", "sizeof(*actor_debug_array) * PSP_DEBUG_COUNT(MAXIMUM_NUMBER_OF_ACTORS)"),
                    ("sizeof(*actor_path_debug_array) * MAXIMUM_NUMBER_OF_ACTOR_PATHS", "sizeof(*actor_path_debug_array) * PSP_DEBUG_COUNT(MAXIMUM_NUMBER_OF_ACTOR_PATHS)"),
                ):
                    if text.count(old) != 2:
                        sys.exit("psp_fixups: ai_debug.c: expected 2 of %s, found %d" % (old, text.count(old)))
                    text = text.replace(old, new)
            open(path, "w", errors="surrogateescape").write(text)
            changed += 1
    return changed

# ---------- build 102: ai_debug.c's path debugger state on demand, and the
# selected squad's records through PSP_DEBUG_INDEX (see FIXES_36 ai_debug.h)
AI_DEBUG_LAZY = """#ifdef HALO_PSP
/* psp_fixups.py (build 102): the path debugger's state (199 KB), allocated
the first time the console's path debugging uses it */
#include <stdlib.h>
static struct path_state *psp_ai_debug_path_state_pointer = NULL;
static struct path_debug_storage *psp_ai_debug_path_storage_pointer = NULL;
static struct path_state *psp_ai_debug_path_state(void)
{
	if (!psp_ai_debug_path_state_pointer)
		psp_ai_debug_path_state_pointer = calloc(1, sizeof(*psp_ai_debug_path_state_pointer));
	return psp_ai_debug_path_state_pointer;
}
static struct path_debug_storage *psp_ai_debug_path_storage(void)
{
	if (!psp_ai_debug_path_storage_pointer)
		psp_ai_debug_path_storage_pointer = calloc(1, sizeof(*psp_ai_debug_path_storage_pointer));
	return psp_ai_debug_path_storage_pointer;
}
#endif
"""

def after_last_include(text):
    """where the file's last #include ends, outside any #if: ChupathingyCE's
    ai_debug.c ends its includes inside #ifdef HALO_64BIT"""
    depth, position, last_end, pending = 0, 0, None, False
    for line in text.splitlines(True):
        position += len(line)
        directive = line.lstrip()
        if directive.startswith("#if"):
            depth += 1
        elif directive.startswith("#endif"):
            depth -= 1
            if pending and depth == 0:
                last_end, pending = position, False
        elif directive.startswith("#include"):
            if depth == 0:
                last_end, pending = position, False
            else:
                pending = True
    return last_end

def ai_debug_shrink(tree):
    changed = 0
    path = os.path.join(tree, "source/ai/ai_debug.c")
    text = open(path, errors="surrogateescape").read()
    if "psp_ai_debug_path_state" not in text:
        n1 = len(re.findall(r"\bai_debug\.path_state\b", text))
        n2 = len(re.findall(r"\bai_debug\.path_storage\b", text))
        if not n1 or not n2:
            sys.exit("psp_fixups: ai_debug.c: no ai_debug.path_state / path_storage")
        text = re.sub(r"\bai_debug\.path_state\b", "(*psp_ai_debug_path_state())", text)
        text = re.sub(r"\bai_debug\.path_storage\b", "(*psp_ai_debug_path_storage())", text)
        # the allocator after the file's last #include, so the types are known
        line_end = after_last_include(text)
        text = text[:line_end] + AI_DEBUG_LAZY + text[line_end:]
        open(path, "w", errors="surrogateescape").write(text)
        changed += 1
    for name in ("ai_debug.c", "actor_firing_position.c"):
        path = os.path.join(tree, "source/ai", name)
        text = open(path, errors="surrogateescape").read()
        if "ai_debug.actor_record[PSP_DEBUG_INDEX(" in text or "ai_debug.actor_record[" not in text:
            continue
        out, position = [], 0
        for match in re.finditer(r"\bai_debug\.actor_record\[", text):
            start = match.end()
            depth, end = 1, start
            while depth:
                depth += {"[": 1, "]": -1}.get(text[end], 0)
                end += 1
            out.append(text[position:start] + "PSP_DEBUG_INDEX(" + text[start:end - 1] + ")]")
            position = end
        out.append(text[position:])
        open(path, "w", errors="surrogateescape").write("".join(out))
        changed += 1
    return changed

# ---------- build 102: colour conversions in single precision. The decomp
# rounds colours the x87 way, (long)rint((double)c * 255), to match the Xbox
# bit for bit; the Allegrex has no double-precision FPU, so each channel cost
# a software double multiply, add, subtract and conversion (~400
# instructions). PPSSPP's I-cache model of The Library in combat: the soft-
# double routines were ~5% of every instruction fetched, most of it from
# real_argb_color_to_pixel32 (the HUD's and widgets' colours, every frame). In
# single precision the result differs only when c * 255 lands within one part
# in 2^24 of a half: a colour's last bit, now and then
FLOAT_COLOUR_FILES = ["source/bitmaps/bitmaps_inlines.h", "source/effects/decals.c"]

# ---------- build 110: render interpolation for the PSP's one local player.
# render_interpolation.c keeps each local player's first-person bones and
# camera (MAXIMUM_LOCAL_PLAYERS, 4): 19 KB of static memory for three players
# the PSP never has (no split screen). Every use there is an array's size, a
# loop's bound or render_interpolation_camera's bounds check, so the file's
# count becomes INTERPOLATION_LOCAL_PLAYERS, 1 on the PSP
INTERPOLATION_PLAYERS = """
/* the local players interpolated: the PSP has one (psp_fixups.py, build 110) */
#ifdef HALO_PSP
#define INTERPOLATION_LOCAL_PLAYERS 1
#else
#define INTERPOLATION_LOCAL_PLAYERS MAXIMUM_LOCAL_PLAYERS
#endif
"""

def interpolation_players(tree):
    path = os.path.join(tree, "port/linux/game/render_interpolation.c")
    text = open(path, errors="surrogateescape").read()
    if "INTERPOLATION_LOCAL_PLAYERS" in text:
        return 0
    anchor = "#define MAXIMUM_INTERPOLATED_NODES 64\n"
    uses = len(re.findall(r"\bMAXIMUM_LOCAL_PLAYERS\b", text))
    if text.count(anchor) != 1 or uses != 8:
        sys.exit("psp_fixups: render_interpolation.c: expected the nodes' define and 8 uses of MAXIMUM_LOCAL_PLAYERS, found %d" % uses)
    text = re.sub(r"\bMAXIMUM_LOCAL_PLAYERS\b", "INTERPOLATION_LOCAL_PLAYERS", text)
    text = text.replace(anchor, anchor + INTERPOLATION_PLAYERS)
    open(path, "w", errors="surrogateescape").write(text)
    return 1

def float_colours(tree):
    changed = 0
    for rel in FLOAT_COLOUR_FILES:
        path = os.path.join(tree, rel)
        text = open(path, errors="surrogateescape").read()
        if "__builtin_rint((double)" not in text:
            continue
        text = text.replace("__builtin_rint((double)", "__builtin_rintf((float)")
        open(path, "w", errors="surrogateescape").write(text)
        changed += 1
    return changed

# ---------- unaligned word accesses the PSP faults on (found with PPSSPP's
# interpreter): every occurrence of each pattern goes through
# PSP_UNALIGNED_ULONG (psp_limits.h)
UNALIGNED = {
    # the trailing guard word after a debug allocation of any size
    "source/cseries/debug_memory.c": ["*(unsigned long *)((byte *)(header + 1)"],
}

def unaligned_accesses(tree):
    changed = 0
    for rel, patterns in UNALIGNED.items():
        path = os.path.join(tree, rel)
        text = open(path, errors="surrogateescape").read()
        if "PSP_UNALIGNED_ULONG" in text:
            continue
        for pattern in patterns:
            if pattern not in text:
                sys.exit("psp_fixups: %s: no %s" % (rel, pattern))
            text = text.replace(pattern, "PSP_UNALIGNED_ULONG(" + pattern[len("*(unsigned long *)("):])
        open(path, "w", errors="surrogateescape").write(text)
        changed += 1
    return changed

# ---------- breadcrumbs: a psp_step() before each call of the game's
# start-up, so the log says how far it got (psp_platform.c)
STEPS = {
    # file: [(function, stop at this text or None)]; the first frames as
    # well as start-up (psp_platform.c logs the first steps only)
    "source/game/game.c": [("void game_initialize(", None), ("void game_tick(", None),
                           # build 36: the frame's particles, contrails, sounds...
                           ("void game_frame(", None)],
    "source/main/main.c": [("void main_loop(", None), ("void main_menu_load(", None),
                           ("static void main_game_render(", None), ("void main_present_frame(", None)],
    "source/render/render.c": [("void render_frame(", None), ("static void render_window(", None)],
    # build 19: what runs after each tick (the gap after profile_tick_end), and the HUD
    "source/game/game_time.c": [("void game_time_update(", None)],
    "source/interface/interface.c": [("void interface_draw_screen(", None)],
    # build 31: the HUD's 2 ms and the visibility's 3 ms a frame, broken down
    "source/interface/hud.c": [("void hud_draw_screen(", None)],
    # build 37: render_objects (8-15 ms a frame on the PSP), broken down
    "source/render/render_objects.c": [("void render_objects(", None), ("static void render_object(", None)],
    # build 66: the visibility's phases (3.6 ms a frame at the vantage point
    # in the emulator)
    "source/structures/structure_visibility.c": [("void structure_visibility_compute(", None)],
}
# build 45: the campaign's tick (b30 recon: 25-30 bipeds at 10-27 ms a tick,
# ai_update 9-14 ms), broken down. Only with PSP_TICK_STEPS=1 (psp_build.py's
# recon builds): a step before every call in these costs ~1.3 us a call, and
# object_update runs for every object every tick
if os.environ.get("PSP_TICK_STEPS", "") == "1":
    STEPS.update({
        "source/units/bipeds.c": [("boolean biped_update(", None), ("static void biped_update_moving(", None)],
        "source/units/units.c": [("boolean unit_update(", None), ("short unit_update_animation(", None)],
        "source/ai/actors.c": [("static void actor_update(", None)],
        "source/ai/actor_perception.c": [("void actor_perception_update(", None), ("static void actor_perception_refresh(", None)],
        "source/objects/objects.c": [("static boolean object_update(", None)],
    })
    # build 106: an object's lighting as it is drawn (b30's beach: 7-10 ms a
    # frame under the step "object_get_level_of_detail_pixels", which is the
    # lighting lookup it is an argument of)
    STEPS["source/render/render_objects.c"] = STEPS["source/render/render_objects.c"] + [
        ("struct render_lighting *object_get_cached_render_lighting(", None),
        ("static void object_render_state_refresh(", None)]
STEP_LINES = {
    "source/bungie_net/network/transport_endpoint_set_winsock.c": [
        "\t\tbypass_file = fopen(", "\t\tif (XNetStartup(", "\t\twsa_error = WSAStartup(",
        "\t\tdeadline = system_milliseconds()", "\t\tXNetRandom(global_nonce"],
}
# build 47: a step with its own label before chosen lines (multi-line calls
# the CALL pattern below can't see, and loop tops). Each text must occur in
# the file; every occurrence gets the step, at the text's own indentation.
# The places were checked to follow a statement or an opening brace, so the
# step never becomes the lone body of an if. Runs after STEPS (the files
# already holding PSP_STEP keep their declaration)
STEP_LABELS = {
    "source/render/render_objects.c": [
        ("\twhile (object_index != NONE)\n\t{\n\t\tstruct object_datum *object = object_get(object_index);\n",
         "render_object_list: an object (definition, LOD pixels, effect)", "after"),
        ("\t\t\t\t\trender_model(\n\t\t\t\t\t\tdefinition->object.model.index,\n\t\t\t\t\t\tlevel_of_detail_pixels,\n",
         "render_model (the object's model)", "before"),
        ("\t\t\t\t\trender_model(\n\t\t\t\t\t\tdefinition->object.model.index,\n\t\t\t\t\t\tlevel_of_detail_pixels * 0.3f,\n",
         "render_model (the object's shadow)", "before"),
        ("\t\t\t\twidgets_render(object_index, data->lighting, &animation);\n", "widgets_render", "before"),
    ],
    # build 66: where the HUD's time goes (hud_render_weapon_interface took
    # 3.1 ms a frame on the PSP, nearly the level's own)
    "source/interface/hud_weapon.c": [
        ("\t\tweapon_build_weapon_interface_state(\n\t\t\tweapon_index,\n\t\t\t&weapon_state);", "hud: weapon state", "before"),
        ("\t\t\tcrosshairs_draw(\n\t\t\t\tplayer,\n\t\t\t\tweapon_index,", "hud: crosshairs", "before"),
        ("\t\t\trender_weapon_hud(\n\t\t\t\thud_index,\n\t\t\t\tplayer->local_player_index,", "hud: weapon elements (flags)", "before"),
        ("\t\t\tplay_weapon_hud_sounds(\n", "hud: weapon sounds", "before"),
        ("\trender_grenade_hud(\n\t\tplayer->local_player_index,", "hud: grenades", "before"),
        ("\tfor (element_index = 0;\n\t\telement_index < definition->statics.count;", "hud: weapon statics", "before"),
        ("\tfor (element_index = 0;\n\t\telement_index < definition->meters.count;", "hud: weapon meters", "before"),
        ("\tfor (element_index = 0;\n\t\telement_index < definition->numbers.count;", "hud: weapon numbers", "before"),
    ],
    "source/interface/hud_unit.c": [
        ("\tunit_hud_shield_meter_mapper_tick();\n", "hud: unit mappers", "before"),
        ("\t\t\t\tif (hud_definition->background.interface_bitmap.index != NONE)\n", "hud: unit background", "before"),
        ("\t\t\t\tif (game_engine_has_shield(player_index) &&\n", "hud: unit shield + health", "before"),
        ("\t\t\t\t\tif (hud_definition->motion_sensor.background.interface_bitmap.index != NONE)\n", "hud: unit motion sensor panel", "before"),
        ("\t\t\t\t\tmotion_sensor_draw_screen(\n", "hud: unit motion sensor blips", "before"),
        ("\t\t\t\t\tword overlay_type_flags = game_engine_has_teams() != FALSE;\n", "hud: unit auxiliary overlays", "before"),
    ],
    "source/models/models.c": [
        ("\t\tif (node_matrices)\n", "render_model: relative node matrices", "before"),
        ("\t\tif (TEST_FLAG(flags, _render_model_shadow_bit))\n\t\t{\n\t\t\trasterizer_environment_shadow_model_begin(",
         "render_model: model begin", "before"),
        ("\t\trender_model_parts(\n", "render_model_parts", "before"),
        ("rasterizer_model_draw(\n", "rasterizer_model_draw", "before"),
        ("rasterizer_model_transparent_geometry_submit(\n", "rasterizer_model_transparent_geometry_submit", "before"),
        ("rasterizer_environment_shadow_model_draw(\n", "rasterizer_environment_shadow_model_draw", "before"),
        ("\t\tif (TEST_FLAG(flags, _render_model_shadow_bit))\n\t\t{\n\t\t\trasterizer_environment_shadow_model_end();",
         "render_model: model end", "before"),
    ],
}

def step_labels(tree):
    changed = 0
    for rel, places in STEP_LABELS.items():
        path = os.path.join(tree, rel)
        text = open(path, errors="surrogateescape").read()
        if "PSP_STEP_LABELS" in text:
            continue
        for place, label, where in places:
            if place not in text:
                sys.exit("psp_fixups: %s: no %r" % (rel, place))
            pieces = text.split(place)
            out = pieces[0]
            for piece in pieces[1:]:
                line_start = out.rfind("\n") + 1
                if where == "before":
                    indent = out[line_start:] if out[line_start:].strip() == "" else ""
                    if not indent:
                        # the place starts with its own indentation
                        indent = place[:len(place) - len(place.lstrip("\t"))]
                        out += '%sPSP_STEP("%s");\n%s' % (indent, label, place)
                    else:
                        out += 'PSP_STEP("%s");\n%s%s' % (label, indent, place)
                else:
                    indent = place.rstrip("\n").split("\n")[-1]
                    indent = indent[:len(indent) - len(indent.lstrip("\t"))]
                    out += '%s%sPSP_STEP("%s");\n' % (place, indent, label)
                out += piece
            text = out
        if "void psp_step(" not in text:
            text = declare_step(text)
        open(path, "w", errors="surrogateescape").write(text + "\n/* PSP_STEP_LABELS */\n")
        changed += 1
    return changed

# a call standing alone as a statement; it becomes one expression, (step, call),
# so it stays a single statement under an if or else without braces
CALL = re.compile(r"^(\t+)([A-Za-z_]\w*)\((.*)\);$", re.M)
NOT_STEPS = {"if", "while", "for", "switch", "return", "sizeof", "PSP_STEP", "match_assert", "match_hs_assert",
             "assert", "error", "csmemset", "csmemcpy", "csstrncpy", "profile_enter", "profile_exit",
             # macros that expand to statements (units.c, bipeds.c)
             "match_assert_valid_real_normal3d", "match_assert_valid_real_vector3d", "match_assert_valid_real_point3d",
             "match_assert_valid_real", "match_assert_valid_real_euler_angles2d", "match_assert_valid_real_matrix4x3",
             "match_assert_valid_real_quaternion", "match_vassert", "vassert", "match_assert_valid_real_plane3d",
             "match_assert_valid_real_normal2d", "match_assert_valid_real_vector2d", "match_assert_valid_real_point2d"}
def step_excluded(name):
    """asserts and other macros that expand to statements can't be made expressions"""
    return name in NOT_STEPS or name.startswith(("match_assert", "assert_valid", "match_vassert"))
STEP_DECLARATION = "\n#ifdef HALO_PSP\nvoid psp_step(const char *name); /* psp_fixups.py */\n#define PSP_STEP(name) psp_step(name)\n#else\n#define PSP_STEP(name) ((void)0)\n#endif\n"

def declare_step(text):
    first = text.index("#include")
    line_end = text.index("\n", first) + 1
    return text[:line_end] + STEP_DECLARATION + text[line_end:]

def definition(text, function):
    """where the function's body starts and ends (not a prototype's)"""
    position = 0
    while True:
        start = text.index("\n" + function, position) + 1
        brace, semicolon = text.find("\n{", start), text.find(";", start)
        if brace >= 0 and (semicolon < 0 or brace < semicolon):
            return brace, text.index("\n}\n", brace)
        position = start

def step_call(match):
    if step_excluded(match.group(2)):
        return match.group(0)
    return '%s(PSP_STEP("%s"), %s(%s));' % (match.group(1), match.group(2), match.group(2), match.group(3))

def breadcrumbs(tree):
    changed = 0
    for rel, functions in STEPS.items():
        path = os.path.join(tree, rel)
        text = open(path, errors="surrogateescape").read()
        if "PSP_STEP(" in text:
            continue
        for function, stop in functions:
            start, end = definition(text, function)
            if stop:
                end = text.index(stop, start)
            text = text[:start] + CALL.sub(step_call, text[start:end]) + text[end:]
        open(path, "w", errors="surrogateescape").write(declare_step(text))
        changed += 1
    for rel, lines in STEP_LINES.items():
        path = os.path.join(tree, rel)
        text = open(path, errors="surrogateescape").read()
        if "PSP_STEP(" in text:
            continue
        for line in lines:
            if text.count(line) != 1:
                sys.exit("psp_fixups: %s: expected 1 of %s" % (rel, line))
            text = text.replace(line, '\t\tPSP_STEP("transport: %s");\n%s' % (line.strip().split("(")[0].replace('"', ''), line))
        open(path, "w", errors="surrogateescape").write(declare_step(text))
        changed += 1
    return changed

# ---------- code added at the end of a file (PSP only)
APPEND = {
    "source/rasterizer/xbox/rasterizer_xbox_draw_primitives.c": '''
#ifdef HALO_PSP
/* the GE renderer's way to the level's geometry (psp_ge.c): a material's
visible triangles, from this frame's triangle list, over its vertex buffer
(psp_fixups.py) */
void psp_ge_draw_level(const void *vertices, const void *lightmap_vertices, int vertex_count,
	const unsigned short *indices, long triangle_count, unsigned int color,
	unsigned int base_key, unsigned int lightmap_key, int alpha_tested);

/* build 68: a vertex or index buffer's bytes come from the geometry cache
when the map keeps them outside the tags (psp_geo.c), from the tags
otherwise; NULL when the cache hasn't read them yet, and the draw is left
for a frame */
void *psp_vertex_lock(const struct vertex_buffer *buffer);
void *psp_index_lock(const struct triangle_buffer *buffer);
void *psp_level_vertices(const struct vertex_buffer *buffer);
void *psp_geo_lock(unsigned int address, unsigned int size);
int psp_geo_external(unsigned int address);
unsigned long psp_geometry_draws_missed = 0;
static boolean psp_geometry_missing(void)
{
	psp_geometry_draws_missed++;
	return FALSE;
}

void psp_environment_draw(
	long dynamic_triangle_buffer_index,
	long first_triangle_index,
	long triangle_count,
	struct vertex_buffer const *vertex_buffer,
	unsigned int color,
	unsigned int base_key,
	unsigned int lightmap_key,
	int alpha_tested)
{
	static boolean reported = FALSE;
	struct dynamic_triangle_buffer *buffer;
	BYTE *vertices = NULL, *lightmap_vertices = NULL;
	struct vertex_buffer const *lightmap = vertex_buffer + 1;

	if (dynamic_triangle_buffer_index == NONE || dynamic_triangle_buffer_index >= dynamic_triangles.buffer_count ||
		!vertex_buffer || !vertex_buffer->hardware_format)
		return;
	if (vertex_buffer->type != _rasterizer_vertex_type_environment_compressed)
	{
		if (!reported)
			platform_psp_log("GE: level vertices of type %d, not drawn (only compressed ones are)", vertex_buffer->type);
		reported = TRUE;
		return;
	}
	buffer = &dynamic_triangles.buffers[dynamic_triangle_buffer_index];
	if (!buffer->triangles || first_triangle_index + triangle_count > buffer->triangle_count)
		return;
	/* the lightmap's vertices follow the material's own (as the Xbox path's
	vertex_buffer + 1), when the level has lightmaps. Build 68: in the
	geometry pack they are one block (32 bytes a vertex, then 8), read once */
	if (lightmap_key && lightmap->hardware_format && lightmap->count == vertex_buffer->count)
	{
		unsigned int base_address = ((const unsigned int *)vertex_buffer->hardware_format)[1];
		unsigned int lightmap_address = ((const unsigned int *)lightmap->hardware_format)[1];

		if (psp_geo_external(base_address) && lightmap_address == base_address + 32 * (unsigned int)vertex_buffer->count)
		{
			/* the pack addresses themselves: the renderer keeps its own copies
			of a material and asks the cache for the bytes only to build them */
			vertices = (BYTE *)base_address;
			lightmap_vertices = (BYTE *)lightmap_address;
		}
		else
		{
			vertices = psp_vertex_lock(vertex_buffer);
			lightmap_vertices = vertices ? psp_vertex_lock(lightmap) : NULL;
			if (!vertices || !lightmap_vertices)
				{ psp_geometry_missing(); return; }
		}
	}
	else
	{
		vertices = psp_level_vertices(vertex_buffer);
		if (!vertices)
			{ psp_geometry_missing(); return; }
	}
	psp_ge_draw_level(vertices, lightmap_vertices, vertex_buffer->count,
		(const unsigned short *)buffer->triangles + 3 * first_triangle_index, triangle_count, color,
		base_key, lightmap_vertices ? lightmap_key : 0, alpha_tested);
}

/* the level triangles a dynamic light reaches (build 29) */
void psp_ge_light_draw(const void *vertices, int vertex_count, const unsigned short *indices, long triangle_count,
	unsigned int base_key);

void psp_environment_light(
	long dynamic_triangle_buffer_index,
	long first_triangle_index,
	long triangle_count,
	struct vertex_buffer const *vertex_buffer,
	unsigned int base_key)
{
	struct dynamic_triangle_buffer *buffer;
	BYTE *vertices = NULL;

	if (dynamic_triangle_buffer_index == NONE || dynamic_triangle_buffer_index >= dynamic_triangles.buffer_count ||
		!vertex_buffer || !vertex_buffer->hardware_format ||
		vertex_buffer->type != _rasterizer_vertex_type_environment_compressed)
		return;
	buffer = &dynamic_triangles.buffers[dynamic_triangle_buffer_index];
	if (!buffer->triangles || first_triangle_index + triangle_count > buffer->triangle_count)
		return;
	vertices = psp_level_vertices(vertex_buffer);
	if (!vertices)
		{ psp_geometry_missing(); return; }
	psp_ge_light_draw(vertices, vertex_buffer->count,
		(const unsigned short *)buffer->triangles + 3 * first_triangle_index, triangle_count, base_key);
}

/* the level triangles under an object's shadow (build 25) */
void psp_ge_shadow_draw(const void *vertices, int vertex_count, const unsigned short *indices, long triangle_count);

void psp_environment_shadow(
	long dynamic_triangle_buffer_index,
	long first_triangle_index,
	long triangle_count,
	struct vertex_buffer const *vertex_buffer)
{
	struct dynamic_triangle_buffer *buffer;
	BYTE *vertices = NULL;

	if (dynamic_triangle_buffer_index == NONE || dynamic_triangle_buffer_index >= dynamic_triangles.buffer_count ||
		!vertex_buffer || !vertex_buffer->hardware_format ||
		vertex_buffer->type != _rasterizer_vertex_type_environment_compressed)
		return;
	buffer = &dynamic_triangles.buffers[dynamic_triangle_buffer_index];
	if (!buffer->triangles || first_triangle_index + triangle_count > buffer->triangle_count)
		return;
	vertices = psp_level_vertices(vertex_buffer);
	if (!vertices)
		{ psp_geometry_missing(); return; }
	psp_ge_shadow_draw(vertices, vertex_buffer->count,
		(const unsigned short *)buffer->triangles + 3 * first_triangle_index, triangle_count);
}

/* the level's see-through surfaces (build 23): queued with the GE's other
see-through draws */
void psp_ge_queue_level_transparent(const void *vertices, int vertex_count, const unsigned short *indices,
	long triangle_count, unsigned int key, float u_scale, float v_scale, float u_offset, float v_offset, short blend,
	const float *centroid);

void psp_environment_transparent(
	long dynamic_triangle_buffer_index,
	long first_triangle_index,
	long triangle_count,
	struct vertex_buffer const *vertex_buffer,
	unsigned int key,
	float u_scale,
	float v_scale,
	float u_offset,
	float v_offset,
	short blend,
	float const *centroid)
{
	struct dynamic_triangle_buffer *buffer;
	BYTE *vertices = NULL;

	if (dynamic_triangle_buffer_index == NONE || dynamic_triangle_buffer_index >= dynamic_triangles.buffer_count ||
		!vertex_buffer || !vertex_buffer->hardware_format ||
		vertex_buffer->type != _rasterizer_vertex_type_environment_compressed)
		return;
	buffer = &dynamic_triangles.buffers[dynamic_triangle_buffer_index];
	if (!buffer->triangles || first_triangle_index + triangle_count > buffer->triangle_count)
		return;
	vertices = psp_level_vertices(vertex_buffer);
	if (!vertices)
		{ psp_geometry_missing(); return; }
	psp_ge_queue_level_transparent(vertices, vertex_buffer->count,
		(const unsigned short *)buffer->triangles + 3 * first_triangle_index, triangle_count, key, u_scale, v_scale,
		u_offset, v_offset, blend, centroid);
}

/* a model part's own vertices and triangles (a strip, or a list), for the GE;
FALSE leaves it to the Direct3D path */
void psp_ge_draw_model(void *vertices, int vertex_count, int uncompressed, const unsigned short *indices,
	long index_count, int strip, unsigned int color, unsigned int base_key, float u_scale, float v_scale,
	int alpha_tested);

void psp_ge_queue_transparent(void *vertices, int vertex_count, int uncompressed, const unsigned short *indices,
	long index_count, int strip, unsigned int key, float u_scale, float v_scale, float u_offset, float v_offset,
	short blend, const float *centroid);

void psp_ge_transparent(
	struct triangle_buffer const *triangle_buffer,
	long triangle_count,
	struct vertex_buffer const *vertex_buffer,
	unsigned int key,
	float u_scale,
	float v_scale,
	float u_offset,
	float v_offset,
	short blend,
	float const *centroid)
{
	BYTE *vertices = NULL, *indices = NULL;
	boolean strip;

	if (!triangle_buffer || !vertex_buffer || !vertex_buffer->hardware_format || !triangle_buffer->hardware_format ||
		(vertex_buffer->type != _rasterizer_vertex_type_model_compressed &&
			vertex_buffer->type != _rasterizer_vertex_type_model_uncompressed))
		return;
	strip = triangle_buffer->type == _triangle_buffer_type_precompiled_strip;
	vertices = psp_vertex_lock(vertex_buffer);
	if (!vertices)
		{ psp_geometry_missing(); return; }
	indices = psp_index_lock(triangle_buffer);
	if (!indices)
		{ psp_geometry_missing(); return; }
	psp_ge_queue_transparent(vertices, vertex_buffer->count, vertex_buffer->type == _rasterizer_vertex_type_model_uncompressed,
		(const unsigned short *)indices, strip ? triangle_count + 2 : 3 * triangle_count, strip, key, u_scale, v_scale,
		u_offset, v_offset, blend, centroid);
}

boolean psp_model_draw(
	struct triangle_buffer const *triangle_buffer,
	long triangle_count,
	struct vertex_buffer const *vertex_buffer,
	unsigned int color,
	unsigned int base_key,
	float u_scale,
	float v_scale,
	int alpha_tested)
{
	BYTE *vertices = NULL, *indices = NULL;
	boolean strip;

	if (!triangle_buffer || !vertex_buffer || !vertex_buffer->hardware_format || !triangle_buffer->hardware_format ||
		(vertex_buffer->type != _rasterizer_vertex_type_model_compressed &&
			vertex_buffer->type != _rasterizer_vertex_type_model_uncompressed))
		return FALSE;
	strip = triangle_buffer->type == _triangle_buffer_type_precompiled_strip;
	vertices = psp_vertex_lock(vertex_buffer);
	if (!vertices)
		return psp_geometry_missing();
	indices = psp_index_lock(triangle_buffer);
	if (!indices)
		return psp_geometry_missing();
	psp_ge_draw_model(vertices, vertex_buffer->count, vertex_buffer->type == _rasterizer_vertex_type_model_uncompressed,
		(const unsigned short *)indices, strip ? triangle_count + 2 : 3 * triangle_count, strip, color, base_key,
		u_scale, v_scale, alpha_tested);
	return TRUE;
}
#endif
''',
    "source/game/players.c": '''
#ifdef HALO_PSP
/* build 108: the local player's unit for the helpers below, or NONE. Only
while a game is in progress: between maps the player and object tables are
disposed but still hold the last map's indices, so reading them asserts
(data.c#396 data->valid) and then faults. Sheepdog01's b107 run of b30: the level
ended, b40 wasn't on the stick, the game's error loop drew a frame, and
nearauto's psp_player_seated read the gone player's unit (object index
#906, a bus error in object_get_and_verify_type) */
static long psp_local_unit(struct player_datum **player_out)
{
	long player_index;
	struct player_datum *player;

	if (player_out)
		*player_out = NULL;
	if (!game_in_progress())
		return NONE;
	player_index = local_player_get_player_index(0);
	if (player_index == NONE)
		return NONE;
	player = player_get(player_index);
	if (player_out)
		*player_out = player;
	return player->unit_index;
}

/* build 108: for the GE's code, which doesn't see the game's headers
(psp_ge.c: nearauto tests the level only while there is one) */
int psp_game_running(void)
{
	return game_in_progress();
}

/* the local player for the heartbeat: whether it has a unit, and where
(psp_platform.c calls this on the game's thread; psp_fixups.py) */
void psp_player_snapshot(char *text, int size)
{
	struct player_datum *player;
	long unit_index = psp_local_unit(&player);

	if (!game_in_progress())
	{
		snprintf(text, size, "player: no game in progress");
		return;
	}
	if (!player)
	{
		snprintf(text, size, "player: no local player");
		return;
	}
	if (unit_index == NONE)
	{
		snprintf(text, size, "player: no unit (respawn timer %ld)", player->respawn_timer);
		return;
	}
	{
		real_point3d origin;

		object_get_origin(unit_index, &origin);
		snprintf(text, size, "player: at %.2f %.2f %.2f", origin.x, origin.y, origin.z);
	}
}

/* build 59: the local player put at a spot and turned to a direction
(ms0:/halo/teleport.txt, psp_platform.c), for repeating what Sheepdog01 saw at a
place his screenshots name */
void psp_player_teleport(float x, float y, float z, float fx, float fy, float fz)
{
	long unit_index = psp_local_unit(NULL);
	real_point3d position;
	real_vector3d facing;

	if (unit_index == NONE)
		return;
	position.x = x; position.y = y; position.z = z;
	facing.i = fx; facing.j = fy; facing.k = fz;
	object_set_position(unit_index, &position, NULL, NULL);
	player_control_set_facing(0, &facing);
}

/* build 90: 1 when the local player sits in a vehicle (nearauto: a model
sphere the eye is inside holds the near plane in, unless it is the vehicle
he is in; psp_ge.c). Build 116 (b113 #17): 2 in a seat without the
third-person camera (a Pelican's bench), where the vehicle's own walls are
at the eye and its sphere holds the plane in as on foot; b90-b115 let the
plane out to 16 x there and cut the cabin open. Keep word "nearseatedfar":
1 in any seat, as before */
int psp_player_seated(void)
{
	static int far_in_any_seat = -1;
	long unit_index = psp_local_unit(NULL);
	struct unit_datum *unit;
	struct object_datum *vehicle;

	if (unit_index == NONE)
		return 0;
	unit = unit_get(unit_index);
	if (unit->object.parent_object_index == NONE)
		return 0;
	if (far_in_any_seat < 0)
		far_in_any_seat = psp_keep("nearseatedfar");
	vehicle = object_get(unit->object.parent_object_index);
	if (far_in_any_seat || !TEST_FLAG(_object_mask_unit, vehicle->object.type) || unit->unit.parent_seat_index == NONE)
		return 1;
	return TEST_FLAG(TAG_BLOCK_GET_ELEMENT(&unit_definition_get(vehicle->definition_index)->unit.seats,
		unit->unit.parent_seat_index, struct unit_seat)->flags, _unit_seat_third_person_camera_bit) ? 1 : 2;
}

/* build 88: where the local player is now (benchrun re-places him only when
he has drifted from the spot); 0 without a player */
int psp_player_origin(float *out)
{
	long unit_index = psp_local_unit(NULL);
	real_point3d origin;

	if (unit_index == NONE)
		return 0;
	object_get_origin(unit_index, &origin);
	out[0] = origin.x;
	out[1] = origin.y;
	out[2] = origin.z;
	return 1;
}

/* build 70: a collision regression check ("colltest" in keep.txt,
psp_platform.c): 256 rays from a metre above the player's feet in fixed
directions, 60 units long, against the structure and objects; each hit's
distance, surface, plane designator and material logged with a checksum.
Two builds (or two bakes of a map) that collide the same give the same
lines: the test of the packed collision tables */
extern boolean collision_test_vector(unsigned long flags, real_point3d const *point, real_vector3d const *vector,
	long ignore_object_index, struct collision_result *collision);
void psp_collision_test(const float *fixed_origin)
{
	long unit_index = psp_local_unit(NULL);
	real_point3d origin;
	unsigned long checksum = 0, hits = 0;
	int ray;

	if (unit_index == NONE)
		return;
	if (fixed_origin)
	{
		/* teleport.txt's spot: the same rays whatever the player did since */
		origin.x = fixed_origin[0]; origin.y = fixed_origin[1]; origin.z = fixed_origin[2];
	}
	else
		object_get_origin(unit_index, &origin);
	origin.z += 1.0f;
	platform_psp_log("colltest: from %.4f %.4f %.4f", origin.x, origin.y, origin.z);
	for (ray = 0; ray < 256; ray++)
	{
		/* a Fibonacci sphere of directions */
		real k = 1.0f - 2.0f * (ray + 0.5f) / 256.0f;
		real r = (real)sqrt(1.0f - k * k);
		real a = (real)ray * 2.39996323f;
		real_vector3d vector;
		struct collision_result result;
		boolean hit;

		vector.i = r * (real)cos(a) * 60.0f;
		vector.j = r * (real)sin(a) * 60.0f;
		vector.k = k * 60.0f;
		csmemset(&result, 0, sizeof(result));
		hit = collision_test_vector(0xC0A3, &origin, &vector, unit_index, &result);
		if (hit)
		{
			unsigned long t_bits;

			csmemcpy(&t_bits, &result.t, sizeof(t_bits));
			checksum = checksum * 31 + t_bits + (unsigned long)result.surface_index * 7 +
				(unsigned long)result.plane_designator * 13 + (unsigned long)result.material_index;
			hits++;
			if (ray % 8 == 0)
				platform_psp_log("colltest: ray %d hit t %.6f surface %ld plane %08lx material %d object %ld",
					ray, result.t, result.surface_index, (unsigned long)result.plane_designator,
					result.material_index, result.object_index);
		}
		else
			checksum = checksum * 31 + 0x9E3779B9UL;
	}
	platform_psp_log("colltest: %lu of 256 rays hit; checksum %08lx", hits, checksum);
}
#endif
''',
}

def appended(tree):
    for rel, code in APPEND.items():
        path = os.path.join(tree, rel)
        text = open(path, errors="surrogateescape").read()
        if "/* appended by psp_fixups.py */" not in text:
            open(path, "w", errors="surrogateescape").write(text + "\n/* appended by psp_fixups.py */\n" + code)
    return len(APPEND)

# ---------- passes the GE renderer makes redundant: once the GE draws the
# level (psp_ge.c), these only feed the null Direct3D path, so on the PSP they
# return at once (about 6-8 ms a frame on Blood Gulch, measured with build 14)
SKIPPED_PASSES = {
    "source/structures/structure_render.c": ["structure_render_diffuse_texture", "structure_render_specular_lightmaps",
        "structure_render_reflection_lightmap_masks", "structure_render_reflections",
        "structure_render_fog", "structure_render_fog_screen"],
    "source/structures/structure_detail_objects.c": ["structure_render_detail_objects"],
    "source/objects/object_lights.c": ["lights_render_specular"],
}
SKIP_DECLARATION = "\n#ifdef HALO_PSP\nint psp_ge_active(void); /* psp_ge.c; psp_fixups.py */\n#endif\n"

def skipped_passes(tree):
    changed = 0
    for rel, functions in SKIPPED_PASSES.items():
        path = os.path.join(tree, rel)
        text = open(path, errors="surrogateescape").read()
        if "psp_ge_active()" in text:
            continue
        for function in functions:
            header = "\nvoid %s(\n\tvoid)\n{\n" % function
            if text.count(header) != 1:
                sys.exit("psp_fixups: %s: expected one definition of %s" % (rel, function))
            text = text.replace(header, header + "#ifdef HALO_PSP\n\tif (psp_ge_active())\n\t\treturn; /* the GE draws the level (psp_fixups.py) */\n#endif\n")
        first = text.index("#include")
        line_end = text.index("\n", first) + 1
        text = text[:line_end] + SKIP_DECLARATION + text[line_end:]
        open(path, "w", errors="surrogateescape").write(text)
        changed += 1
    return changed

PRELUDE = '''/* PSP sizes (psp_fixups.py) */
#ifdef HALO_PSP
/* (the tag window's and the texture cache's sizes, PSP_TAG_CACHE_SIZE and
PSP_TEXTURE_CACHE_SIZE, are psp_capacity.h's: every file reads them, not
only the files changed here) */
#define PSP_TEXTURE_STEAL_GUARD 0x4000
/* build 102: 1.125 MB (was 1 MB): Sheepdog01's b101 Library run streamed 99 MB of
sound in 6.5 minutes through the 1 MB cache (2,954 loads, 35 failed for
room, the last second's working set up to 1000 KB); half of the 256 KB the
build gave back to the heap (AI debugger, log buffers). Build 107: chosen at
start-up, keep.txt's "soundN" (KB) or the default (psp_platform.c's
psp_sound_cache_bytes), so it is no longer a constant */
unsigned long psp_sound_cache_bytes(void);
#define PSP_SOUND_CACHE_SIZE psp_sound_cache_bytes()
#define PSP_PROFILE_FRAMES 2
#define PSP_DEBUG_PRIMITIVES 64
/* one screen: no split screen on the PSP */
#define PSP_LENS_FLARE_WINDOWS 1
/* build 107: the lens flare results kept per BSP marker. The game kept one
for every marker index a BSP could have (65,536: 64 KB); the most any BSP
of the ten campaign maps has is 4,616 (d40; a10 2,596, c20 733), read from
the maps' "lens flare markers". A marker past this shares one spare result
(its flare fades with the others'; logged once) */
#define PSP_LENS_FLARE_MARKERS 8192
void platform_psp_log(const char *format, ...);
unsigned long psp_microseconds(void);
void psp_tick_account(unsigned long microseconds);
void psp_frame_account(unsigned long microseconds);
/* build 46: the hardware profiler's phases (psp_platform.c) */
void psp_phase(int phase);
#else
#define PSP_TAG_CACHE_SIZE 0x01600000
#define PSP_TEXTURE_CACHE_SIZE 0x1600000
#define PSP_TEXTURE_STEAL_GUARD 0x104000
#define PSP_SOUND_CACHE_SIZE 0x400000
#define PSP_PROFILE_FRAMES 256
#define PSP_DEBUG_PRIMITIVES 8192
#define PSP_LENS_FLARE_WINDOWS MAXIMUM_WINDOWS
#define PSP_LENS_FLARE_MARKERS MAXIMUM_LENS_FLARE_MARKERS_PER_STRUCTURE
#endif
'''

# ---------- build 19: with the GE drawing, the null path's shadows,
# see-through parts, particles, decals, HUD and texture loads cost CPU time
# and draw nothing, and render interpolation blends between ticks the PSP
# can't draw more often than: each is skipped unless ms0:/halo/keep.txt names
# it (psp_platform.c's psp_keep), so its cost can still be measured.
# objects_update is timed by object type (psp_platform.c's psp_object_account)
KEEP_DECLARATION = "#ifdef HALO_PSP\nint psp_keep(const char *name); /* psp_platform.c; psp_fixups.py */\nint psp_ge_active(void);\n#endif\n"
# objects.c (build 19): idle scenery sleeps. Blood Gulch updates ~259
# scenery objects every tick, and for a tree or a rock the update reproduces
# the object exactly. An object whose update leaves its data byte-identical
# twice running sleeps: it is skipped while its data stays the same, and any
# change from outside (damage, a script moving it, a saved game loading)
# wakes it, since its data no longer matches. Asleep it is still drawn and
# still collides, like an object outside the Xbox's active area.
# "awake" in keep.txt turns it off
PSP_SLEEP_CODE = """#ifdef HALO_PSP
void psp_object_account(int type, unsigned long microseconds); /* psp_platform.c; psp_fixups.py */
int psp_keep(const char *name);
static unsigned long psp_object_start;
static int psp_sleep_enabled = -1;
static int psp_sleep_debug = -1, psp_sleep_reports = 0, psp_vehicle_sleep_reports = 0;
/* sleepdebug: one vehicle's data at the end of its update, to see what
changes before the next (build 44) */
static unsigned long between[640];
static short between_identifier = 0;
static int between_index = -1;
static int psp_vehicle_sleep_on = -1;
long psp_objects_asleep, psp_objects_sleep_checked, psp_vehicles_asleep, psp_corpses_asleep;
/* build 106: how far dead bipeds get through the corpse-sleep rule, a count
per biped update: dead; and garbage (not feigning); and at rest; and idle
past 2 s (so offered to the byte-identical rule). b105 run 1 slept none
and couldn't say why. Build 112: psp_corpses_placed_dead, the bodies the
level placed dead (never garbage) that go on to the same stages */
long psp_corpse_stage[4], psp_corpses_holding, psp_corpses_placed_dead;
/* build 114: bodies kept awake because something other than the level held
them up, and the ground tests made (psp_corpse_on_level) */
long psp_corpses_off_level, psp_corpse_ground_tests;
/* build 114: quiet (0 to 2) as a byte, and ground beside it: for a body
offered to sleep, whether the level is under it (psp_corpse_on_level: 0 not
asked yet, 1 the level, 2 not). Still 8 bytes an entry */
static struct { short identifier; signed char quiet; unsigned char ground; unsigned long checksum; } psp_sleep[MAXIMUM_OBJECTS_PER_MAP];

/* What the byte-identical rule leaves out of an object's data: the words an
update changes every tick without changing anything the game shows or does.
One set of rules for the checksum and for sleepdebug's report of what kept
an object awake, so the two always agree */
struct psp_checksum_rules
{
	long count;            /* words compared (the datum, at most 2048 bytes) */
	long half_skip;        /* low half left out (a unit's timer) */
	long whole_skip;       /* whole word left out (a weapon's last-owned time) */
	long idle_word;        /* a biped's idle_ticks: its half left out */
	unsigned long idle_mask;
	long aim_first, aim_end; /* a dead biped's aim and look servo */
	long functions_first, functions_end, colours_first, colours_end; /* and what it recomputes asleep */
};

static void psp_checksum_rules_for(struct object_header_datum const *header, struct psp_checksum_rules *rules)
{
	/* build 106: units.c's psp_unit_aim_bytes */
	extern const long psp_unit_aim_bytes[2];

	rules->count = (header->data_size < 2048 ? header->data_size : 2048) / 4;
	/* build 44: a unit's timer (unit.timer, +0x1BC; units.c's round robin
	for the "used time" work) counts every tick whatever the unit does, so
	it is left out for vehicles: asleep, a parked vehicle's turn simply
	passes to the others */
	rules->half_skip = header->type == _object_type_vehicle || header->type == _object_type_biped ? 0x1BC / 4 : -1;
	/* and a weapon's item.last_owned_time (+0x1B4), stamped with the game
	time every tick while it is attached to a unit (items.c): the vehicle's
	own gun carries it, and only unattached items are ever collected by it */
	rules->whole_skip = header->type == _object_type_weapon ? 0x1B4 / 4 : -1;
	/* build 105: a settled corpse's idle_ticks (biped_update counts them
	while it lies dead and at rest): left out, and advanced by hand while it
	sleeps (psp_sleepy_update) */
	rules->idle_word = header->type == _object_type_biped ? (long)(offsetof(struct object_datum, object.idle_ticks) / 4) : -1;
	rules->idle_mask = (offsetof(struct object_datum, object.idle_ticks) & 2) ? 0x0000FFFFUL : 0xFFFF0000UL;
	/* build 106: a dead biped's aim and look servo: it oscillates on a corpse
	and the dying pose ignores it (see units.c) */
	rules->aim_first = rules->aim_end = -1;
	rules->functions_first = rules->functions_end = rules->colours_first = rules->colours_end = -1;
	if (header->type == _object_type_biped &&
		TEST_FLAG(((struct object_datum const *)header->datum)->object.damage_flags, _object_dead_bit))
	{
		rules->aim_first = psp_unit_aim_bytes[0] / 4;
		rules->aim_end = (psp_unit_aim_bytes[1] + 3) / 4;
		/* and its function values (incoming, then outgoing) and outgoing
		change colours, which a sleeping corpse still computes every tick
		(psp_sleepy_update): a Flood body's first function cycles for ever */
		rules->functions_first = offsetof(struct object_datum, object.incoming_function_values) / 4;
		rules->functions_end = (offsetof(struct object_datum, object.outgoing_function_values) +
			sizeof(((struct object_datum *)0)->object.outgoing_function_values) + 3) / 4;
		rules->colours_first = offsetof(struct object_datum, object.outgoing_change_colors) / 4;
		rules->colours_end = (offsetof(struct object_datum, object.outgoing_change_colors) +
			sizeof(((struct object_datum *)0)->object.outgoing_change_colors) + 3) / 4;
	}
}

/* a word as the rule compares it. Word 2 is every object's magic_number
(+0x8): the marker a render or collision pass stamps on each object it
visits (object_mark_function), so a visible object's changed every frame.
Until build 44 only objects off screen ever slept */
static unsigned long psp_checksum_word(struct psp_checksum_rules const *rules, long index, unsigned long value)
{
	if (index == 2 || index == rules->whole_skip || (index >= rules->aim_first && index < rules->aim_end) ||
		(index >= rules->functions_first && index < rules->functions_end) ||
		(index >= rules->colours_first && index < rules->colours_end))
		return 0;
	if (index == rules->half_skip)
		return value & 0xFFFF0000UL;
	if (index == rules->idle_word)
		return value & rules->idle_mask;
	return value;
}

/* build 113: the sum itself. b112's, rotate by 5 then xor each word in,
cancels: a bit changed in one word and the bit 5 places up changed in the
next go in as the same value and xor to nothing. A Warthog's flip is just
that (vehicle.flags gains 0x10, and the next word, upending_type, becomes 2:
0x200), so the sleep never saw it and the vehicle never righted itself
(b113_candidates.md #10). FNV-1a doesn't cancel like that: each word is
xored in, then the lane is multiplied by an odd number, so for a second
change to undo a first it must equal what the multiply made of the first,
which depends on every byte before it. A single change always shows (xor and
an odd multiply can't map two values to one). Four lanes, word i in lane
i % 4, so the four multiplies of a turn of the loop overlap. "weaksum" in
keep.txt brings back b112's sum, for timing the two.

Build 114: the multiply by 33 (a shift by 5 and an add) in place of FNV's
prime. b113's Test 1 on the PSP: FNV-1a cost about 1 ms a tick over b112's
sum (scenery's update 4.4 -> 5.5 ms for the same objects). What fixed the
flip holds for any odd multiplier: a single change always shows (xor and an
odd multiply are both one to one), and the flip's two neighbouring words go
to different lanes, so they cancel only if the end's mix of the lanes makes
two unrelated differences equal. Both share one blind spot: the top bit of
two words four apart (one lane) changed together cancels, since a top-bit
difference passes any odd multiply unchanged. "fnvsum" brings back b113's */
#define PSP_CHECKSUM_STEP(sum, value) (((sum) << 5 | (sum) >> 27) ^ (value))
#define PSP_LANE_STEP_FNV(lane, value) ((lane) = ((lane) ^ (value)) * 16777619UL)
#define PSP_LANE_STEP_33(lane, value) ((lane) = ((lane) ^ (value)) * 33UL)
/* which sum: 0 the multiply by 33 (build 114), 1 FNV-1a ("fnvsum", b113),
2 b112's rotate and xor ("weaksum") */
#define PSP_SUM_33 0
#define PSP_SUM_FNV 1
#define PSP_SUM_WEAK 2
static int psp_checksum_kind = -1;

static int psp_checksum_kind_now(void)
{
	if (psp_checksum_kind < 0)
		psp_checksum_kind = psp_keep("weaksum") ? PSP_SUM_WEAK : psp_keep("fnvsum") ? PSP_SUM_FNV : PSP_SUM_33;
	return psp_checksum_kind;
}

/* one word into its lane, by the kind (for the few words the rules touch;
the long runs go through psp_checksum_span_lanes's loops) */
static void psp_lane_step(unsigned long lane[4], int kind, long index, unsigned long value)
{
	if (kind == PSP_SUM_FNV)
		PSP_LANE_STEP_FNV(lane[index & 3], value);
	else
		PSP_LANE_STEP_33(lane[index & 3], value);
}

static void psp_lanes_start(unsigned long lane[4])
{
	lane[0] = 0x811C9DC5UL; /* FNV's own start, and three others */
	lane[1] = 0x9E3779B9UL;
	lane[2] = 0x85EBCA6BUL;
	lane[3] = 0xC2B2AE35UL;
}

static unsigned long psp_lanes_end(unsigned long const lane[4])
{
	return lane[0] ^ (lane[1] << 8 | lane[1] >> 24) ^ (lane[2] << 16 | lane[2] >> 16) ^ (lane[3] << 24 | lane[3] >> 8);
}

/* the checksum word by word through the rules: the reference the fast one
below must equal ("checksumcheck" in keep.txt compares them) */
static unsigned long psp_object_checksum_reference(struct object_header_datum const *header)
{
	unsigned long const *word = (unsigned long const *)header->datum;
	unsigned long sum = 0x9E3779B9UL, lane[4];
	struct psp_checksum_rules rules;
	long index;

	int kind = psp_checksum_kind_now();

	psp_checksum_rules_for(header, &rules);
	if (kind == PSP_SUM_WEAK)
	{
		for (index = 0; index < rules.count; index++)
			sum = PSP_CHECKSUM_STEP(sum, psp_checksum_word(&rules, index, word[index]));
		return sum;
	}
	psp_lanes_start(lane);
	for (index = 0; index < rules.count; index++)
		psp_lane_step(lane, kind, index, psp_checksum_word(&rules, index, word[index]));
	return psp_lanes_end(lane);
}

/* build 106: the same sum, faster. The rules touch few words (word 2, and
for units and weapons a timer word, the idle count and a dead biped's aim,
functions and colours); every other word goes in as it is, so the runs
between the touched ones go through a plain loop, four words a turn, and
only the touched words through psp_checksum_word. b30's beach: 277 scenery
objects, most asleep, cost 5 ms a tick in the emulator, mostly this sum.
Build 113: the four-lane sum (psp_checksum_span_lanes), or "weaksum"'s */
static unsigned long psp_checksum_span(unsigned long sum, unsigned long const *word, long first, long end)
{
	long index = first;

	for (; index + 4 <= end; index += 4)
	{
		sum = PSP_CHECKSUM_STEP(sum, word[index]);
		sum = PSP_CHECKSUM_STEP(sum, word[index + 1]);
		sum = PSP_CHECKSUM_STEP(sum, word[index + 2]);
		sum = PSP_CHECKSUM_STEP(sum, word[index + 3]);
	}
	for (; index < end; index++)
		sum = PSP_CHECKSUM_STEP(sum, word[index]);
	return sum;
}

/* a run of words into the lanes, one function per kind so each loop is the
step alone (build 114: the 33 and FNV kinds) */
#define PSP_SPAN_LANES(name, STEP) \\
static void name(unsigned long lane[4], unsigned long const *word, long first, long end) \\
{ \\
	long index = first; \\
	unsigned long a, b, c, d; \\
	/* up to a multiple of 4, so the loop's words go to lanes 0 to 3 */ \\
	for (; index < end && (index & 3); index++) \\
		STEP(lane[index & 3], word[index]); \\
	a = lane[0]; b = lane[1]; c = lane[2]; d = lane[3]; \\
	for (; index + 4 <= end; index += 4) \\
	{ \\
		STEP(a, word[index]); \\
		STEP(b, word[index + 1]); \\
		STEP(c, word[index + 2]); \\
		STEP(d, word[index + 3]); \\
	} \\
	lane[0] = a; lane[1] = b; lane[2] = c; lane[3] = d; \\
	for (; index < end; index++) \\
		STEP(lane[index & 3], word[index]); \\
}
PSP_SPAN_LANES(psp_checksum_span_lanes_33, PSP_LANE_STEP_33)
PSP_SPAN_LANES(psp_checksum_span_lanes_fnv, PSP_LANE_STEP_FNV)
#undef PSP_SPAN_LANES

static void psp_checksum_span_lanes(unsigned long lane[4], int kind, unsigned long const *word, long first, long end)
{
	if (kind == PSP_SUM_FNV)
		psp_checksum_span_lanes_fnv(lane, word, first, end);
	else
		psp_checksum_span_lanes_33(lane, word, first, end);
}

static unsigned long psp_object_checksum(struct object_header_datum const *header)
{
	static int psp_checksum_check = -1;
	static unsigned long psp_checksum_checked = 0, psp_checksum_wrong = 0;
	static int psp_checksum_slow = -1;
	unsigned long const *word = (unsigned long const *)header->datum;
	unsigned long sum = 0x9E3779B9UL, lane[4];
	struct psp_checksum_rules rules;
	long touched[7][2], count = 0, index = 0, k, j;
	int kind = psp_checksum_kind_now(), weak = kind == PSP_SUM_WEAK;

	/* "slowchecksum": the reference alone, for timing the difference */
	if (psp_checksum_slow < 0)
		psp_checksum_slow = psp_keep("slowchecksum");
	if (psp_checksum_slow)
		return psp_object_checksum_reference(header);
	psp_checksum_rules_for(header, &rules);
	/* the touched ranges, in order */
	touched[count][0] = 2; touched[count][1] = 3; count++;
#define PSP_TOUCHED(first, end) if ((first) >= 0 && (end) > (first)) { touched[count][0] = (first); touched[count][1] = (end); count++; }
	PSP_TOUCHED(rules.half_skip, rules.half_skip + 1);
	PSP_TOUCHED(rules.whole_skip, rules.whole_skip + 1);
	PSP_TOUCHED(rules.idle_word, rules.idle_word + 1);
	PSP_TOUCHED(rules.aim_first, rules.aim_end);
	PSP_TOUCHED(rules.functions_first, rules.functions_end);
	PSP_TOUCHED(rules.colours_first, rules.colours_end);
#undef PSP_TOUCHED
	for (k = 1; k < count; k++)
		for (j = k; j > 0 && touched[j][0] < touched[j - 1][0]; j--)
		{
			long first = touched[j][0], end = touched[j][1];

			touched[j][0] = touched[j - 1][0]; touched[j][1] = touched[j - 1][1];
			touched[j - 1][0] = first; touched[j - 1][1] = end;
		}
	psp_lanes_start(lane);
	for (k = 0; k < count && index < rules.count; k++)
	{
		long first = touched[k][0] < index ? index : touched[k][0], end = touched[k][1];

		if (first > rules.count)
			first = rules.count;
		if (end > rules.count)
			end = rules.count;
		if (weak)
			sum = psp_checksum_span(sum, word, index, first);
		else
			psp_checksum_span_lanes(lane, kind, word, index, first);
		for (index = first; index < end; index++)
		{
			unsigned long value = psp_checksum_word(&rules, index, word[index]);

			if (weak)
				sum = PSP_CHECKSUM_STEP(sum, value);
			else
				psp_lane_step(lane, kind, index, value);
		}
		if (index < first)
			index = first;
	}
	if (weak)
		sum = psp_checksum_span(sum, word, index, rules.count);
	else
	{
		psp_checksum_span_lanes(lane, kind, word, index, rules.count);
		sum = psp_lanes_end(lane);
	}
	if (psp_checksum_check < 0)
		psp_checksum_check = psp_keep("checksumcheck");
	if (psp_checksum_check)
	{
		psp_checksum_checked++;
		if (psp_object_checksum_reference(header) != sum && psp_checksum_wrong++ < 8)
			platform_psp_log("sleep: the fast checksum differs from the reference for %s (type %d)",
				tag_get_name(((struct object_datum const *)header->datum)->definition_index), header->type);
		if (psp_checksum_checked % 100000 == 0)
			platform_psp_log("sleep: checksumcheck: %lu sums compared, %lu differed", psp_checksum_checked, psp_checksum_wrong);
	}
	return sum;
}

static boolean object_update(long object_index);
static void psp_sleepy_update(long object_index, short absolute_index, struct object_header_datum *header, int may_sleep);

/* build 114: a body sleeps only while the level itself holds it up.
A sleeping body skips its whole update, and its own bytes don't change when
what it rests on goes away, so a body that came to rest on something else
(another body that was cleared away, a vehicle, the player) stayed in the
air (Sheepdog01's b113 Test 3: an Elite's body hanging above the beach). On the
Xbox its physics runs every tick and finds itself airborne the tick its
support goes (bipeds.c: the at-rest bit is worked out afresh each tick).
The level never moves, so a body it holds can only start moving if
something changes the body itself, which wakes it. The test is the biped's
own for its support (bipeds.c): a sphere at the foot of its physics pill,
the pill's radius plus 0.05, against the level, broken surfaces left out.
It runs once as a body settles, and again only after the body has moved
(the game zeroes its idle count whenever it isn't at rest). "corpseanyground"
in keep.txt: any support, as b105 to b113 */
#include "units/bipeds.h"
#include "physics/breakable_surfaces.h"
static struct collision_bsp_test_sphere_result psp_ground_result; /* 4 KB: off the stack */

static int psp_corpse_on_level(long object_index, short absolute_index, struct object_header_datum const *header)
{
	static int psp_any_ground = -1;
	real_point3d base;
	real height, width;
	int on_level;

	if (psp_any_ground < 0)
		psp_any_ground = psp_keep("corpseanyground");
	if (psp_any_ground)
		return 1;
	if (absolute_index >= MAXIMUM_OBJECTS_PER_MAP)
		return 0;
	if (psp_sleep[absolute_index].identifier != header->identifier)
	{
		/* a slot of its own from now (as psp_sleepy_update would start it) */
		psp_sleep[absolute_index].identifier = header->identifier;
		psp_sleep[absolute_index].quiet = 0;
		psp_sleep[absolute_index].ground = 0;
	}
	if (!psp_sleep[absolute_index].ground)
	{
		biped_get_physics_pill(object_index, &base, &height, &width);
		on_level = collision_bsp_test_sphere(global_collision_bsp_get(), MAXIMUM_BREAKABLE_SURFACES_PER_MAP,
			breakable_surface_flags_get(), &base, width + 0.05f, &psp_ground_result) &&
			psp_ground_result.surface_count > 0;
		psp_sleep[absolute_index].ground = on_level ? 1 : 2;
		psp_corpse_ground_tests++;
	}
	if (psp_sleep[absolute_index].ground == 2)
	{
		psp_corpses_off_level++;
		return 0;
	}
	return 1;
}

/* a body that isn't settled (it moved, or hasn't been at rest 2 s): asked again when it settles */
static void psp_corpse_unsettled(short absolute_index, struct object_header_datum const *header)
{
	if (absolute_index < MAXIMUM_OBJECTS_PER_MAP && psp_sleep[absolute_index].identifier == header->identifier)
		psp_sleep[absolute_index].ground = 0;
}

int psp_crumb_mark(const char *what, const char *detail); /* psp_trace.c (build 101) */
extern int psp_crumbs_on; /* build 106: the name is only looked up while crumbs are on */
static void psp_object_update(long object_index, short absolute_index, struct object_header_datum *header)
{
	struct object_datum const *object = header->datum;
	unsigned long before;

	/* build 101: "crumbs": which object, on the screen as it updates */
	if (psp_crumbs_on > 0 && header->type != _object_type_scenery && header->type != _object_type_machine &&
		header->type != _object_type_sound_scenery)
		psp_crumb_mark("object", tag_get_name(object->definition_index));

	if (psp_sleep_enabled < 0)
		psp_sleep_enabled = !psp_keep("awake");
	if (psp_vehicle_sleep_on < 0)
		psp_vehicle_sleep_on = !psp_keep("vehiclesawake");
	if (header->type == _object_type_vehicle)
	{
		/* build 40: each vehicle kind's update time, parked (at rest) or
		not (Sheepdog01: the warthog and the scorpion cost far more than the ghost;
		their physics tags have 15 and 18 mass points to the ghost's 5,
		each tested against the ground every tick) */
		static struct { long definition; unsigned long us[2], count[2]; } psp_vehicle_cost[8];
		static unsigned long psp_vehicle_updates = 0;
		unsigned long psp_start = psp_microseconds();
		int psp_resting = TEST_FLAG(object->object.flags, _object_at_rest_bit) ? 1 : 0, k;
		long definition = object->definition_index;

		/* build 40: a parked, empty vehicle sleeps as idle scenery does
		(its update leaving it byte-identical twice running); anyone getting
		in, a shot, a blast's push changes its data and wakes it. The 8
		parked vehicles cost 4.7 ms a tick on the PSP (build 39) */
		/* build 44: on. Of the two counters a parked vehicle keeps (build
		41), unit.timer is left out of the checksum (above); the other, the
		animation frame (+0x80: the scorpion's idle), keeps a vehicle awake,
		so only the warthog and the ghost sleep for now. keep.txt's
		"vehiclesawake" turns it off */
		/* build 89: and only once the game's physics calls it at rest. The
		byte-identical test alone also put to sleep a vehicle the physics
		keeps awake on purpose (touching a machine or a breakable surface):
		a lift moving under it, or another vehicle driving into it, then
		didn't move it (code review) */
		psp_sleepy_update(object_index, absolute_index, header, psp_vehicle_sleep_on && psp_resting);
		for (k = 0; k < 8 && psp_vehicle_cost[k].definition && psp_vehicle_cost[k].definition != definition; k++);
		if (k < 8)
		{
			psp_vehicle_cost[k].definition = definition;
			psp_vehicle_cost[k].us[psp_resting] += psp_microseconds() - psp_start;
			psp_vehicle_cost[k].count[psp_resting]++;
		}
		if (++psp_vehicle_updates % 3000 == 0)
		{
			for (k = 0; k < 8 && psp_vehicle_cost[k].definition; k++)
			{
				platform_psp_log("vehicles: %s: moving %lu updates at %lu us, at rest %lu at %lu us",
					tag_get_name(psp_vehicle_cost[k].definition),
					psp_vehicle_cost[k].count[0], psp_vehicle_cost[k].count[0] ? psp_vehicle_cost[k].us[0] / psp_vehicle_cost[k].count[0] : 0,
					psp_vehicle_cost[k].count[1], psp_vehicle_cost[k].count[1] ? psp_vehicle_cost[k].us[1] / psp_vehicle_cost[k].count[1] : 0);
				psp_vehicle_cost[k].us[0] = psp_vehicle_cost[k].us[1] = 0;
				psp_vehicle_cost[k].count[0] = psp_vehicle_cost[k].count[1] = 0;
			}
		}
		return;
	}
	/* build 102: idle machines sleep too, as scenery does: a machine whose
	update leaves it byte-identical twice running is skipped until something
	changes it. Sheepdog01's b101 Library combat: 61 machines (doors, lifts, the
	elevator's parts) cost 7.5 ms a tick. What a machine reads besides its own
	bytes is covered: its power and position groups (a switch, a script) go
	into the checksum (psp_family_checksum); an automatic door still runs on
	the ticks it looks for someone near (one in four, machine_update), so it
	opens as before; a platform (always moving) never sleeps. Measured in the
	emulator on The Library: no gain (machines 1.73 ms a tick either way):
	60% of its machine updates are type 2, always moving, and the idle ones
	were cheap. So off unless keep.txt says "machinesleep" */
	if (header->type == _object_type_machine)
	{
		static int psp_machine_sleep_on = -1;
		int may = 0;

		if (psp_machine_sleep_on < 0)
			psp_machine_sleep_on = psp_keep("machinesleep");
		if (psp_machine_sleep_on)
		{
			struct machine_datum const *machine = (struct machine_datum const *)object;
			struct machine_definition const *definition = machine_definition_get(machine->definition_index);

			may = definition->machine.type != 2 /* platform */ &&
				!(definition->machine.type == 0 /* door */ && !TEST_FLAG(machine->machine.flags, 0 /* does not operate automatically */) &&
					((game_time_get() + object_index) & 3) == 0);
		}
		{
			/* build 102: why machines stay awake, every 3000 machine updates */
			static unsigned long psp_machine_seen, psp_machine_gear, psp_machine_door_tick, psp_machine_children, psp_machine_by_type[4];
			struct machine_datum const *machine = (struct machine_datum const *)object;
			struct machine_definition const *definition = machine_definition_get(machine->definition_index);

			psp_machine_seen++;
			psp_machine_by_type[definition->machine.type & 3]++;
			if (definition->machine.type == 2)
				psp_machine_gear++;
			else if (!may)
				psp_machine_door_tick++;
			else if (object->object.first_child_object_index != NONE)
				psp_machine_children++;
			if (psp_machine_seen % 3000 == 0)
			{
				platform_psp_log("machines: of %lu updates, by type %lu %lu %lu %lu; type 2 (always moving) %lu, door's look-around tick %lu, carrying something %lu; the rest may sleep (asleep counts: objects_update's line)",
					psp_machine_seen, psp_machine_by_type[0], psp_machine_by_type[1], psp_machine_by_type[2], psp_machine_by_type[3],
					psp_machine_gear, psp_machine_door_tick, psp_machine_children);
				psp_machine_seen = psp_machine_gear = psp_machine_door_tick = psp_machine_children = 0;
				psp_machine_by_type[0] = psp_machine_by_type[1] = psp_machine_by_type[2] = psp_machine_by_type[3] = 0;
			}
		}
		psp_sleepy_update(object_index, absolute_index, header, may);
		return;
	}
	/* build 105: a settled corpse sleeps as idle scenery does. Sheepdog01's b102
	run 3: 116 biped updates a tick in the Library's first Flood fight with
	at most 35 actors, about 0.74 ms each; the garbage collector never takes
	a body a player can see. Only a truly dead one (garbage: a Flood combat
	form feigning death is not), at rest for 2 s by the game's own count,
	carrying nothing; the byte-identical rule then decides, and a push or
	damage wakes it. Campaign only (no game engine). "corpsesawake": off.
	Build 112: also the bodies the level placed dead (set dressing: units.c
	gives them _unit_placed_here_dead_bit and bars them from the garbage
	list, so the collector never takes them and the garbage rule never let
	them sleep). b108 run 1, b30's security room: 20 bodies, 13 garbage and
	asleep, 7 placed dead and updated every tick. They are dead from the
	start, so no feigning; the byte-identical rule still decides, and a push
	or damage wakes them. "placeddeadawake": off */
	if (header->type == _object_type_biped)
	{
		static int psp_corpse_sleep_on = -1, psp_placed_dead_sleep_on = -1;
		int may = 0;

		if (psp_corpse_sleep_on < 0)
		{
			psp_corpse_sleep_on = !psp_keep("corpsesawake");
			psp_placed_dead_sleep_on = !psp_keep("placeddeadawake");
		}
		if (psp_corpse_sleep_on && !game_engine_running() && TEST_FLAG(object->object.damage_flags, _object_dead_bit))
		{
			int garbage = TEST_FLAG(object->object.flags, _object_garbage_bit);
			int placed_dead = !garbage && psp_placed_dead_sleep_on &&
				TEST_FLAG(((struct unit_datum const *)object)->unit.flags, _unit_placed_here_dead_bit);

			psp_corpse_stage[0]++;
			if (garbage)
				psp_corpse_stage[1]++;
			if (placed_dead)
				psp_corpses_placed_dead++;
			if (garbage || placed_dead)
			{
				if (TEST_FLAG(object->object.flags, _object_at_rest_bit) && object->object.idle_ticks > 60)
				{
					psp_corpse_stage[2]++;
					psp_corpse_stage[3]++;
					may = psp_corpse_on_level(object_index, absolute_index, header);
				}
				else
				{
					if (TEST_FLAG(object->object.flags, _object_at_rest_bit))
						psp_corpse_stage[2]++;
					psp_corpse_unsettled(absolute_index, header);
				}
			}
		}
		psp_sleepy_update(object_index, absolute_index, header, may);
		return;
	}
	psp_sleepy_update(object_index, absolute_index, header, header->type == _object_type_scenery);
}

/* an object and what it carries (a parked vehicle's own gun, build 41),
summed together; 0 when it can't sleep: it carries a unit (someone in a
seat) or something carrying something else */
static unsigned long psp_family_checksum(struct object_header_datum const *header)
{
	struct object_datum const *object = header->datum;
	unsigned long sum = psp_object_checksum(header);
	long child = object->object.first_child_object_index;
	int children = 0;

	while (child != NONE)
	{
		struct object_header_datum const *child_header = object_header_get(child);
		struct object_datum const *child_object = child_header->datum;

		if (child_header->type == _object_type_biped || child_header->type == _object_type_vehicle ||
			child_object->object.first_child_object_index != NONE || ++children > 4)
		{
			if (psp_sleep_debug > 0 && header->type == _object_type_vehicle && game_time_get() > 150 && psp_vehicle_sleep_reports++ < 24)
				platform_psp_log("sleep: %s can't sleep: it carries %s (type %d, which itself carries %s)",
					tag_get_name(object->definition_index), tag_get_name(child_object->definition_index), child_header->type,
					child_object->object.first_child_object_index != NONE ?
						tag_get_name(object_get(child_object->object.first_child_object_index)->definition_index) : "nothing");
			return 0;
		}
		sum = ((sum << 7) | (sum >> 25)) ^ psp_object_checksum(child_header);
		child = child_object->object.next_object_index;
	}
	/* build 102: a machine's power and position groups, which its update
	reads and a switch or a script changes without touching the machine */
	if (header->type == _object_type_machine)
	{
		struct device_datum const *device = (struct device_datum const *)object;
		short groups[2];
		int k;

		groups[0] = device->device.power_group_index;
		groups[1] = device->device.position_group_index;
		for (k = 0; k < 2; k++)
			if (groups[k] != NONE)
			{
				unsigned long const *group = (unsigned long const *)datum_get(device_groups_data, groups[k]);

				sum = ((sum << 9) | (sum >> 23)) ^ group[0] ^ (group[1] * 0x9E3779B1UL);
			}
	}
	return sum ? sum : 1;
}

/* build 107: keep.txt's "sceneryhalf": sleeping scenery is checked on
alternate ticks, half of it each tick (by its index), and stays asleep
unchecked on the other. The check (the checksum of the whole datum) is most
of what sleeping scenery costs: 2.3% of b30's beach [E]. What it trades:
anything done to a scenery object (a script, a blast) is noticed up to a
tick later than before. Off by default. The counts are the heartbeat's
"sleeping scenery" line */
long psp_scenery_unchecked, psp_scenery_woken;
static int psp_scenery_half = -1;

static int psp_scenery_skips_check(struct object_header_datum const *header, short absolute_index, int may_sleep)
{
	struct object_datum const *object = header->datum;
	/* the double speed powerup updates objects on even ticks only, so the
	half by game time would be the same half every update */
	long update = game_players_are_double_speed() ? game_time_get() >> 1 : game_time_get();

	if (psp_scenery_half < 0)
		psp_scenery_half = psp_keep("sceneryhalf");
	return psp_scenery_half && psp_sleep_enabled && may_sleep && header->type == _object_type_scenery &&
		absolute_index < MAXIMUM_OBJECTS_PER_MAP && object->object.first_child_object_index == NONE &&
		psp_sleep[absolute_index].identifier == header->identifier && psp_sleep[absolute_index].quiet >= 2 &&
		((update + absolute_index) & 1) != 0;
}

static void psp_sleepy_update(long object_index, short absolute_index, struct object_header_datum *header, int may_sleep)
{
	struct object_datum const *object = header->datum;
	unsigned long before = 0;
	int corpse_reported = 0;
	int unchecked = psp_scenery_skips_check(header, absolute_index, may_sleep);

	if (unchecked)
		psp_scenery_unchecked++;
	else if (psp_sleep_enabled && may_sleep && absolute_index < MAXIMUM_OBJECTS_PER_MAP &&
		(object->object.first_child_object_index == NONE || header->type == _object_type_vehicle))
		before = psp_family_checksum(header);
	if (!before && !unchecked)
	{
		if (may_sleep && header->type == _object_type_biped && object->object.first_child_object_index != NONE)
		{
			/* build 106: a corpse still holding something can't sleep (the
			checksum covers the object alone): counted for the report */
			extern long psp_corpses_holding;

			psp_corpses_holding++;
		}
		object_update(object_index);
		if (absolute_index < MAXIMUM_OBJECTS_PER_MAP)
			psp_sleep[absolute_index].quiet = 0;
		return;
	}
	psp_objects_sleep_checked++;
	if (psp_sleep_debug > 0 && header->type == _object_type_vehicle && game_time_get() > 150)
	{
		/* what changed since the last tick's end, outside the update */
		long count = (header->data_size < 2560 ? header->data_size : 2560) / 4, w;

		if (between_index == absolute_index && between_identifier == header->identifier)
		{
			for (w = 0; w < count; w++)
				if (between[w] != ((unsigned long const *)header->datum)[w] && w != 2 && psp_vehicle_sleep_reports++ < 40)
					platform_psp_log("sleep: %s changed between ticks: word %ld (byte %ld) %08lx -> %08lx (quiet %d, checksum %s)",
						tag_get_name(object->definition_index), w, w * 4, between[w], ((unsigned long const *)header->datum)[w],
						psp_sleep[absolute_index].quiet, psp_sleep[absolute_index].checksum == before ? "same" : "differs");
		}
		else if (between_index < 0)
		{
			between_index = absolute_index;
			between_identifier = header->identifier;
		}
	}
	if (unchecked || (psp_sleep[absolute_index].identifier == header->identifier &&
		psp_sleep[absolute_index].quiet >= 2 && psp_sleep[absolute_index].checksum == before))
	{
		struct object_datum *sleeper = (struct object_datum *)header->datum;

		psp_objects_asleep++;
		/* build 106: object_update counts the active garbage it updates, and
		the garbage collector starts at 50 of them: a sleeping wreck or corpse
		still counts, so collection starts when it did on the Xbox */
		if (TEST_FLAG(sleeper->object.flags, _object_garbage_bit))
			++object_globals->active_garbage_object_count;
		if (header->type == _object_type_vehicle)
			psp_vehicles_asleep++;
		if (header->type == _object_type_biped)
		{
			/* build 105: the corpse's idle count goes on as the update would
			have kept it (the checksum leaves it out) */
			if (sleeper->object.idle_ticks < 32767)
				sleeper->object.idle_ticks++;
			/* build 106: and its functions are computed as the update would
			(a Flood body's first function cycles for ever, and its shaders
			read it): only the expensive parts sleep, the pose, the physics,
			the animation and the AI's unit work */
			object_type_export_function_values(object_index);
			object_compute_function_values(object_index);
			object_compute_change_colors(object_index);
			psp_corpses_asleep++;
		}
		return;
	}
	/* sceneryhalf: sleeping scenery that something changed (and that may
	have waited a tick unchecked) */
	if (psp_scenery_half && header->type == _object_type_scenery &&
		psp_sleep[absolute_index].identifier == header->identifier && psp_sleep[absolute_index].quiet >= 2)
		psp_scenery_woken++;
	if (psp_sleep_debug < 0)
		psp_sleep_debug = psp_keep("sleepdebug");
	if (psp_sleep_debug && header->type == _object_type_biped && may_sleep && game_time_get() > 150)
	{
		/* build 106: a corpse that stays awake: every word its update
		changed that the checksum compares (one try in 300, 12 at most) */
		static int biped_reports = 0;
		static long biped_tries = 0;

		if (biped_reports < 12 && biped_tries++ % 300 == 0)
		{
			static unsigned long copy[512];
			long count = (header->data_size < 2048 ? header->data_size : 2048) / 4, word, shown = 0;
			unsigned long after_sum;

			struct psp_checksum_rules rules;

			csmemcpy(copy, header->datum, count * 4);
			object_update(object_index);
			after_sum = psp_object_checksum(header);
			psp_checksum_rules_for(header, &rules);
			biped_reports++;
			for (word = 0; word < count && shown < 24; word++)
				if (psp_checksum_word(&rules, word, copy[word]) != psp_checksum_word(&rules, word, ((unsigned long const *)header->datum)[word]))
				{
					platform_psp_log("sleep: corpse %s word %ld (byte %ld) of %d changed, %08lx -> %08lx",
						tag_get_name(object->definition_index), word, word * 4, header->data_size,
						copy[word], ((unsigned long const *)header->datum)[word]);
					shown++;
				}
			platform_psp_log("sleep: corpse %s: %ld changed words shown, checksum %s (quiet %d)", tag_get_name(object->definition_index),
				shown, after_sum == before ? "same" : "differs", psp_sleep[absolute_index].quiet);
			corpse_reported = 1;
		}
	}
	if (corpse_reported)
		; /* updated above */
	else if (psp_sleep_debug && (header->type == _object_type_vehicle ? (game_time_get() > 150 && psp_vehicle_sleep_reports++ < 24) : psp_sleep_reports < 12))
	{
		static unsigned long copy[512];
		long count = (header->data_size < 2048 ? header->data_size : 2048) / 4, word;

		static unsigned long child_copy[512];
		long child_count = 0, child_index = object->object.first_child_object_index;

		if (header->type == _object_type_vehicle && child_index != NONE)
		{
			struct object_header_datum const *child_header = object_header_get(child_index);

			child_count = (child_header->data_size < 2048 ? child_header->data_size : 2048) / 4;
			csmemcpy(child_copy, child_header->datum, child_count * 4);
		}
		csmemcpy(copy, header->datum, count * 4);
		object_update(object_index);
		if (child_count)
		{
			struct object_header_datum const *child_header = object_header_get(child_index);
			long w;

			for (w = 0; w < child_count; w++)
				if (child_copy[w] != ((unsigned long const *)child_header->datum)[w] &&
					!(child_header->type == _object_type_weapon && w == 0x1B4 / 4))
				{
					platform_psp_log("sleep: %s's child word %ld (byte %ld) of %d changed, %08lx -> %08lx",
						tag_get_name(object->definition_index), w, w * 4, child_header->data_size, child_copy[w],
						((unsigned long const *)child_header->datum)[w]);
					break;
				}
		}
		for (word = 0; word < count; word++)
		{
			if (copy[word] != ((unsigned long const *)header->datum)[word] && word != 2 && word != (header->type == _object_type_vehicle ? 0x1BC / 4 : -1))
			{
				if (game_time_get() > 150 && (psp_sleep_reports++ & 0) == 0)
					platform_psp_log("sleep: %s stays awake: word %ld (byte %ld) of %d changed, %08lx -> %08lx",
						tag_get_name(object->definition_index), word, word * 4, header->data_size,
						copy[word], ((unsigned long const *)header->datum)[word]);
				break;
			}
		}
		if (word == count && header->type == _object_type_vehicle)
		{
			/* the vehicle itself held still: what it carries moved */
			long child = object->object.first_child_object_index;

			while (child != NONE)
			{
				struct object_header_datum const *child_header = object_header_get(child);
				struct object_datum const *child_object = child_header->datum;
				static unsigned long child_sum[64];
				int slot = child & 63;
				unsigned long sum = psp_object_checksum(child_header);

				if (child_sum[slot] && child_sum[slot] != sum)
					platform_psp_log("sleep: %s stays awake: its %s (type %d) changed", tag_get_name(object->definition_index),
						tag_get_name(child_object->definition_index), child_header->type);
				child_sum[slot] = sum;
				child = child_object->object.next_object_index;
			}
		}
	}
	else
		object_update(object_index);
	if (psp_family_checksum(header) == before)
	{
		if (psp_sleep[absolute_index].identifier == header->identifier)
		{
			if (psp_sleep[absolute_index].quiet < 2)
				psp_sleep[absolute_index].quiet++;
		}
		else
		{
			psp_sleep[absolute_index].identifier = header->identifier;
			psp_sleep[absolute_index].quiet = 1;
			psp_sleep[absolute_index].ground = 0; /* build 114: a new object in the slot */
		}
	}
	else
	{
		if (psp_sleep[absolute_index].identifier != header->identifier)
			psp_sleep[absolute_index].ground = 0;
		psp_sleep[absolute_index].identifier = header->identifier;
		psp_sleep[absolute_index].quiet = 0;
	}
	psp_sleep[absolute_index].checksum = before;
	if (psp_sleep_debug > 0 && header->type == _object_type_vehicle && between_index == absolute_index)
		csmemcpy(between, header->datum, (header->data_size < 2560 ? header->data_size : 2560));
}
#endif
"""

FIXES_19_SKY = {
"source/render/render_sky.c": [
("""			rasterizer_models_begin(TRUE);
			csmemset(&render_model_lighting, 0, sizeof(render_model_lighting));""",
 """#ifdef HALO_PSP
			{
				/* the sky on the GE: at the far plane, behind everything (build 21; psp_fixups.py) */
				extern void psp_ge_sky(int on);

				psp_ge_sky(1);
			}
#endif
			rasterizer_models_begin(TRUE);
			csmemset(&render_model_lighting, 0, sizeof(render_model_lighting));"""),
("""			rasterizer_models_end();
		}
	}

	return;
}""",
 """			rasterizer_models_end();
#ifdef HALO_PSP
			{
				extern void psp_ge_sky(int on);

				psp_ge_sky(0);
			}
#endif
		}
	}

	return;
}"""),
],
}

FIXES_22 = {
"source/render/render_sprite.c": [
("""void build_sprites_end(
	struct build_sprite_data *data)
{""",
 """#ifdef HALO_PSP
int psp_ge_active(void);
int psp_ge_sprites(void const *vertices, int sprite_count, unsigned int bitmap_key, short blend, float const *view_to_world);
#endif

void build_sprites_end(
	struct build_sprite_data *data)
{"""),
("""			rasterizer_dynamic_vertices_unlock(group->vertex_buffer_index);
			if (TEST_FLAG(data->flags, _build_sprites_screen_space_bit))""",
 """#ifdef HALO_PSP
			/* sprites (particles, muzzle flashes, impacts) go to the GE's list of
			see-through draws; they are built in view space (psp_ge.c; build 22;
			psp_fixups.py) */
			if (psp_ge_active() && !TEST_FLAG(data->flags, _build_sprites_screen_space_bit))
			{
				psp_ge_sprites(group->vertices, group->sprite_count, (unsigned int)group->bitmap,
					data->shader ? data->shader->framebuffer_blend_function : 0,
					(float const *)&render.frustum.view_to_world);
				rasterizer_dynamic_vertices_unlock(group->vertex_buffer_index);
				rasterizer_dynamic_vertices_delete(group->vertex_buffer_index);
				continue;
			}
#endif
			rasterizer_dynamic_vertices_unlock(group->vertex_buffer_index);
			if (TEST_FLAG(data->flags, _build_sprites_screen_space_bit))"""),
],
}

FIXES_19 = {
"source/render/render_objects.c": [
("void render_object_shadows(\n\tvoid)\n{",
 KEEP_DECLARATION + "\nvoid render_object_shadows(\n\tvoid)\n{"),
("\tprofile_enter(render_object_shadows_section);\n\n\tif (render_shadows)\n",
 "\tprofile_enter(render_object_shadows_section);\n\n\tif (render_shadows\n#ifdef HALO_PSP\n\t\t&& (!psp_ge_active() || !PSP_KEEP_ONCE(\"noshadows\")) /* the GE's blob shadows since build 25 (psp_fixups.py) */\n#endif\n\t\t)\n"),
],
"source/rasterizer/rasterizer_transparent_geometry.c": [
("void rasterizer_transparent_geometry_draw(\n\tboolean water)\n{",
 KEEP_DECLARATION + "\nvoid rasterizer_transparent_geometry_draw(\n\tboolean water)\n{"),
("\trasterizer_profile_begin((short)profile);\n\tif (transparent_geometry_group_count > 0)\n",
 "\trasterizer_profile_begin((short)profile);\n#ifdef HALO_PSP\n\tif (psp_ge_active() && !PSP_KEEP_ONCE(\"transparent\"))\n\t{\n\t\t/* nothing draws them on the PSP yet (psp_fixups.py) */\n\t\ttransparent_geometry_group_index = (short)transparent_geometry_group_count;\n\t\trasterizer_profile_end((short)profile);\n\t\treturn;\n\t}\n#endif\n\tif (transparent_geometry_group_count > 0)\n"),
],
"source/render/render_particles.c": [
("void render_particles(\n\tvoid)\n{",
 KEEP_DECLARATION + "\nvoid render_particles(\n\tvoid)\n{"),
("\treal_vector3d direction;\n\n\trendered_particle_count = 0;\n",
 "\treal_vector3d direction;\n\n#ifdef HALO_PSP\n\tif (psp_ge_active() && PSP_KEEP_ONCE(\"noparticles\"))\n\t\treturn; /* noparticles in keep.txt: none (psp_fixups.py) */\n#endif\n\trendered_particle_count = 0;\n"),
],
"source/rasterizer/rasterizer.c": [
("void rasterizer_decals_draw(\n\tshort cluster_index)\n{",
 KEEP_DECLARATION + "\nvoid rasterizer_decals_draw(\n\tshort cluster_index)\n{"),
("\t_rasterizer_decals_draw(cluster_index);\n",
 "#ifdef HALO_PSP\n\tif (psp_ge_active() && PSP_KEEP_ONCE(\"nodecals\"))\n\t\treturn; /* the GE draws them since build 23; nodecals leaves them out (psp_fixups.py) */\n#endif\n\t_rasterizer_decals_draw(cluster_index);\n"),
],
"source/interface/interface.c": [
("void interface_draw_screen(\n\tvoid)\n{",
 KEEP_DECLARATION + "\nvoid interface_draw_screen(\n\tvoid)\n{"),
("\tif (render.local_player_index == NONE)\n\t\treturn;\n\n\tweapon_hud_index = interface_get_weapon_hud_index(&flashlight_power);",
 "\tif (render.local_player_index == NONE)\n\t\treturn;\n#ifdef HALO_PSP\n\tif (psp_ge_active() && PSP_KEEP_ONCE(\"nohud\"))\n\t\treturn; /* nohud in keep.txt: no HUD (psp_fixups.py) */\n#endif\n\n\tweapon_hud_index = interface_get_weapon_hud_index(&flashlight_power);"),
],
"source/cache/xbox_texture_cache.c": [
("void *_texture_cache_bitmap_get_hardware_format(\n\tstruct bitmap_data *bitmap,",
 KEEP_DECLARATION + "\nvoid *_texture_cache_bitmap_get_hardware_format(\n\tstruct bitmap_data *bitmap,"),
Either(universal=("\tif (TEST_FLAG(bitmap->flags, _bitmap_cached_bit))\n\t{\n\t\tif (bitmap->cache_block_index == NONE && load)",
 "#ifdef HALO_PSP\n\t/* the null path's textures are never drawn, and a blocking load waits on\n\tthe Memory Stick (psp_fixups.py) */\n\tif (psp_ge_active() && !PSP_KEEP_ONCE(\"textures\"))\n\t{\n\t\t/* a stand-in, never drawn with: the HUD checks a texture is there before\n\t\tit draws, and the GE takes its textures from the map's texture pack\n\t\t(build 20) */\n\t\tstatic unsigned long psp_texture_stand_in[16];\n\n\t\treturn psp_texture_stand_in;\n\t}\n#endif\n\tif (TEST_FLAG(bitmap->flags, _bitmap_cached_bit))\n\t{\n\t\tif (bitmap->cache_block_index == NONE && load)"),
 # ChupathingyCE
 chupa=("""	if (TEST_FLAG(bitmap->flags, _bitmap_cached_bit))
	{
		/* port: a cache block that isn't this bitmap's""",
  """#ifdef HALO_PSP
	/* the null path's textures are never drawn, and a blocking load waits on
	the Memory Stick (psp_fixups.py) */
	if (psp_ge_active() && !PSP_KEEP_ONCE("textures"))
	{
		/* a stand-in, never drawn with: the HUD checks a texture is there before
		it draws, and the GE takes its textures from the map's texture pack
		(build 20) */
		static unsigned long psp_texture_stand_in[16];

		return psp_texture_stand_in;
	}
#endif
	if (TEST_FLAG(bitmap->flags, _bitmap_cached_bit))
	{
		/* port: a cache block that isn't this bitmap's""")),
],
"port/linux/src/sdl_platform.c": [
("int halo_interpolation_enabled(void)\n{",
 KEEP_DECLARATION + "\nint halo_interpolation_enabled(void)\n{"),
Either(universal=("\tif (enabled < 0)\n\t\tenabled = config_boolean(\"display.interpolation\");",
 "#ifdef HALO_PSP\n\t/* off unless keep.txt names it: the PSP draws fewer frames than ticks\n\t(psp_fixups.py) */\n\tif (enabled < 0)\n\t\tenabled = psp_keep(\"interpolation\");\n#else\n\tif (enabled < 0)\n\t\tenabled = config_boolean(\"display.interpolation\");\n#endif"),
 # ChupathingyCE
 chupa=("""	if (read_at != config_changes())
	{
		read_at = config_changes();
		enabled = config_boolean("display.interpolation");
	}""",
  """#ifdef HALO_PSP
	/* off unless keep.txt names it: the PSP draws fewer frames than ticks
	(psp_fixups.py) */
	if (read_at == (unsigned long)-1)
	{
		read_at = 0;
		enabled = psp_keep("interpolation");
	}
#else
	if (read_at != config_changes())
	{
		read_at = config_changes();
		enabled = config_boolean("display.interpolation");
	}
#endif""")),
],
"source/objects/objects.c": [
("void objects_update(\n\tvoid)\n{",
 PSP_SLEEP_CODE + "\nvoid objects_update(\n\tvoid)\n{"),
("\tif (csmemcmp(last_active_cluster_bits, active_cluster_bits, BIT_VECTOR_SIZE_IN_BYTES(cluster_count)))\n",
 "#ifdef HALO_PSP\n\tpsp_object_start = psp_microseconds();\n#endif\n\tif (csmemcmp(last_active_cluster_bits, active_cluster_bits, BIT_VECTOR_SIZE_IN_BYTES(cluster_count)))\n"),
("\t\tstructure_decals_update(\n\t\t\tlast_active_cluster_bits,\n\t\t\tactive_cluster_bits,\n\t\t\tcluster_count);\n\t}\n",
 "\t\tstructure_decals_update(\n\t\t\tlast_active_cluster_bits,\n\t\t\tactive_cluster_bits,\n\t\t\tcluster_count);\n\t}\n#ifdef HALO_PSP\n\tpsp_object_account(12, psp_microseconds() - psp_object_start);\n#endif\n"),
("\t\t\t\t\tobject_update(object_index);\n",
 "#ifdef HALO_PSP\n\t\t\t\t\tpsp_object_start = psp_microseconds();\n\t\t\t\t\tpsp_object_update(object_index, i, object_header);\n\t\t\t\t\tpsp_object_account(object_header->type, psp_microseconds() - psp_object_start);\n#else\n\t\t\t\t\tobject_update(object_index);\n#endif\n"),
("\tobjects_garbage_collection();\n\n\tprofile_exit(section);",
 "#ifdef HALO_PSP\n\tpsp_object_start = psp_microseconds();\n\tobjects_garbage_collection();\n\tpsp_object_account(13, psp_microseconds() - psp_object_start);\n#else\n\tobjects_garbage_collection();\n#endif\n\n\tprofile_exit(section);"),
],
"port/include/halo_math.h": [
("#define pow(x, y) halo_pow(x, y)\n",
 """#define pow(x, y) halo_pow(x, y)

#ifdef HALO_PSP
/* single precision for float arguments (build 19; psp_fixups.py). The PSP's
FPU has no double precision: a double sqrt is a 2,400-cycle library call,
and the compiler does not narrow (real)sqrt(f) or (real)floor(f) itself.
sqrt, floor, ceil and fmod are exact, so the float versions give the same
bits; the halo_ trig functions already compute in float (psp_libc.c), so
calling sinf and the rest directly only drops the conversions. Arguments
that really are double keep the double functions. */
#define PSP_IS_FLOAT(x) __builtin_types_compatible_p(__typeof__(x), float)
#define PSP_NARROW1(F, D, x) __builtin_choose_expr(PSP_IS_FLOAT(x), F##f(x), D(x))
#define PSP_NARROW2(F, D, x, y) __builtin_choose_expr(PSP_IS_FLOAT((x) + (y)), F##f(x, y), D(x, y))
#undef sin
#undef cos
#undef tan
#undef asin
#undef acos
#undef atan
#undef atan2
#undef pow
#define sin(x) PSP_NARROW1(sin, halo_sin, x)
#define cos(x) PSP_NARROW1(cos, halo_cos, x)
#define tan(x) PSP_NARROW1(tan, halo_tan, x)
#define asin(x) PSP_NARROW1(asin, halo_asin, x)
#define acos(x) PSP_NARROW1(acos, halo_acos, x)
#define atan(x) PSP_NARROW1(atan, halo_atan, x)
#define atan2(y, x) PSP_NARROW2(atan2, halo_atan2, y, x)
#define pow(x, y) PSP_NARROW2(pow, halo_pow, x, y)
#define sqrt(x) PSP_NARROW1(sqrt, (sqrt), x)
#define floor(x) PSP_NARROW1(floor, (floor), x)
#define ceil(x) PSP_NARROW1(ceil, (ceil), x)
#define fmod(x, y) PSP_NARROW2(fmod, (fmod), x, y)
#endif
"""),
],
"source/render/render_objects.c#lod": [
("\treturn render_frustum_sphere_diameter_in_pixels(&render.frustum, &center, radius);\n}",
 "#ifdef HALO_PSP\n\t/* the game measures pixels on a 480-line screen; the PSP's has 272, so\n\tmodels pick the level of detail Bungie's thresholds give at the size they\n\treally appear (build 19; fulllod in keep.txt turns it off; psp_fixups.py) */\n\tif (!PSP_KEEP_ONCE(\"fulllod\"))\n\t\treturn render_frustum_sphere_diameter_in_pixels(&render.frustum, &center, radius) * (272.0f / 480.0f);\n#endif\n\treturn render_frustum_sphere_diameter_in_pixels(&render.frustum, &center, radius);\n}"),
],
}

# build 23: the motion sensor (psp_ge.c draws it into a small target of its
# own, then onto the HUD)
FIXES_23 = {
"source/rasterizer/rasterizer.c": [
("""void rasterizer_hud_motion_sensor_blip_begin(
	void)
{
	_rasterizer_hud_motion_sensor_blip_begin();""",
 """#ifdef HALO_PSP
int psp_ge_active(void);
void psp_ge_blips_begin(void);
void psp_ge_blip(float x, float y, float radius, float red, float green, float blue, unsigned int key);
void psp_ge_blips_end(float center_x, float center_y, float scale, unsigned int sweep_key, unsigned int mask_key);
struct bitmap_data *bitmap_group_try_and_get_bitmap(long bitmap_group_index, short bitmap_index);
long interface_get_tag_index(short index);
#define PSP_INTERFACE_BITMAP(index) ((unsigned int)bitmap_group_try_and_get_bitmap(interface_get_tag_index(index), 0))
#endif

void rasterizer_hud_motion_sensor_blip_begin(
	void)
{
#ifdef HALO_PSP
	if (psp_ge_active())
	{
		psp_ge_blips_begin();
		return;
	}
#endif
	_rasterizer_hud_motion_sensor_blip_begin();"""),
("""	_rasterizer_hud_motion_sensor_blip_draw(position, intensity, size, color, large_blip);""",
 """#ifdef HALO_PSP
	if (psp_ge_active())
	{
		/* the Xbox's own sums (rasterizer_xbox_motion_sensor.c): the sensor's
		square is -1 to 1 */
		psp_ge_blip(position->x * -0.03125f, position->y * -0.03125f, size * 0.0625f, color->red * intensity,
			color->green * intensity, color->blue * intensity, PSP_INTERFACE_BITMAP(large_blip ? 13 : 12));
		return;
	}
#endif
	_rasterizer_hud_motion_sensor_blip_draw(position, intensity, size, color, large_blip);"""),
("""	_rasterizer_hud_motion_sensor_blip_end(center, scale);""",
 """#ifdef HALO_PSP
	if (psp_ge_active())
	{
		psp_ge_blips_end(center->x, center->y, scale, PSP_INTERFACE_BITMAP(7), PSP_INTERFACE_BITMAP(8));
		return;
	}
#endif
	_rasterizer_hud_motion_sensor_blip_end(center, scale);"""),
],
}

FIXES_23["source/rasterizer/xbox/rasterizer_xbox_text.c"] = [
("""void rasterizer_text_end(
	void)
{
	return;
}""",
 """#ifdef HALO_PSP
/* the HUD's and menus' text goes to the GE (psp_ge.c; build 23; psp_fixups.py) */
int psp_ge_active(void);
int psp_ge_text_begin(void const *bitmap, short blend, float offset_x, float offset_y);
void psp_ge_text_character(void const *vertices);
void psp_ge_text_end(void);
static int psp_text_open = 0;
#endif

void rasterizer_text_end(
	void)
{
#ifdef HALO_PSP
	if (psp_text_open)
	{
		psp_ge_text_end();
		psp_text_open = 0;
	}
#endif
	return;
}"""),
("""	short map_index;
""",
 """	short map_index;

#ifdef HALO_PSP
	if (psp_ge_active())
	{
		extern void psp_ge_ui_space(int width, int height);

		psp_ge_ui_space(global_window_parameters.camera.viewport_bounds.x1 -
			global_window_parameters.camera.viewport_bounds.x0,
			global_window_parameters.camera.viewport_bounds.y1 -
			global_window_parameters.camera.viewport_bounds.y0);
		psp_text_open = rasterizer_debug_options.draw_dynamic_screen_geometry &&
			global_window_parameters.rasterizer_target == 0 && parameters && parameters->map[0] &&
			psp_ge_text_begin(parameters->map[0], parameters->framebuffer_blend_function,
				parameters->scale ? parameters->scale->i : 0.0f, parameters->scale ? parameters->scale->j : 0.0f);
		return;
	}
#endif
"""),
("""	boolean success;
	short vertex_index;
""",
 """	boolean success;
	short vertex_index;

#ifdef HALO_PSP
	if (psp_ge_active())
	{
		if (psp_text_open)
			psp_ge_text_character(vertices);
		return;
	}
#endif
"""),
]

# build 23: the AI's line-of-sight debug drawing keeps 16,384 points and
# 8,192 rays (300 KB of the 534 KB ai_debug) and only fills them when that
# debug view is on; on the PSP it keeps 64 of each (the code checks the
# limits). The Xbox layout's offset checks after them don't apply then.
FIXES_23["source/ai/ai_debug.h"] = [
("""	MAXIMUM_AI_DEBUG_LINEOFSIGHT_POINTS = 16384,
	MAXIMUM_AI_DEBUG_LINEOFSIGHT_PAIRS = 8192,""",
 """#ifdef HALO_PSP
	/* psp_fixups.py (build 23) */
	MAXIMUM_AI_DEBUG_LINEOFSIGHT_POINTS = 64,
	MAXIMUM_AI_DEBUG_LINEOFSIGHT_PAIRS = 64,
#else
	MAXIMUM_AI_DEBUG_LINEOFSIGHT_POINTS = 16384,
	MAXIMUM_AI_DEBUG_LINEOFSIGHT_PAIRS = 8192,
#endif"""),
("""typedef char ai_debug_state_ballistic_lineoffire_valid_offset_assert[""",
 """#ifndef HALO_PSP
typedef char ai_debug_state_ballistic_lineoffire_valid_offset_assert["""),
("""typedef char ai_debug_state_field_7D380_offset_assert[
	offsetof(struct ai_debug_state, evaluation_context_valid) == 0x7D380 ? 1 : -1];""",
 """typedef char ai_debug_state_field_7D380_offset_assert[
	offsetof(struct ai_debug_state, evaluation_context_valid) == 0x7D380 ? 1 : -1];
#endif"""),
]

# build 102: the rest of the AI debugger's static state, 231 of its 237 KB:
# the path debugger's own path search (path_state 82 KB, path_storage 117 KB;
# used only after the console's ai_debug_path command, allocated then on the
# PSP, ai_debug_shrink below) and the firing-position records of the selected
# squad (512 x 64 bytes; only written with a squad selected from the console,
# and every index goes through PSP_DEBUG_INDEX like the actor records). The
# game's heap gets it back
FIXES_23["source/ai/ai_debug.h"] += [
("""	NUMBER_OF_AI_DEBUG_ACTOR_RECORDS = 512,""",
 """#ifdef HALO_PSP
	NUMBER_OF_AI_DEBUG_ACTOR_RECORDS = 1, /* psp_fixups.py (build 102) */
#else
	NUMBER_OF_AI_DEBUG_ACTOR_RECORDS = 512,
#endif"""),
("""	struct path_state path_state;
	boolean field_608A8;
	char __unknown608A9[91];
	struct path_debug_storage path_storage;
""",
 """#ifndef HALO_PSP
	struct path_state path_state;
#endif
	boolean field_608A8;
	char __unknown608A9[91];
#ifndef HALO_PSP
	struct path_debug_storage path_storage;
#endif
"""),
]

# build 23: memory only a network game uses, allocated when one first does
# (the host's per-tick lists of object states and inventories: 245 KB), and
# the null renderer's index-range cache cut to what it still draws (the GE
# draws the world, HUD and text: 60 KB)
FIXES_23["port/linux/game/network_objects.c"] = [
("""	static struct distributed_object_state states[MAXIMUM_TRACKED_OBJECTS + RESTING_STATES_PER_TICK];
	static byte kinds[MAXIMUM_TRACKED_OBJECTS + RESTING_STATES_PER_TICK];
	struct distributed_object_state_message message;""",
 """#ifdef HALO_PSP
	/* allocated by the first network game (psp_fixups.py, build 23) */
	static struct distributed_object_state *states = NULL;
	static byte *kinds = NULL;
#else
	static struct distributed_object_state states[MAXIMUM_TRACKED_OBJECTS + RESTING_STATES_PER_TICK];
	static byte kinds[MAXIMUM_TRACKED_OBJECTS + RESTING_STATES_PER_TICK];
#endif
	struct distributed_object_state_message message;"""),
("""	short resting = 0;
	long told_count = objects_host_told_count;""",
 """	short resting = 0;
	long told_count = objects_host_told_count;
#ifdef HALO_PSP
	if (!states)
	{
		states = malloc(sizeof(*states) * (MAXIMUM_TRACKED_OBJECTS + RESTING_STATES_PER_TICK));
		kinds = malloc(MAXIMUM_TRACKED_OBJECTS + RESTING_STATES_PER_TICK);
		if (!states || !kinds)
		{
			free(states);
			free(kinds);
			states = NULL;
			kinds = NULL;
			return;
		}
	}
#endif"""),
("""	static struct distributed_inventory inventories[MAXIMUM_TRACKED_OBJECTS];
	static byte kinds[MAXIMUM_TRACKED_OBJECTS];
	static real_point3d origins[MAXIMUM_TRACKED_OBJECTS];
	struct distributed_inventory_message message;
	short limit = MIN(MAXIMUM_ENTRIES_PER_MESSAGE, DATAGRAM_ENTRIES(struct distributed_inventory));
	long inventory_count = 0;
	long round = game_time_get() / INVENTORY_INTERVAL_TICKS;
	struct object_iterator iterator;
	short machine_number;
""",
 """#ifdef HALO_PSP
	/* allocated by the first network game (psp_fixups.py, build 23) */
	static struct distributed_inventory *inventories = NULL;
	static byte *kinds = NULL;
	static real_point3d *origins = NULL;
#else
	static struct distributed_inventory inventories[MAXIMUM_TRACKED_OBJECTS];
	static byte kinds[MAXIMUM_TRACKED_OBJECTS];
	static real_point3d origins[MAXIMUM_TRACKED_OBJECTS];
#endif
	struct distributed_inventory_message message;
	short limit = MIN(MAXIMUM_ENTRIES_PER_MESSAGE, DATAGRAM_ENTRIES(struct distributed_inventory));
	long inventory_count = 0;
	long round = game_time_get() / INVENTORY_INTERVAL_TICKS;
	struct object_iterator iterator;
	short machine_number;

#ifdef HALO_PSP
	if (!inventories)
	{
		inventories = malloc(sizeof(*inventories) * MAXIMUM_TRACKED_OBJECTS);
		kinds = malloc(MAXIMUM_TRACKED_OBJECTS);
		origins = malloc(sizeof(*origins) * MAXIMUM_TRACKED_OBJECTS);
		if (!inventories || !kinds || !origins)
		{
			free(inventories);
			free(kinds);
			free(origins);
			inventories = NULL;
			kinds = NULL;
			origins = NULL;
			return;
		}
	}
#endif
"""),
]
FIXES_23["port/linux/src/d3d8_gl.c#index_ranges"] = [
("""#define INDEX_RANGE_SLOTS 4096
""",
 """#ifdef HALO_PSP
#define INDEX_RANGE_SLOTS 256 /* psp_fixups.py, build 23 */
#else
#define INDEX_RANGE_SLOTS 4096
#endif
"""),
]

# build 23: the GE fogs with the game's atmospheric fog (colour, distances,
# density) instead of a fixed one
FIXES_23["source/rasterizer/rasterizer.c"].append(
("""	_rasterizer_window_set_fog(fog);
	return;""",
 """	_rasterizer_window_set_fog(fog);
#ifdef HALO_PSP
	{
		/* as the Xbox settled it: off reads as zero density (psp_ge.c; psp_fixups.py) */
		extern void psp_ge_fog(const float *color, float minimum_distance, float maximum_distance, float density);

		psp_ge_fog(&global_window_parameters.fog.atmospheric_color.red,
			global_window_parameters.fog.atmospheric_minimum_distance,
			global_window_parameters.fog.atmospheric_maximum_distance,
			global_window_parameters.fog.atmospheric_maximum_density);
	}
#endif
	return;"""))

# build 23: the level's own see-through surfaces (glass, water, the BSP's
# effects) join the GE's see-through list, as the model parts do
FIXES_23["source/rasterizer/rasterizer.c"].append(
("""	_rasterizer_environment_transparent_geometry_submit(
		shader,
		shader_permutation_index,
		lightmap,
		dynamic_triangle_buffer_index,
		first_triangle_index,
		triangle_count,
		vertex_buffers,
		centroid,
		plane,
		offset,
		lighting,
		geometry_flags);
	return;""",
 """#ifdef HALO_PSP
	{
		extern int psp_ge_active(void);
		extern unsigned int psp_tex_bitmap_key(long, int);
		extern void psp_environment_transparent(long, long, long, struct vertex_buffer const *, unsigned int, float,
			float, float, float, short, float const *);

		if (psp_ge_active())
		{
			float u_scale, v_scale, u_offset, v_offset;
			short blend;
			unsigned int key = psp_transparent_map(shader, shader_permutation_index, FALSE, &u_scale, &v_scale,
				&u_offset, &v_offset, &blend);

			if (key)
				psp_environment_transparent(dynamic_triangle_buffer_index, first_triangle_index, triangle_count,
					vertex_buffers, key, u_scale, v_scale, u_offset, v_offset, blend, centroid ? &centroid->x : NULL);
			return;
		}
	}
#endif
	_rasterizer_environment_transparent_geometry_submit(
		shader,
		shader_permutation_index,
		lightmap,
		dynamic_triangle_buffer_index,
		first_triangle_index,
		triangle_count,
		vertex_buffers,
		centroid,
		plane,
		offset,
		lighting,
		geometry_flags);
	return;"""))

# build 23: decals (bullet holes, blood, scorch marks, the level's painted
# markings) to the GE, from the game's own decal vertices
FIXES_23["source/rasterizer/xbox/rasterizer_xbox_decals.c"] = [
("""		unsigned long intensity;

		if (local_framebuffer_blend_function != framebuffer_blend_function)
		{""",
 """		unsigned long intensity;

#ifdef HALO_PSP
		if (psp_ge_active())
		{
			/* psp_ge.c (build 23; psp_fixups.py). Build 106: first thing in
			the loop, before the Xbox's blend, shader and texture state for
			the decal (set for nothing here: the GE call sets its own, and
			rasterizer_set_texture looked up the bitmap each time it changed,
			nearly every bullet hole), and the GE's texture key worked out only
			when the decal's bitmap differs from the last one's. b30's fight
			draws 800-900 decals a frame */
			extern void psp_ge_decal(void const *vertices, int quad_count, unsigned int key, short blend,
				unsigned long color, int intensity, int alpha_tested, int permanent);
			extern unsigned int psp_tex_bitmap_key(long, int);
			byte *vertex_data = NULL;

			vertex_data_offset = (unsigned long)lruv_block_get_address(local_vertex_cache, decal_index);
			color = decal->color;
			intensity = (decal->intensity * (color >> 24) + 127) >> 8;
			if (shader->map.index != psp_key_group || decal->bitmap_index != psp_key_bitmap)
			{
				psp_key_group = shader->map.index;
				psp_key_bitmap = decal->bitmap_index;
				psp_key = psp_tex_bitmap_key(shader->map.index, decal->bitmap_index);
			}
			IDirect3DVertexBuffer8_Lock(local_d3d_vertex_buffer, 0, 0, &vertex_data, D3DLOCK_READONLY);
			if (vertex_data)
				psp_ge_decal(vertex_data + vertex_data_offset, decal->quad_count,
					psp_key, framebuffer_blend_function, color,
					(int)intensity, local_layer == _decal_layer_alpha_tested,
					/* build 90: the level's permanent decals (the scenario's painted symbols) */
					TEST_FLAG(decal->flags, _decal_permanent_bit) ? 1 : 0);
			IDirect3DVertexBuffer8_Unlock(local_d3d_vertex_buffer);
			decal_index = decal->next_decal_index;
			continue;
		}
#endif

		if (local_framebuffer_blend_function != framebuffer_blend_function)
		{"""),
("""void _rasterizer_decals_draw(
	short cluster_index)
{
	long decal_index;
""",
 """#ifdef HALO_PSP
int psp_ge_active(void);
#endif

void _rasterizer_decals_draw(
	short cluster_index)
{
	long decal_index;
#ifdef HALO_PSP
	/* the last decal's bitmap and its GE texture key (build 106), for this
	call's decals only (a new map may reuse the numbers) */
	long psp_key_group = NONE;
	short psp_key_bitmap = NONE;
	unsigned int psp_key = 0;
#endif
"""),
]

# build 23: the screen flash (taking damage, pickups, teleporting) on the GE
FIXES_23["source/rasterizer/rasterizer.c"].append(
("""void rasterizer_screen_flash(
	void)
{
	_rasterizer_screen_flash();""",
 """void rasterizer_screen_flash(
	void)
{
#ifdef HALO_PSP
	{
		extern int psp_ge_active(void);
		extern void psp_ge_screen_flash(short type, float intensity, float const *argb);

		if (psp_ge_active())
		{
			if (rasterizer_debug_options.screen_flash_enabled)
				psp_ge_screen_flash(global_window_parameters.screen_flash.type,
					global_window_parameters.screen_flash.intensity,
					global_window_parameters.screen_flash.color.n);
			return;
		}
	}
#endif
	_rasterizer_screen_flash();"""))

# build 25: blob shadows on the GE. The game still picks each shadow's frame
# and the level triangles that receive it; the silhouette it would draw from
# the light (a model render per object) isn't needed for a blob
FIXES_23["source/render/render_objects.c#shadows"] = [
("""					render_model(
						definition->object.model.index,
						level_of_detail_pixels * 0.3f,""",
 """#ifdef HALO_PSP
					if (!psp_ge_active())
#endif
					render_model(
						definition->object.model.index,
						level_of_detail_pixels * 0.3f,"""),
]
FIXES_23["source/rasterizer/rasterizer.c"].append(
("""	return _rasterizer_environment_shadow_begin(
		object_index,
		shadow_matrix,
		shadow_color,
		object_bounding_radius,
		shadow_volume_bounding_radius);""",
 """#ifdef HALO_PSP
	{
		extern int psp_ge_active(void);
		extern void psp_ge_shadow_begin(const float *matrix, const float *color, float radius);

		if (psp_ge_active())
		{
			psp_ge_shadow_begin((const float *)shadow_matrix, &shadow_color->red, object_bounding_radius);
			if (shadow_volume_bounding_radius)
				*shadow_volume_bounding_radius = object_bounding_radius;
			return TRUE;
		}
	}
#endif
	return _rasterizer_environment_shadow_begin(
		object_index,
		shadow_matrix,
		shadow_color,
		object_bounding_radius,
		shadow_volume_bounding_radius);"""))
FIXES_23["source/rasterizer/rasterizer.c"].append(
("""	_rasterizer_environment_shadow_end();
	return;""",
 """#ifdef HALO_PSP
	{
		extern int psp_ge_active(void);
		extern void psp_ge_shadow_end(void);

		if (psp_ge_active())
		{
			psp_ge_shadow_end();
			return;
		}
	}
#endif
	_rasterizer_environment_shadow_end();
	return;"""))
FIXES_23["source/rasterizer/rasterizer.c"].append(
("""	_rasterizer_environment_shadow_draw(shader, bitmap_index, dynamic_triangle_buffer_index, first_triangle_index, triangle_count, vertex_buffer);""",
 """#ifdef HALO_PSP
	{
		extern int psp_ge_active(void);
		extern void psp_environment_shadow(long, long, long, struct vertex_buffer const *);

		if (psp_ge_active())
		{
			psp_environment_shadow(dynamic_triangle_buffer_index, first_triangle_index, triangle_count, vertex_buffer);
			return;
		}
	}
#endif
	_rasterizer_environment_shadow_draw(shader, bitmap_index, dynamic_triangle_buffer_index, first_triangle_index, triangle_count, vertex_buffer);"""))

# build 25: shadows at the same size on screen as on the Xbox. The PSP's
# level-of-detail pixels are measured on its 272 lines (build 19), so the
# 30-pixel threshold (of 480) becomes 17, and the fade-in over 15 pixels 8.5
FIXES_23["source/render/render_objects.c#shadows"].append(
("""#define OBJECT_SHADOW_MINIMUM_PIXELS 30.f""",
 """#ifdef HALO_PSP
#define OBJECT_SHADOW_MINIMUM_PIXELS (30.f * 272.f / 480.f) /* psp_fixups.py */
#else
#define OBJECT_SHADOW_MINIMUM_PIXELS 30.f
#endif"""))
FIXES_23["source/render/render_objects.c#shadows"].append(
("""					(level_of_detail_pixels - OBJECT_SHADOW_MINIMUM_PIXELS) * 0.06666667f;""",
 """					(level_of_detail_pixels - OBJECT_SHADOW_MINIMUM_PIXELS) * 0.06666667f
#ifdef HALO_PSP
					* (480.f / 272.f)
#endif
					;"""))

# build 26: checkpoints write only the game state's pages that hold
# anything. A checkpoint is the whole 3.3 MB game state written to the
# Memory Stick (2 s or more there), but on Blood Gulch 98% of it is zeros
# (empty pools): 75 of its 837 4 KB pages hold data. The file keeps a page
# map, then those pages; reverting reads them back over a cleared state
FIXES_23["source/saved games/game_state_xbox.c"] = [
("""boolean game_state_write_to_file(
	void)
{
	unsigned long bytes_written;
	boolean result = FALSE;
""",
 """#ifdef HALO_PSP
#define PSP_STATE_PAGE 4096
#define PSP_STATE_HEADER 2048
#define PSP_STATE_SIGNATURE 0x53505350 /* 'PSPS' */
void platform_psp_log(const char *format, ...);
unsigned long psp_microseconds(void);

static boolean psp_state_page_empty(unsigned long const *words)
{
	long index;

	for (index = 0; index < PSP_STATE_PAGE / 4; index += 4)
		if (words[index] | words[index + 1] | words[index + 2] | words[index + 3])
			return FALSE;
	return TRUE;
}

/* build 116 (R11): the log holds its writes around a checkpoint, so the
sound reader has the Memory Stick first after it (psp_platform.c) */
void psp_log_hold(unsigned int milliseconds);
#define PSP_STATE_LOG_HOLD_MS 2500

/* build 116 (R10): the checkpoint's pages stored compactly. b115 test 1
[M]: each checkpoint froze the game 0.2-1.1 s writing 1.3-1.9 MB of game
state, and most pages change each time, so writing only the changed ones
saves little. Most of a page is zero words, though: each kept page is now
a bitmap of its non-zero words (128 bytes) and those words, or the page
as it is when that saves nothing. The header (4 KB) carries each page's
count of non-zero words, so the reader decodes in place from the buffer's
tail: no entry is longer than its page. Keep word "statepages": the
format before (psp_state_read reads either) */
#define PSP_STATE_SIGNATURE_WORDS 0x5A535350 /* 'PSSZ' */
#define PSP_STATE_WORDS_HEADER 4096
#define PSP_STATE_PAGE_WORDS (PSP_STATE_PAGE / 4)
#define PSP_STATE_RAW_ABOVE ((PSP_STATE_PAGE - PSP_STATE_PAGE_WORDS / 8) / 4) /* 992 words: past this, raw */
#define PSP_STATE_STAGE 32768
static unsigned long psp_state_words_header[PSP_STATE_WORDS_HEADER / 4];

static unsigned long psp_state_entry_bytes(unsigned short count)
{
	return count >= PSP_STATE_RAW_ABOVE ? PSP_STATE_PAGE : PSP_STATE_PAGE_WORDS / 8 + 4 * (unsigned long)count;
}

/* the header's parts: the page bitmap after four words, then the counts */
#define PSP_STATE_MAP_BYTES(pages) ((((pages) + 7) / 8 + 3) & ~3UL)
#define PSP_STATE_COUNTS(header, pages) ((unsigned short *)((byte *)(header) + 16 + PSP_STATE_MAP_BYTES(pages)))

static boolean psp_state_write_words(HANDLE handle, byte const *buffer, unsigned long size)
{
	unsigned long *header = psp_state_words_header;
	unsigned long pages = size / PSP_STATE_PAGE, page, written, kept = 0, stream = 0, used = 0;
	unsigned long start = psp_microseconds();
	byte *map = (byte *)&header[4], *stage, *stage_block;
	unsigned short *counts;
	boolean ok = TRUE;

	if (size % PSP_STATE_PAGE || 16 + PSP_STATE_MAP_BYTES(pages) + 2 * pages > PSP_STATE_WORDS_HEADER)
		return FALSE;
	/* build 117: the stage 64-byte aligned (an independent review: b115's
	fast path wrote straight from the game state's aligned buffer) */
	stage_block = (byte *)malloc(PSP_STATE_STAGE + 63);
	if (!stage_block)
		return FALSE;
	stage = (byte *)(((unsigned long)stage_block + 63) & ~63UL);
	csmemset(header, 0, PSP_STATE_WORDS_HEADER);
	counts = PSP_STATE_COUNTS(header, pages);
	for (page = 0; page < pages; page++)
	{
		unsigned long const *words = (unsigned long const *)(buffer + page * PSP_STATE_PAGE);
		unsigned short count = 0;
		long w;

		for (w = 0; w < PSP_STATE_PAGE_WORDS; w++)
			count += words[w] != 0;
		if (count)
		{
			map[page >> 3] |= 1 << (page & 7);
			counts[page] = count;
			stream += psp_state_entry_bytes(count);
			kept++;
		}
	}
	header[0] = PSP_STATE_SIGNATURE_WORDS;
	header[1] = size;
	header[2] = pages;
	header[3] = stream;
	if (SetFilePointer(handle, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
		!WriteFile(handle, header, PSP_STATE_WORDS_HEADER, &written, NULL) || written != PSP_STATE_WORDS_HEADER)
		ok = FALSE;
	for (page = 0; ok && page < pages; page++)
	{
		unsigned long const *words = (unsigned long const *)(buffer + page * PSP_STATE_PAGE);
		unsigned long bytes = psp_state_entry_bytes(counts[page]);

		if (!counts[page])
			continue;
		if (used + bytes > PSP_STATE_STAGE)
		{
			/* build 117: whole 4 KB pieces only, the rest moved to the
			stage's start, so every write but the last starts and ends on a
			4 KB bound of the file. b116 test 1 [M]: written in pieces of any
			length, b30's first checkpoint (330 pages, 768 KB) took 3,102
			ms where b115 wrote the same 330 pages whole, 1,320 KB, in 219 ms
			(that a write ending mid-sector makes the Memory Stick rewrite
			it for the next one is [U]) */
			unsigned long whole = used & ~(unsigned long)(PSP_STATE_PAGE - 1);

			ok = WriteFile(handle, stage, whole, &written, NULL) && written == whole;
			if (!ok)
				break;
			memmove(stage, stage + whole, used - whole);
			used -= whole;
		}
		if (counts[page] >= PSP_STATE_RAW_ABOVE)
			csmemcpy(stage + used, words, PSP_STATE_PAGE);
		else
		{
			byte *bits = stage + used;
			unsigned long *out = (unsigned long *)(bits + PSP_STATE_PAGE_WORDS / 8);
			long w;

			csmemset(bits, 0, PSP_STATE_PAGE_WORDS / 8);
			for (w = 0; w < PSP_STATE_PAGE_WORDS; w++)
				if (words[w])
				{
					bits[w >> 3] |= 1 << (w & 7);
					*out++ = words[w];
				}
		}
		used += bytes;
	}
	if (ok && used)
		ok = WriteFile(handle, stage, used, &written, NULL) && written == used;
	free(stage_block);
	if (ok)
		platform_psp_log("checkpoint: %lu of %lu pages of game state, %lu KB as non-zero words (%lu KB as pages), written in %lu ms",
			kept, pages, (PSP_STATE_WORDS_HEADER + stream) / 1024, kept * PSP_STATE_PAGE / 1024,
			(psp_microseconds() - start) / 1000);
	return ok;
}

/* header: signature, size, page count, then one bit per page */
static boolean psp_state_write(HANDLE handle, byte const *buffer, unsigned long size)
{
	static unsigned long header[PSP_STATE_HEADER / 4];
	unsigned long pages = size / PSP_STATE_PAGE, page, written, kept = 0, start = psp_microseconds();
	byte *map = (byte *)&header[3];

	if (size % PSP_STATE_PAGE || pages > (PSP_STATE_HEADER - 12) * 8)
		return FALSE;
	psp_log_hold(PSP_STATE_LOG_HOLD_MS);
	{
		/* build 116 (R10): the compact format, unless "statepages" (or it
		can't: no room for its staging buffer); the pages as before then */
		static int pages_only = -1;

		if (pages_only < 0)
			pages_only = psp_keep("statepages");
		if (!pages_only && psp_state_write_words(handle, buffer, size))
		{
			psp_log_hold(PSP_STATE_LOG_HOLD_MS);
			return TRUE;
		}
	}
	csmemset(header, 0, sizeof(header));
	header[0] = PSP_STATE_SIGNATURE;
	header[1] = size;
	header[2] = pages;
	/* build 105 (diagnostic): how many kept pages changed since the last
	checkpoint (a hash a page), and the scan's time apart from the writes':
	b102 run 6's checkpoint froze the game 1.76 s for 388 pages, and writing
	only the changed pages is worth building only if few change */
	static unsigned long psp_page_hash[2048];
	static int psp_page_hashes_valid = 0;
	unsigned long changed = 0, scan_us;

	for (page = 0; page < pages; page++)
		if (!psp_state_page_empty((unsigned long const *)(buffer + page * PSP_STATE_PAGE)))
		{
			unsigned long const *words = (unsigned long const *)(buffer + page * PSP_STATE_PAGE);
			unsigned long hash = 0x811C9DC5UL;
			long w;

			for (w = 0; w < PSP_STATE_PAGE / 4; w++)
				hash = ((hash << 5) | (hash >> 27)) ^ words[w];
			if (page < 2048)
			{
				if (!psp_page_hashes_valid || psp_page_hash[page] != hash)
					changed++;
				psp_page_hash[page] = hash;
			}
			map[page >> 3] |= 1 << (page & 7);
			kept++;
		}
		else if (page < 2048)
		{
			if (psp_page_hashes_valid && psp_page_hash[page] != 0)
				changed++;
			psp_page_hash[page] = 0;
		}
	scan_us = psp_microseconds() - start;
	if (SetFilePointer(handle, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
		!WriteFile(handle, header, PSP_STATE_HEADER, &written, NULL) || written != PSP_STATE_HEADER)
		return FALSE;
	/* each run of kept pages in one write */
	for (page = 0; page < pages; )
	{
		unsigned long run = 0;

		if (!(map[page >> 3] & (1 << (page & 7))))
		{
			page++;
			continue;
		}
		while (page + run < pages && (map[(page + run) >> 3] & (1 << ((page + run) & 7))))
			run++;
		if (!WriteFile(handle, buffer + page * PSP_STATE_PAGE, run * PSP_STATE_PAGE, &written, NULL) ||
			written != run * PSP_STATE_PAGE)
			return FALSE;
		page += run;
	}
	platform_psp_log("checkpoint: %lu of %lu pages of game state written in %lu ms (scan %lu ms; %lu pages changed since the last checkpoint%s)",
		kept, pages, (psp_microseconds() - start) / 1000, scan_us / 1000, changed, psp_page_hashes_valid ? "" : ": the first");
	psp_page_hashes_valid = 1;
	psp_log_hold(PSP_STATE_LOG_HOLD_MS); /* build 116: from the end of the write */
	return TRUE;
}

/* build 116 (R10): the compact format's pages (psp_state_write_words): the
entries read to the buffer's tail, then each page decoded in place, front
to back. No entry is longer than its page, so the entries still to decode
always start at or after the page being written; a kept page's entry is
copied aside first, since it may overlap its own page */
static boolean psp_state_read_words(HANDLE handle, byte *buffer, unsigned long size, unsigned long const *first)
{
	unsigned long *header = psp_state_words_header;
	unsigned long pages = size / PSP_STATE_PAGE, page, read, stream;
	byte const *map = (byte const *)&header[4], *in;
	unsigned short const *counts;
	byte *entry;

	csmemcpy(header, first, PSP_STATE_HEADER);
	if (16 + PSP_STATE_MAP_BYTES(pages) + 2 * pages > PSP_STATE_WORDS_HEADER ||
		!ReadFile(handle, (byte *)header + PSP_STATE_HEADER, PSP_STATE_WORDS_HEADER - PSP_STATE_HEADER, &read, NULL) ||
		read != PSP_STATE_WORDS_HEADER - PSP_STATE_HEADER)
		return FALSE;
	stream = header[3];
	counts = PSP_STATE_COUNTS(header, pages);
	if (stream > size || (stream && (!ReadFile(handle, buffer + size - stream, stream, &read, NULL) || read != stream)))
		return FALSE;
	entry = (byte *)malloc(PSP_STATE_PAGE);
	if (!entry)
		return FALSE;
	in = buffer + size - stream;
	for (page = 0; page < pages; page++)
	{
		unsigned long *out = (unsigned long *)(buffer + page * PSP_STATE_PAGE);
		unsigned long bytes;

		if (!(map[page >> 3] & (1 << (page & 7))))
		{
			csmemset(out, 0, PSP_STATE_PAGE);
			continue;
		}
		bytes = psp_state_entry_bytes(counts[page]);
		if (in + bytes > buffer + size || in < (byte const *)out)
		{
			free(entry);
			return FALSE;
		}
		if (counts[page] >= PSP_STATE_RAW_ABOVE)
			memmove(out, in, PSP_STATE_PAGE);
		else
		{
			byte const *bits = entry;
			unsigned long const *words = (unsigned long const *)(entry + PSP_STATE_PAGE_WORDS / 8);
			unsigned long set = 0;
			long w;

			csmemcpy(entry, in, bytes);
			/* the bitmap must name exactly the entry's count of words: a
			corrupt entry with more bits set read past its copy (review) */
			for (w = 0; w < PSP_STATE_PAGE_WORDS / 32; w++)
				set += (unsigned long)__builtin_popcountl(((unsigned long const *)entry)[w]);
			if (set != counts[page])
			{
				free(entry);
				return FALSE;
			}
			for (w = 0; w < PSP_STATE_PAGE_WORDS; w++)
				out[w] = (bits[w >> 3] & (1 << (w & 7))) ? *words++ : 0;
		}
		in += bytes;
	}
	free(entry);
	return in == buffer + size;
}

static boolean psp_state_read(HANDLE handle, byte *buffer, unsigned long size)
{
	static unsigned long header[PSP_STATE_HEADER / 4];
	unsigned long pages = size / PSP_STATE_PAGE, page, read;
	byte const *map = (byte const *)&header[3];

	if (SetFilePointer(handle, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
		!ReadFile(handle, header, PSP_STATE_HEADER, &read, NULL) || read != PSP_STATE_HEADER ||
		header[1] != size || header[2] != pages)
		return FALSE;
	if (header[0] == PSP_STATE_SIGNATURE_WORDS)
		return psp_state_read_words(handle, buffer, size, header);
	if (header[0] != PSP_STATE_SIGNATURE)
		return FALSE;
	csmemset(buffer, 0, size);
	for (page = 0; page < pages; )
	{
		unsigned long run = 0;

		if (!(map[page >> 3] & (1 << (page & 7))))
		{
			page++;
			continue;
		}
		while (page + run < pages && (map[(page + run) >> 3] & (1 << ((page + run) & 7))))
			run++;
		if (!ReadFile(handle, buffer + page * PSP_STATE_PAGE, run * PSP_STATE_PAGE, &read, NULL) ||
			read != run * PSP_STATE_PAGE)
			return FALSE;
		page += run;
	}
	return TRUE;
}
#endif

boolean game_state_write_to_file(
	void)
{
	unsigned long bytes_written;
	boolean result = FALSE;
"""),
("""	if (SetFilePointer(xbox_game_state_globals.handle, 0, NULL, FILE_BEGIN) !=
			INVALID_SET_FILE_POINTER &&
		WriteFile(xbox_game_state_globals.handle, xbox_game_state_globals.buffer,
			xbox_game_state_globals.buffer_size, &bytes_written, NULL) &&
		bytes_written == xbox_game_state_globals.buffer_size)""",
 """#ifdef HALO_PSP
	(void)bytes_written;
	if (psp_state_write(xbox_game_state_globals.handle, (byte const *)xbox_game_state_globals.buffer,
		xbox_game_state_globals.buffer_size))
#else
	if (SetFilePointer(xbox_game_state_globals.handle, 0, NULL, FILE_BEGIN) !=
			INVALID_SET_FILE_POINTER &&
		WriteFile(xbox_game_state_globals.handle, xbox_game_state_globals.buffer,
			xbox_game_state_globals.buffer_size, &bytes_written, NULL) &&
		bytes_written == xbox_game_state_globals.buffer_size)
#endif"""),
Either(universal=("""	if (SetFilePointer(xbox_game_state_globals.handle, 0, NULL, FILE_BEGIN) !=
			INVALID_SET_FILE_POINTER &&
		ReadFile(xbox_game_state_globals.handle, xbox_game_state_globals.buffer,
			xbox_game_state_globals.buffer_size, &bytes_read, NULL) &&
		bytes_read == xbox_game_state_globals.buffer_size)""",
 """#ifdef HALO_PSP
	(void)bytes_read;
	if (psp_state_read(xbox_game_state_globals.handle, (byte *)xbox_game_state_globals.buffer,
		xbox_game_state_globals.buffer_size))
#else
	if (SetFilePointer(xbox_game_state_globals.handle, 0, NULL, FILE_BEGIN) !=
			INVALID_SET_FILE_POINTER &&
		ReadFile(xbox_game_state_globals.handle, xbox_game_state_globals.buffer,
			xbox_game_state_globals.buffer_size, &bytes_read, NULL) &&
		bytes_read == xbox_game_state_globals.buffer_size)
#endif"""),
 # ChupathingyCE
 chupa=("""	/* port: read beside the game state, and taken once checked */
	image = malloc(xbox_game_state_globals.buffer_size);
	if (image &&
		SetFilePointer(xbox_game_state_globals.handle, 0, NULL, FILE_BEGIN) !=
			INVALID_SET_FILE_POINTER &&
		ReadFile(xbox_game_state_globals.handle, image,
			xbox_game_state_globals.buffer_size, &bytes_read, NULL) &&
		bytes_read == xbox_game_state_globals.buffer_size &&
		game_state_image_accept(image, xbox_game_state_globals.buffer_size))""",
  """#ifdef HALO_PSP
	/* psp_state_write's pages, read in place (GAME_STATE_IMAGE_NEW) and
	checked there (psp_fixups.py) */
	(void)bytes_read;
	image = GAME_STATE_IMAGE_NEW(xbox_game_state_globals.buffer, xbox_game_state_globals.buffer_size);
	if (psp_state_read(xbox_game_state_globals.handle, (byte *)image, xbox_game_state_globals.buffer_size) &&
		game_state_image_accept(image, xbox_game_state_globals.buffer_size))
#else
	/* port: read beside the game state, and taken once checked */
	image = malloc(xbox_game_state_globals.buffer_size);
	if (image &&
		SetFilePointer(xbox_game_state_globals.handle, 0, NULL, FILE_BEGIN) !=
			INVALID_SET_FILE_POINTER &&
		ReadFile(xbox_game_state_globals.handle, image,
			xbox_game_state_globals.buffer_size, &bytes_read, NULL) &&
		bytes_read == xbox_game_state_globals.buffer_size &&
		game_state_image_accept(image, xbox_game_state_globals.buffer_size))
#endif""")),
]

# build 26: the Memory Stick's free space from the PSP itself (newlib's
# statvfs reports none, and the main menu then refuses to save: "not enough
# free blocks")
FIXES_23["port/linux/src/posix_files.c"] = [
("""	struct statvfs st;

	if (statvfs(path, &st) != 0)
		return -1;
	split64((unsigned long long)st.f_bavail * st.f_frsize, free_low, free_high);
	split64((unsigned long long)st.f_blocks * st.f_frsize, total_low, total_high);
	return 0;""",
 """#ifdef HALO_PSP
	/* sceIoDevctl's memory stick query (psp_fixups.py) */
	extern int sceIoDevctl(const char *device, unsigned int command, void *input, int input_length, void *output,
		int output_length);
	struct { unsigned int maximum_clusters, free_clusters, maximum_sectors, sector_size, sectors_per_cluster; }
		information, *pointer = &information;

	(void)path;
	if (sceIoDevctl("ms0:", 0x02425818, &pointer, sizeof(pointer), NULL, 0) < 0)
		return -1;
	split64((unsigned long long)information.free_clusters * information.sectors_per_cluster * information.sector_size,
		free_low, free_high);
	split64((unsigned long long)information.maximum_clusters * information.sectors_per_cluster *
		information.sector_size, total_low, total_high);
	return 0;
#else
	struct statvfs st;

	if (statvfs(path, &st) != 0)
		return -1;
	split64((unsigned long long)st.f_bavail * st.f_frsize, free_low, free_high);
	split64((unsigned long long)st.f_blocks * st.f_frsize, total_low, total_high);
	return 0;
#endif"""),
]

# build 26: the menus are laid out on 640 of the 848 columns, centred while
# they draw (halo_screen_ui_offset); the GE pillarboxes them then
FIXES_23["port/linux/src/d3d8_gl.c#ui"] = [
("""void halo_screen_ui_offset(unsigned char centered)
{""",
 """void halo_screen_ui_offset(unsigned char centered)
{
#ifdef HALO_PSP
	{
		extern void psp_ge_ui_centered(int centered);

		psp_ge_ui_centered(centered); /* psp_fixups.py */
	}
#endif"""),
]

# build 27: a BSP switch loads the next BSP where the last one was; the GE's
# copies of the level's vertices go with it
FIXES_23["source/cache/cache_files_windows.c#bsp"] = [
("""	rasterizer_globals.current_lock_operation = _rasterizer_lock_bsp_switch;
""",
 """	rasterizer_globals.current_lock_operation = _rasterizer_lock_bsp_switch;
#ifdef HALO_PSP
	{
		extern void psp_ge_forget_level(void);

		psp_ge_forget_level(); /* psp_fixups.py */
	}
#endif
"""),
]

# build 29: dynamic lights (the flashlight, muzzle flashes, plasma) on the
# level, lit per vertex on the CPU and added over the level's base texture
FIXES_23["source/rasterizer/rasterizer.c"].append(
("""	_rasterizer_environment_diffuse_light_begin(light_index);
	return;""",
 """#ifdef HALO_PSP
	{
		extern int psp_ge_active(void);
		extern void psp_ge_light_begin(const float *position, const float *forward, const float *color, float radius,
			float cosine_falloff, float cosine_cutoff);

		if (psp_ge_active())
		{
			if (light_index >= 0 && light_index < rasterizer_lights.light_count)
			{
				struct rasterizer_light_submit_parameters const *light = &rasterizer_lights.lights[light_index];
				/* the light's definition: cosines of its cone at +28 and +32 (object_lights.c) */
				char const *definition = (char const *)light->definition;

				psp_ge_light_begin(&light->position.x, &light->forward.i, &light->color.red, light->radius,
					definition ? *(float const *)(definition + 28) : -1.0f,
					definition ? *(float const *)(definition + 32) : -1.0f);
			}
			return;
		}
	}
#endif
	_rasterizer_environment_diffuse_light_begin(light_index);
	return;"""))
FIXES_23["source/rasterizer/rasterizer.c"].append(
("""	_rasterizer_environment_diffuse_light_draw(shader, bitmap_index, dynamic_triangle_buffer_index, first_triangle_index, triangle_count, vertex_buffer);""",
 """#ifdef HALO_PSP
	{
		extern int psp_ge_active(void);
		extern void psp_environment_light(long, long, long, struct vertex_buffer const *, unsigned int);

		if (psp_ge_active())
		{
			unsigned int base_key = 0;

			if (shader && *(short const *)((char const *)shader + 36) == 3)
				base_key = psp_tex_bitmap_key(*(long const *)((char const *)shader + 148), 0);
			psp_environment_light(dynamic_triangle_buffer_index, first_triangle_index, triangle_count, vertex_buffer,
				base_key);
			return;
		}
	}
#endif
	_rasterizer_environment_diffuse_light_draw(shader, bitmap_index, dynamic_triangle_buffer_index, first_triangle_index, triangle_count, vertex_buffer);"""))
FIXES_23["source/rasterizer/rasterizer.c"].append(
("""	_rasterizer_environment_diffuse_light_end();
	return;""",
 """#ifdef HALO_PSP
	{
		extern int psp_ge_active(void);

		if (psp_ge_active())
			return;
	}
#endif
	_rasterizer_environment_diffuse_light_end();
	return;"""))

# build 30: who asks for the large blocks (system_malloc's callers)
FIXES_23["source/cseries/debug_memory.c"] = [
("""	header = system_malloc(allocation_size);""",
 """#ifdef HALO_PSP
	if (size >= 0x10000)
		platform_psp_log("heap: %u KB for %s line %ld", size / 1024, file ? file : "?", line);
#endif
	header = system_malloc(allocation_size);"""),
]

# build 30: the frame statistics' 144 KB sorting buffer only serves the
# debug statistics view; the PSP doesn't allocate it (and counts nothing)
FIXES_23["source/rasterizer/rasterizer_frame_statistics.c"] = [
("""	rasterizer_frame_statistics_temp_buffer = match_malloc(
		"c:\\\\halo\\\\SOURCE\\\\rasterizer\\\\rasterizer_frame_statistics.c",
		41,
		sizeof(word) *
			RASTERIZER_MAXIMUM_TRIANGLES_PER_TRIANGLE_BUFFER *
			NUMBER_OF_VERTICES_PER_TRIANGLE);

	if (!rasterizer_frame_statistics_temp_buffer)""",
 """#ifdef HALO_PSP
	rasterizer_frame_statistics_temp_buffer = NULL; /* psp_fixups.py */
	if (FALSE)
#else
	rasterizer_frame_statistics_temp_buffer = match_malloc(
		"c:\\\\halo\\\\SOURCE\\\\rasterizer\\\\rasterizer_frame_statistics.c",
		41,
		sizeof(word) *
			RASTERIZER_MAXIMUM_TRIANGLES_PER_TRIANGLE_BUFFER *
			NUMBER_OF_VERTICES_PER_TRIANGLE);

	if (!rasterizer_frame_statistics_temp_buffer)
#endif"""),
("""		if (triangles)""",
 """		if (triangles && rasterizer_frame_statistics_temp_buffer)"""),
]
# which data arrays are large
FIXES_23["source/memory/data.c"] = [
("""	struct data_array *data = (struct data_array *)match_malloc(
		"c:\\\\halo\\\\SOURCE\\\\memory\\\\data.c",
		41,
		data_allocation_size(maximum_count, size));
""",
 """	struct data_array *data = (struct data_array *)match_malloc(
		"c:\\\\halo\\\\SOURCE\\\\memory\\\\data.c",
		41,
		data_allocation_size(maximum_count, size));
#ifdef HALO_PSP
	if (data_allocation_size(maximum_count, size) >= 0x10000)
		platform_psp_log("heap: data array '%s', %d of %d bytes", name, maximum_count, size);
#endif
"""),
]

# ---------- build 31: the widget layer's sprites and occlusion tests on the
# GE (lens flares, light volumes). The Xbox counts a flare's visible pixels
# with a hardware visibility query; the PSP has none, so one ray from the eye
# through the level and sight-blocking objects stands in for it, and the
# game's own fade (a quarter of the way up, half down, per tick) smooths the
# yes-or-no into a fade
FIXES_31 = {}
FIXES_31["source/rasterizer/xbox/rasterizer_xbox_widgets.c"] = [
("""#include "rasterizer/rasterizer_console_vars.h"
""",
 """#include "rasterizer/rasterizer_console_vars.h"
#ifdef HALO_PSP
#include "physics/collisions.h"
int psp_ge_active(void);
unsigned int psp_tex_bitmap_key(long, int);
int psp_tex_bitmap_count(long);
void psp_ge_widget_begin(int depth_test);
void psp_ge_widget_texture(unsigned int key);
void psp_ge_widget_occlusion(int visible);
void psp_ge_widget_sprite(float const *point, float radius, float scale_x, float scale_y, float rotation,
	unsigned long color);
int psp_keep(const char *name); /* psp_platform.c */
#define PSP_OCCLUSION_TESTS 256
#define PSP_OCCLUSION_RAYS 24
static long psp_occlusion_visible[PSP_OCCLUSION_TESTS];
static long psp_occlusion_frame_rays = 0, psp_occlusion_frame_index = -1;
/* build 44: a flare's ray is cast every third frame, the answer kept between
(the Xbox's own occlusion query answered a frame late; a flare fades over
many frames anyway). The rays cost about 0.3 ms each on the PSP: 1.3 ms a
frame driving down the valley in build 42. keep.txt's "flaresexact" casts
every frame */
#define PSP_OCCLUSION_KEEP_FRAMES 3
static unsigned long psp_occlusion_tested[PSP_OCCLUSION_TESTS];
static unsigned long psp_occlusion_frame = 1;
static int psp_occlusion_exact = -1;
unsigned long psp_occlusion_rays_cast = 0, psp_occlusion_rays_kept = 0; /* psp_ge.c logs them */
#endif
"""),
("""long _rasterizer_widget_get_occlusion_test_result(
	long occlusion_test_index)
{
	long occlusion_test_result = NONE;
""",
 """long _rasterizer_widget_get_occlusion_test_result(
	long occlusion_test_index)
{
	long occlusion_test_result = NONE;

#ifdef HALO_PSP
	if (psp_ge_active())
		return occlusion_test_index >= 0 && occlusion_test_index < PSP_OCCLUSION_TESTS ?
			psp_occlusion_visible[occlusion_test_index] : 0;
#endif
"""),
("""void _rasterizer_widget_begin(
	short type,
	word flags)
{
	real vertex_constants[5][4];
	short viewport_width;
	short viewport_height;
""",
 """void _rasterizer_widget_begin(
	short type,
	word flags)
{
	real vertex_constants[5][4];
	short viewport_width;
	short viewport_height;

#ifdef HALO_PSP
	if (psp_ge_active())
	{
		if (type == _widget_type_internal_sprite)
			psp_ge_widget_begin(TEST_FLAG(flags, _widget_zbuffer_enable_bit));
		return;
	}
#endif
"""),
("""boolean _rasterizer_widget_set_texture(
	short stage_index,
	long bitmap_group_index,
	short sequence_index)
{
	boolean success;
""",
 """boolean _rasterizer_widget_set_texture(
	short stage_index,
	long bitmap_group_index,
	short sequence_index)
{
	boolean success;

#ifdef HALO_PSP
	if (psp_ge_active())
	{
		/* the bitmap as rasterizer_set_texture_non_blocking picks it (the
		sequence index is the bitmap's, wrapped), or the game's glow */
		long tag_index = bitmap_group_index != NONE ? bitmap_group_index : global_rasterizer_data->glow.index;
		int count = psp_tex_bitmap_count(tag_index);

		psp_ge_widget_texture(count > 0 ? psp_tex_bitmap_key(tag_index, sequence_index % count) : 0);
		/* FALSE: ready (TRUE would tell a lens flare to wait for it) */
		return FALSE;
	}
#endif
"""),
("""	match_assert(
		"c:\\\\halo\\\\SOURCE\\\\rasterizer\\\\xbox\\\\rasterizer_xbox_widgets.c",
		334,
		global_d3d_device);
	D3DDevice_SetRenderState_ZEnable(zbuffer_enable);
""",
 """#ifdef HALO_PSP
	if (psp_ge_active())
	{
		psp_ge_widget_begin(zbuffer_enable);
		return;
	}
#endif
	match_assert(
		"c:\\\\halo\\\\SOURCE\\\\rasterizer\\\\xbox\\\\rasterizer_xbox_widgets.c",
		334,
		global_d3d_device);
	D3DDevice_SetRenderState_ZEnable(zbuffer_enable);
"""),
("""	match_assert(
		"c:\\\\halo\\\\SOURCE\\\\rasterizer\\\\xbox\\\\rasterizer_xbox_widgets.c",
		425,
		point);
""",
 """	match_assert(
		"c:\\\\halo\\\\SOURCE\\\\rasterizer\\\\xbox\\\\rasterizer_xbox_widgets.c",
		425,
		point);
#ifdef HALO_PSP
	if (psp_ge_active())
	{
		psp_ge_widget_sprite(&point->x, radius, scale ? scale->i : 1.0f, scale ? scale->j : 1.0f, rotation, color);
		return;
	}
#endif
"""),
("""	if (rasterizer_debug_options.lens_flare_occlusion_enabled)
	{
		if (rasterizer_widget_project_billboard(
			point,
			radius,
			&projected_center,
			&projected_axes))
		{
			projected_axes.i = MAX(1.0f, projected_axes.i);
			projected_axes.j = MAX(1.0f, projected_axes.j);
""",
 """#ifdef HALO_PSP
	if (psp_ge_active())
	{
		real on_screen = 0.0f;

		pixel_count = 0;
		if (rasterizer_widget_project_billboard(point, radius, &projected_center, &projected_axes))
		{
			/* the share of the test's square inside the viewport: the
			Xbox's query counts no pixels off the screen */
			real width = (real)(global_window_parameters.camera.viewport_bounds.x1 -
				global_window_parameters.camera.viewport_bounds.x0);
			real height = (real)(global_window_parameters.camera.viewport_bounds.y1 -
				global_window_parameters.camera.viewport_bounds.y0);
			real x0, x1, y0, y1;

			projected_axes.i = MAX(1.0f, projected_axes.i);
			projected_axes.j = MAX(1.0f, projected_axes.j);
			pixel_count = (long)(4.0f * projected_axes.i * projected_axes.j);
			x0 = MAX(0.0f, projected_center.x - projected_axes.i);
			x1 = MIN(width, projected_center.x + projected_axes.i);
			y0 = MAX(0.0f, projected_center.y - projected_axes.j);
			y1 = MIN(height, projected_center.y + projected_axes.j);
			if (x1 > x0 && y1 > y0)
				on_screen = (x1 - x0) * (y1 - y0) / (4.0f * projected_axes.i * projected_axes.j);
		}
		if (index >= 0 && index < PSP_OCCLUSION_TESTS)
		{
			long visible = 0;

			/* a budget of rays a frame (a new frame starts its tests again
			from index 0); past it, a flare counts as hidden */
			if (index <= psp_occlusion_frame_index)
			{
				psp_occlusion_frame_rays = 0;
				psp_occlusion_frame++;
			}
			psp_occlusion_frame_index = index;
			if (psp_occlusion_exact < 0)
				psp_occlusion_exact = psp_keep("flaresexact");
			if (!psp_occlusion_exact && pixel_count > 0 && on_screen > 0.0f &&
				psp_occlusion_tested[index] && psp_occlusion_frame - psp_occlusion_tested[index] < PSP_OCCLUSION_KEEP_FRAMES)
			{
				/* last cast's answer, scaled to this frame's size on screen */
				visible = psp_occlusion_visible[index] > 0 ? (long)(pixel_count * MIN(on_screen, 1.0f)) : 0;
				psp_occlusion_rays_kept++;
				psp_ge_widget_occlusion(visible > 0);
			}
			else if (pixel_count > 0 && on_screen > 0.0f && psp_occlusion_frame_rays < PSP_OCCLUSION_RAYS)
			{
				struct collision_result collision;
				real_vector3d ray;

				psp_occlusion_frame_rays++;
				psp_occlusion_rays_cast++;
				psp_occlusion_tested[index] = psp_occlusion_frame;
				vector_from_points3d(&global_window_parameters.camera.position, point, &ray);
				/* the level's sky surfaces (no material) are where the
				sky is drawn: a ray that meets one has reached the sky */
				if (!collision_test_vector(
					FLAG(_collision_test_front_facing_surfaces_bit) |
						FLAG(_collision_test_ignore_invisible_surfaces_bit) |
						FLAG(_collision_test_structure_bit) |
						FLAG(_collision_test_objects_bit) |
						_collision_test_objects_sight_blocking_flags,
					&global_window_parameters.camera.position,
					&ray,
					NONE,
					&collision) ||
					(collision.type == _collision_result_structure && collision.material_index == NONE))
					visible = (long)(pixel_count * MIN(on_screen, 1.0f));
				psp_ge_widget_occlusion(visible > 0);
			}
			psp_occlusion_visible[index] = visible;
		}
		return pixel_count;
	}
#endif
	if (rasterizer_debug_options.lens_flare_occlusion_enabled)
	{
		if (rasterizer_widget_project_billboard(
			point,
			radius,
			&projected_center,
			&projected_axes))
		{
			projected_axes.i = MAX(1.0f, projected_axes.i);
			projected_axes.j = MAX(1.0f, projected_axes.j);
"""),
]

# ---------- build 31: the portal walk's budget. structure_visibility_traverse_
# cluster walks every simple path through the portal graph (a cluster is
# unmarked when the walk leaves it), which outdoors, where many portals see
# each other, is thousands of steps a frame (2.7 ms on Blood Gulch in the
# emulator). Past a budget of steps the walk stops and the frame uses the
# camera cluster's potentially visible set instead, as structures_use_pvs_for_vs
# does: every cluster that could be seen, each clipped to the whole screen. That
# draws more than the walk would have found, never less, so nothing pops; and
# indoors, where the walk is short, nothing changes
FIXES_31["source/structures/structure_visibility.c"] = [
("""static void structure_visibility_traverse_cluster(
	short cluster_index,
	struct portal_hull const *visible_region)
{
	struct structure_bsp *structure = global_structure_bsp_get();""",
 """#ifdef HALO_PSP
#define PSP_PORTAL_STEPS 160
static long psp_portal_steps = 0;
static unsigned long psp_portal_walks = 0, psp_portal_fallbacks = 0, psp_portal_step_total = 0;
#endif

static void structure_visibility_traverse_cluster(
	short cluster_index,
	struct portal_hull const *visible_region)
{
	struct structure_bsp *structure = global_structure_bsp_get();
#ifdef HALO_PSP
	if (++psp_portal_steps > PSP_PORTAL_STEPS)
		return;
#endif"""),
("""	render.rendered_cluster_count = 0;
	structure_visibility_find_clusters();

	if (structures_use_pvs_for_vs)
	{""",
 """	render.rendered_cluster_count = 0;
#ifdef HALO_PSP
	psp_portal_steps = 0;
	if (!structures_use_pvs_for_vs)
	{
		void platform_psp_log(const char *format, ...);

		psp_portal_walk_id++;
		structure_visibility_find_clusters();
		psp_portal_walks++;
		psp_portal_step_total += psp_portal_steps;
		if (psp_portal_steps > PSP_PORTAL_STEPS)
			psp_portal_fallbacks++;
		if (psp_portal_walks % 900 == 0)
			platform_psp_log("portal walks: %lu, %lu over budget (falling back to the PVS), %lu steps a walk; portal hulls %lu worked out, %lu reused",
				psp_portal_walks, psp_portal_fallbacks, psp_portal_step_total / psp_portal_walks, psp_hull_misses, psp_hull_hits);
	}
	psp_pvs_fallback = psp_portal_steps > PSP_PORTAL_STEPS;
	if (structures_use_pvs_for_vs || psp_portal_steps > PSP_PORTAL_STEPS)
#else
	structure_visibility_find_clusters();

	if (structures_use_pvs_for_vs)
#endif
	{"""),
]

# build 59: after the fallback above, each rendered cluster has a new slot,
# and the frusta the walk built belong to the old slots (or to none). The
# fallback means "the whole screen", so it uses the camera's own frustum, as
# structures_use_pvs_for_vs does (it never happened on Blood Gulch: 0 walks
# over budget in b58, but a bigger campaign map would have culled the wrong
# surfaces). And "visdump": what the walk found, logged every 150 walks
FIXES_31["source/structures/structure_visibility.c#fallback"] = [
("""static void structure_visibility_traverse_subclusters(
	struct structure_bsp *structure)
{
	long rendered_cluster_index;
""",
 """#ifdef HALO_PSP
static boolean psp_pvs_fallback = FALSE;
#define PSP_PVS_FALLBACK psp_pvs_fallback
#else
#define PSP_PVS_FALLBACK 0
#endif
static void structure_visibility_traverse_subclusters(
	struct structure_bsp *structure)
{
	long rendered_cluster_index;
"""),
("""		frustum = structures_use_pvs_for_vs || render.cluster_index == NONE
			? &render.frustum
			: &rendered_cluster->frustum;

		for (subcluster_index = 0;""",
 """		frustum = structures_use_pvs_for_vs || PSP_PVS_FALLBACK || render.cluster_index == NONE
			? &render.frustum
			: &rendered_cluster->frustum;

		for (subcluster_index = 0;"""),
("""		struct render_frustum *frustum = structures_use_pvs_for_vs || render.cluster_index == NONE
			? &render.frustum
			: &rendered_cluster->frustum;
		long consumed_surface_index_count = 0;""",
 """		struct render_frustum *frustum = structures_use_pvs_for_vs || PSP_PVS_FALLBACK || render.cluster_index == NONE
			? &render.frustum
			: &rendered_cluster->frustum;
		long consumed_surface_index_count = 0;"""),
("""		structure_visibility_traverse_surface_lists(structure);
	}

	return;
}""",
 """		structure_visibility_traverse_surface_lists(structure);
	}

#ifdef HALO_PSP
	{
		int psp_keep(const char *name);
		void platform_psp_log(const char *format, ...);
		static int dump = -1;
		static unsigned long calls = 0;

		if (dump < 0)
			dump = psp_keep("visdump");
		if (dump && ++calls % 150 == 0)
		{
			char text[400];
			int used = 0;
			short index;

			for (index = 0; index < render.rendered_cluster_count && used < (int)sizeof(text) - 60; index++)
			{
				struct rendered_cluster *rendered = rendered_cluster_get(index);

				used += snprintf(text + used, sizeof(text) - used, " %d[%.2f %.2f %.2f %.2f]",
					rendered->cluster_index, rendered->clip_bounds.x0, rendered->clip_bounds.x1,
					rendered->clip_bounds.y0, rendered->clip_bounds.y1);
			}
			platform_psp_log("visdump: camera %.3f %.3f %.3f leaf %ld cluster %ld; %d clusters, %ld surfaces%s;%s",
				render.camera.position.x, render.camera.position.y, render.camera.position.z,
				(long)render.leaf_index, (long)render.cluster_index, (int)render.rendered_cluster_count,
				(long)render.environment_surface_count, psp_pvs_fallback ? " (PVS)" : "", text);
		}
	}
#endif
	return;
}"""),
]

# and the portal hulls (build 31): the walk asks for the same portal's
# screen outline again on every path that reaches it (about 450 times a
# frame on Blood Gulch for some 60 portals); within one walk the camera is the
# same, so each is worked out once and kept
FIXES_31["source/structures/structure_visibility.c#hulls"] = [
Either(universal=("""static short portal_hull_from_portal(
	short portal_index,
	boolean direction,
	struct portal_hull *result)
{
	struct structure_bsp *structure = global_structure_bsp_get();
	struct structure_visibility_portal *portal = TAG_BLOCK_GET_ELEMENT(
		&structure->cluster_portals,
		portal_index,
		struct structure_visibility_portal);

	return portal_hull_from_points(""",
 """#ifdef HALO_PSP
#define PSP_HULL_CACHE 128
#define PSP_HULL_VERTICES 8
static struct
{
	unsigned long walk;
	long key;
	short result, vertex_count;
	real_point2d vertices[PSP_HULL_VERTICES];
} psp_hull_cache[PSP_HULL_CACHE];
static unsigned long psp_portal_walk_id = 1, psp_hull_hits = 0, psp_hull_misses = 0;

static short portal_hull_from_portal_uncached(
	short portal_index,
	boolean direction,
	struct portal_hull *result);

static short portal_hull_from_portal(
	short portal_index,
	boolean direction,
	struct portal_hull *result)
{
	long key = 2 * (long)portal_index + (direction ? 1 : 0);
	unsigned int slot = (unsigned int)key & (PSP_HULL_CACHE - 1);
	short answer;

	if (psp_hull_cache[slot].walk == psp_portal_walk_id && psp_hull_cache[slot].key == key)
	{
		psp_hull_hits++;
		result->vertex_count = psp_hull_cache[slot].vertex_count;
		if (result->vertex_count > 0)
			csmemcpy(result->vertices, psp_hull_cache[slot].vertices,
				result->vertex_count * sizeof(real_point2d));
		return psp_hull_cache[slot].result;
	}
	psp_hull_misses++;
	answer = portal_hull_from_portal_uncached(portal_index, direction, result);
	if (result->vertex_count <= PSP_HULL_VERTICES)
	{
		psp_hull_cache[slot].walk = psp_portal_walk_id;
		psp_hull_cache[slot].key = key;
		psp_hull_cache[slot].result = answer;
		psp_hull_cache[slot].vertex_count = result->vertex_count;
		if (result->vertex_count > 0)
			csmemcpy(psp_hull_cache[slot].vertices, result->vertices,
				result->vertex_count * sizeof(real_point2d));
	}
	return answer;
}

static short portal_hull_from_portal_uncached(
#else
static short portal_hull_from_portal(
#endif
	short portal_index,
	boolean direction,
	struct portal_hull *result)
{
	struct structure_bsp *structure = global_structure_bsp_get();
	struct structure_visibility_portal *portal = TAG_BLOCK_GET_ELEMENT(
		&structure->cluster_portals,
		portal_index,
		struct structure_visibility_portal);

	result->vertex_count = 0;
	return portal_hull_from_points("""),
 # ChupathingyCE
 chupa=("""static short portal_hull_from_portal(
	short portal_index,
	boolean direction,
	struct portal_hull *result)
{
	struct structure_bsp *structure = global_structure_bsp_get();
	struct structure_visibility_portal *portal = TAG_BLOCK_GET_ELEMENT(
		&structure->cluster_portals,
		portal_index,
		struct structure_visibility_portal);

	/* port: a portal (from the map) whose plane is no plane is not seen
""",
  """#ifdef HALO_PSP
#define PSP_HULL_CACHE 128
#define PSP_HULL_VERTICES 8
static struct
{
	unsigned long walk;
	long key;
	short result, vertex_count;
	real_point2d vertices[PSP_HULL_VERTICES];
} psp_hull_cache[PSP_HULL_CACHE];
static unsigned long psp_portal_walk_id = 1, psp_hull_hits = 0, psp_hull_misses = 0;

static short portal_hull_from_portal_uncached(
	short portal_index,
	boolean direction,
	struct portal_hull *result);

static short portal_hull_from_portal(
	short portal_index,
	boolean direction,
	struct portal_hull *result)
{
	long key = 2 * (long)portal_index + (direction ? 1 : 0);
	unsigned int slot = (unsigned int)key & (PSP_HULL_CACHE - 1);
	short answer;

	if (psp_hull_cache[slot].walk == psp_portal_walk_id && psp_hull_cache[slot].key == key)
	{
		psp_hull_hits++;
		result->vertex_count = psp_hull_cache[slot].vertex_count;
		if (result->vertex_count > 0)
			csmemcpy(result->vertices, psp_hull_cache[slot].vertices,
				result->vertex_count * sizeof(real_point2d));
		return psp_hull_cache[slot].result;
	}
	psp_hull_misses++;
	answer = portal_hull_from_portal_uncached(portal_index, direction, result);
	if (result->vertex_count <= PSP_HULL_VERTICES)
	{
		psp_hull_cache[slot].walk = psp_portal_walk_id;
		psp_hull_cache[slot].key = key;
		psp_hull_cache[slot].result = answer;
		psp_hull_cache[slot].vertex_count = result->vertex_count;
		if (result->vertex_count > 0)
			csmemcpy(psp_hull_cache[slot].vertices, result->vertices,
				result->vertex_count * sizeof(real_point2d));
	}
	return answer;
}

static short portal_hull_from_portal_uncached(
#else
static short portal_hull_from_portal(
#endif
	short portal_index,
	boolean direction,
	struct portal_hull *result)
{
	struct structure_bsp *structure = global_structure_bsp_get();
	struct structure_visibility_portal *portal = TAG_BLOCK_GET_ELEMENT(
		&structure->cluster_portals,
		portal_index,
		struct structure_visibility_portal);

	result->vertex_count = 0;
	/* port: a portal (from the map) whose plane is no plane is not seen
""")),
]

# build 32: a vehicle's update, split (psp_platform.c's object report)
FIXES_31["source/objects/objects.c#vehicles"] = [
("""		object_type_update(object_index);
		if (object_definition->object.collision_model.index!=NONE)
		{
			object_damage_update(object_index);
		}

		object_type_export_function_values(object_index);

		if (!TEST_FLAG(object->object.flags, _object_do_not_recompute_node_matrices_bit))
		{
			object_compute_node_matrices(object_index);
		}
""",
 """#ifdef HALO_PSP
		{
			/* build 45: bipeds split the same way (accounts 17-19), for the
			campaign's 25-30 of them a tick */
			int timed = object->object.type == _object_type_vehicle ? 14 : object->object.type == _object_type_biped ? 17 : 0;
			unsigned long mark = timed ? psp_microseconds() : 0, now;

			object_type_update(object_index);
			if (timed)
			{
				now = psp_microseconds();
				psp_object_account(timed, now - mark);
				mark = now;
			}
			if (object_definition->object.collision_model.index!=NONE)
			{
				object_damage_update(object_index);
			}
			object_type_export_function_values(object_index);
			if (timed)
			{
				now = psp_microseconds();
				psp_object_account(timed + 1, now - mark);
				mark = now;
			}
			if (!TEST_FLAG(object->object.flags, _object_do_not_recompute_node_matrices_bit))
			{
				object_compute_node_matrices(object_index);
			}
			if (timed)
				psp_object_account(timed + 2, psp_microseconds() - mark);
		}
#else
		object_type_update(object_index);
		if (object_definition->object.collision_model.index!=NONE)
		{
			object_damage_update(object_index);
		}

		object_type_export_function_values(object_index);

		if (!TEST_FLAG(object->object.flags, _object_do_not_recompute_node_matrices_bit))
		{
			object_compute_node_matrices(object_index);
		}
#endif
"""),
]

# build 36: particles. Sheepdog01's b35 run: under sustained assault rifle fire the
# game's frame step (particles_update, particle_systems_update...) took 15-39
# ms a frame and render_particles 10-14 more: each free particle (a spark, a
# chip of rock) runs point physics with a collision ray every frame, and
# sparks bounce rather than settle. The PSP keeps a budget of free particles
# alive at once (keep.txt "fullparticles": the Xbox's 1024); past it a new
# one isn't made, so a long burst's shower thins instead of the frame rate
# falling. Attached ones (muzzle flashes, on objects) don't count and are
# always made. Logged every 900 updates: the peak, refusals, physics a frame
FIXES_36 = {}
# build 95: high-water marks for the Xbox-sized buffers (campaign memory
# plan): the sound cache's working set, loads and failures; the D3D layer's
# dynamic vertex groups, triangles and buffer slots, each frame's peak
FIXES_36["source/cache/xbox_sound_cache.c#highwater"] = [
("""	cache_block_index = lruv_block_new(
		xbox_sound_cache_globals.cache,
		sound->samples.size);
""", """	cache_block_index = lruv_block_new(
		xbox_sound_cache_globals.cache,
		sound->samples.size);
#ifdef HALO_PSP
	if (cache_block_index != NONE)
	{
		psp_sound_cache_loads++;
		psp_sound_cache_load_bytes += sound->samples.size;
		if (sound->samples.size > psp_sound_cache_largest)
			psp_sound_cache_largest = sound->samples.size;
	}
	else
		psp_sound_cache_failures++;
#endif
"""),
("static void sound_cache_start_loading_sound(\n\tstruct sound_permutation *sound)\n{",
 """#ifdef HALO_PSP
/* build 95 (psp_fixups.py): the sound cache's use, from the game thread every
5 s (psp_platform.c). An LRU cache fills by design; what it needs is its
working set: the pages of sounds playing (locked) or used in the last second */
static unsigned long psp_sound_cache_loads = 0, psp_sound_cache_load_bytes = 0, psp_sound_cache_failures = 0,
	psp_sound_cache_largest = 0, psp_sound_cache_peak_pages = 0;

void psp_sound_cache_report(void)
{
	static byte usage[4096];
	long pages = PSP_SOUND_CACHE_SIZE >> 12, k, occupied = 0, recent = 0, locked = 0;

	if (!xbox_sound_cache_globals.cache || pages > (long)sizeof(usage))
		return;
	lruv_cache_get_page_usage(xbox_sound_cache_globals.cache, usage);
	for (k = 0; k < pages; k++)
	{
		if (usage[k])
			occupied++;
		if (usage[k] && !(usage[k] & 4))
			recent++;
		if (usage[k] & 8)
			locked++;
	}
	if ((unsigned long)recent > psp_sound_cache_peak_pages)
		psp_sound_cache_peak_pages = recent;
	platform_psp_log("sound cache: %ld KB occupied of %ld; used in the last second %ld KB (peak %lu KB), playing %ld KB; %lu loads (%lu KB, largest %lu KB), %lu failed for room",
		occupied * 4, pages * 4, recent * 4, psp_sound_cache_peak_pages * 4, locked * 4, psp_sound_cache_loads,
		psp_sound_cache_load_bytes / 1024, psp_sound_cache_largest / 1024, psp_sound_cache_failures);
}
#endif

static void sound_cache_start_loading_sound(
	struct sound_permutation *sound)
{"""),
]
FIXES_36["source/rasterizer/xbox/rasterizer_xbox_draw_primitives.c#highwater"] = [
("void rasterizer_dynamic_geometry_begin(\n\tvoid)\n{\n\tlong vertex_type;\n",
 """#ifdef HALO_PSP
/* build 95 (psp_fixups.py): each frame's use of the dynamic buffers, the
peak kept for the heartbeat (psp_platform.c) */
static long psp_dynamic_vertex_peak[NUMBER_OF_RASTERIZER_VERTEX_TYPES], psp_dynamic_triangle_peak = 0,
	psp_dynamic_buffer_peak[2];

void psp_dynamic_geometry_report(void)
{
	char line[400];
	int length = 0;
	long vertex_type;

	for (vertex_type = 0; vertex_type < NUMBER_OF_RASTERIZER_VERTEX_TYPES; vertex_type++)
	{
		long size = rasterizer_geometry_get_vertex_size(vertex_type);

		if (!dynamic_vertices.groups[vertex_type].total_vertex_count)
			continue;
		length += snprintf(line + length, sizeof(line) - length, " type %ld: %ld of %ld vertices (%ld of %ld KB);",
			vertex_type, psp_dynamic_vertex_peak[vertex_type], dynamic_vertices.groups[vertex_type].total_vertex_count,
			psp_dynamic_vertex_peak[vertex_type] * size / 1024, dynamic_vertices.groups[vertex_type].total_vertex_count * size / 1024);
	}
	platform_psp_log("dynamic geometry, frame peaks:%s triangles %ld of %d (%ld of %d KB); buffers %ld and %ld of %d and %d",
		line, psp_dynamic_triangle_peak, RASTERIZER_MAXIMUM_DYNAMIC_TRIANGLES,
		psp_dynamic_triangle_peak * 6 / 1024, RASTERIZER_MAXIMUM_DYNAMIC_TRIANGLES * 6 / 1024,
		psp_dynamic_buffer_peak[0], psp_dynamic_buffer_peak[1], RASTERIZER_MAXIMUM_DYNAMIC_VERTEX_BUFFERS,
		RASTERIZER_MAXIMUM_DYNAMIC_TRIANGLE_BUFFERS);
}
#endif

void rasterizer_dynamic_geometry_begin(
	void)
{
	long vertex_type;
#ifdef HALO_PSP
	for (vertex_type = 0; vertex_type < NUMBER_OF_RASTERIZER_VERTEX_TYPES; vertex_type++)
		if (dynamic_vertices.groups[vertex_type].vertex_count > psp_dynamic_vertex_peak[vertex_type])
			psp_dynamic_vertex_peak[vertex_type] = dynamic_vertices.groups[vertex_type].vertex_count;
	if (dynamic_triangles.triangle_count > psp_dynamic_triangle_peak)
		psp_dynamic_triangle_peak = dynamic_triangles.triangle_count;
	if (dynamic_vertices.buffer_count > psp_dynamic_buffer_peak[0])
		psp_dynamic_buffer_peak[0] = dynamic_vertices.buffer_count;
	if (dynamic_triangles.buffer_count > psp_dynamic_buffer_peak[1])
		psp_dynamic_buffer_peak[1] = dynamic_triangles.buffer_count;
#endif
"""),
]
# build 93: the debug-drawing vertex buffer (render_debug's lines and
# markers) was 24,576 vertices, 384 KB of the heap; nothing in a PSP build
# draws debug geometry, and a request past the size is refused and warned
# once (the allocator checks maximum_vertex_count). 1,024 keeps it working
# for a debug line or two. The Library ran out of heap at its first frame
FIXES_36["source/rasterizer/xbox/rasterizer_xbox_draw_primitives.c#debugcap"] = [
("	RASTERIZER_MAXIMUM_DYNAMIC_DEBUG_VERTICES = 24576,",
 "#ifdef HALO_PSP\n	RASTERIZER_MAXIMUM_DYNAMIC_DEBUG_VERTICES = 1024, /* psp_fixups.py, build 93: 384 KB of heap */\n#else\n	RASTERIZER_MAXIMUM_DYNAMIC_DEBUG_VERTICES = 24576,\n#endif"),
]
# build 37: the game's matrix product on the VFPU (psp/src/psp_vfpu.c), which
# checks itself against the FPU at start-up; keep.txt "novfpu" turns it off
# build 37: the object's lighting, its own step (render_object, which
# breadcrumbs() also steps through)
FIXES_36["source/render/render_objects.c"] = [
("""			if (needs_lighting)
			{
				data->lighting = object_get_cached_render_lighting(""",
 """			if (needs_lighting)
			{
#ifdef HALO_PSP
				{
					void psp_step(const char *name);

					psp_step("object_get_cached_render_lighting");
				}
#endif
				data->lighting = object_get_cached_render_lighting("""),
]
# build 38: the first-person projection ends with its model
FIXES_36["source/rasterizer/rasterizer.c#model_end"] = [
("""void rasterizer_model_end(
	void)
{
	_rasterizer_model_end();
	return;
}""",
 """void rasterizer_model_end(
	void)
{
	_rasterizer_model_end();
#ifdef HALO_PSP
	{
		extern void psp_ge_model_shape(int, const float *, float, float, float);

		psp_ge_model_shape(0, NULL, 0.0f, 0.0f, 0.0f);
	}
#endif
	return;
}"""),
]
FIXES_36["source/math/matrix_math.c"] = [
("""void matrix4x3_multiply(
	real_matrix4x3 const *a,
	real_matrix4x3 const *b,
	real_matrix4x3 *result)
{
	real const *a_elements = a->n[0];""",
 """#ifdef HALO_PSP
extern int psp_vfpu_on;
void psp_vfpu_matrix4x3_multiply(const void *a, const void *b, void *result);
#endif

void matrix4x3_multiply(
	real_matrix4x3 const *a,
	real_matrix4x3 const *b,
	real_matrix4x3 *result)
{
#ifdef HALO_PSP
	if (psp_vfpu_on)
	{
		psp_vfpu_matrix4x3_multiply(a, b, result);
		return;
	}
#endif
	{
	real const *a_elements = a->n[0];"""),
("""		for (row = 0; row < 12; row++)
			result_elements[row] = product[row];
	}

	result->scale = a->scale * b->scale;

	return;
}""",
 """		for (row = 0; row < 12; row++)
			result_elements[row] = product[row];
	}

	result->scale = a->scale * b->scale;
	}

	return;
}"""),
]

# build 36: the HUD. At the Xbox's size the HUD covers the same share of the
# PSP's screen as of a TV, which on 4.3 inches is small (Sheepdog01, b35). Halo
# places each element from a corner (or the centre) of the window's bounds,
# so the weapon and unit HUD and the messages are laid out in a window k
# times smaller and the GE draws them k times larger: every element grows
# and stays on its own corner. (The game's own HUD scale, for split screen,
# is overridden per element in multiplayer.) Names over players, nav points
# and damage arrows follow the 3D view and keep the full window.
# keep.txt: hud100, hud115, hud130, hud145, hud160 (default 1.5)
FIXES_36["source/interface/hud.c"] = [
("""void hud_draw_screen(
	void)
{""",
 """#ifdef HALO_PSP
int psp_keep(const char *name); /* psp_platform.c; psp_fixups.py */
void psp_ge_hud_zoom(float zoom);
static rectangle2d psp_hud_window, psp_hud_viewport;
static int psp_hud_zoomed = 0;
/* build 40: while zoomed, corner elements move this far out toward their
side (the game's own window units; hud_draw.c's hud_calculate_point) */
short psp_hud_push = 0;
#define PSP_HUD_PUSH 32

static void psp_hud_zoom_begin(void)
{
	static float zoom = 0.0f;
	short *w = render.camera.window_bounds.v, *v = render.camera.viewport_bounds.v;
	int k;

	if (zoom == 0.0f)
		zoom = psp_keep("hud100") ? 1.0f : psp_keep("hud115") ? 1.15f : psp_keep("hud130") ? 1.3f :
			psp_keep("hud145") ? 1.45f : psp_keep("hud160") ? 1.6f : 1.5f; /* build 40: 1.5 (Sheepdog01, b39: 1.3 readable with the full-size textures, but small) */
	if (psp_hud_zoomed || zoom == 1.0f)
		return;
	psp_hud_window = render.camera.window_bounds;
	psp_hud_viewport = render.camera.viewport_bounds;
	for (k = 0; k < 4; k++)
	{
		w[k] = (short)(w[k] / zoom + 0.5f);
		v[k] = (short)(v[k] / zoom + 0.5f);
	}
	psp_ge_hud_zoom(zoom);
	psp_hud_push = psp_keep("hudnopush") ? 0 : PSP_HUD_PUSH;
	psp_hud_zoomed = 1;
}

static void psp_hud_zoom_end(void)
{
	if (!psp_hud_zoomed)
		return;
	render.camera.window_bounds = psp_hud_window;
	render.camera.viewport_bounds = psp_hud_viewport;
	psp_ge_hud_zoom(1.0f);
	psp_hud_push = 0;
	psp_hud_zoomed = 0;
}
#define PSP_HUD_ZOOM_BEGIN psp_hud_zoom_begin();
#define PSP_HUD_ZOOM_END psp_hud_zoom_end();
#else
#define PSP_HUD_ZOOM_BEGIN
#define PSP_HUD_ZOOM_END
#endif

void hud_draw_screen(
	void)
{"""),
("""				hud_render_weapon_interface(player);
				hud_show_action_response(player_index);
				hud_play_unit_sounds(player, hud_scripted_globals->show_hud);
				hud_render_unit_interface(player);
				hud_render_nav_points(render.local_player_index);""",
 """				PSP_HUD_ZOOM_BEGIN
				hud_render_weapon_interface(player);
				hud_show_action_response(player_index);
				hud_play_unit_sounds(player, hud_scripted_globals->show_hud);
				hud_render_unit_interface(player);
				PSP_HUD_ZOOM_END
				hud_render_nav_points(render.local_player_index);"""),
("""			else
			{
				hud_show_action_response(player_index);
				hud_play_unit_sounds(player, hud_scripted_globals->show_hud);
			}""",
 """			else
			{
				PSP_HUD_ZOOM_BEGIN
				hud_show_action_response(player_index);
				PSP_HUD_ZOOM_END
				hud_play_unit_sounds(player, hud_scripted_globals->show_hud);
			}"""),
("""		hud_messaging_update(render.local_player_index);
	}

	rasterizer_hud_end();""",
 """		PSP_HUD_ZOOM_BEGIN
		hud_messaging_update(render.local_player_index);
		PSP_HUD_ZOOM_END
	}

	rasterizer_hud_end();"""),
]

# build 40: the corner elements pushed out toward the screen's sides. Halo
# keeps them a TV's safe area in from each edge, which the zoom made larger
# still (Sheepdog01, b39: "push it more to the outer sides horizontally"). A fixed
# distance, so the elements of one corner keep their places to each other
FIXES_36["source/interface/hud_draw.c"] = [
("""		point.y = placement->offset.y *
			(TEST_FLAG(corner, _hud_anchor_bottom_bit) ? -1 : 1) * scale +
			render.camera.window_bounds.v[corner & FLAG(_hud_anchor_bottom_bit)] -
			render.camera.viewport_bounds.y0;
	}""",
 """		point.y = placement->offset.y *
			(TEST_FLAG(corner, _hud_anchor_bottom_bit) ? -1 : 1) * scale +
			render.camera.window_bounds.v[corner & FLAG(_hud_anchor_bottom_bit)] -
			render.camera.viewport_bounds.y0;
#ifdef HALO_PSP
		{
			extern short psp_hud_push; /* hud.c (psp_fixups.py) */

			point.x += TEST_FLAG(corner, _hud_anchor_right_bit) ? psp_hud_push : -psp_hud_push;
		}
#endif
	}"""),
]

# build 41: the frame throttle's wait. It spun on the vertical blank count,
# which a thread of the port's (d3d8_gl.c) moves on at 60 Hz; a PSP thread
# runs until it waits, so the spinning game thread kept that thread from
# ever running, and after a second the game gave up on its frame-rate
# control (Sheepdog01's red "stuck waiting for VBLANK callback!"). Waiting a
# millisecond at a time lets it run
FIXES_36["source/main/main.c"] = [
("""				if (precache_in_progress)
				{
					Sleep(1);
				}
				if (system_milliseconds() > start_milliseconds + 1000)""",
 """#ifdef HALO_PSP
				Sleep(1);
#else
				if (precache_in_progress)
				{
					Sleep(1);
				}
#endif
				if (system_milliseconds() > start_milliseconds + 1000)"""),
]

# build 44: the text picture's cache lines go out when a character is added
# to it, not every time one is drawn (psp_ge.c's text_character)
FIXES_36["source/rasterizer/rasterizer_text.c"] = [
Either(universal=("""		rasterizer_bitmap_changed(hardware_character_cache.bitmap);

		hardware_character_cache.x0 += font_character->bitmap_width;""",
 """		rasterizer_bitmap_changed(hardware_character_cache.bitmap);
#ifdef HALO_PSP
		{
			/* the GE's copy of the picture (psp_ge.c; psp_fixups.py) */
			extern void psp_ge_text_changed(void const *bitmap, int x, int y, int width, int height);

			psp_ge_text_changed(hardware_character_cache.bitmap, hardware_character->x0, hardware_character->y0,
				font_character->bitmap_width, font_character->bitmap_height);
		}
#endif

		hardware_character_cache.x0 += font_character->bitmap_width;"""),
 # ChupathingyCE
 chupa=("""		rasterizer_bitmap_changed(hardware_character_cache.bitmap);

		hardware_character_cache.x0 += cell_width;""",
  """		rasterizer_bitmap_changed(hardware_character_cache.bitmap);
#ifdef HALO_PSP
		{
			/* the GE's copy of the picture (psp_ge.c; psp_fixups.py): the
			whole cell written, its border included */
			extern void psp_ge_text_changed(void const *bitmap, int x, int y, int width, int height);

			psp_ge_text_changed(hardware_character_cache.bitmap, hardware_character->x0, hardware_character->y0,
				cell_width, cell_height);
		}
#endif

		hardware_character_cache.x0 += cell_width;""")),
]

# build 44: where a driven vehicle's physics spends its time (8-10 ms a tick
# on the PSP for the warthog and the scorpion, build 41): the ground test per
# mass point (collision_get_features_in_sphere), the swept ray per mass point,
# and the unit collisions. Logged every 600 updates
FIXES_36["source/physics/physics.c"] = [
("""void physics_compute_new(
	struct physics_instance const *instance,
	struct powered_mass_point_datum const *powered_mass_points,
	struct mass_point_datum *mass_points,
	real_vector3d *total_force,
	real_vector3d *total_torque)
{""",
 """#ifdef HALO_PSP
unsigned long psp_microseconds(void);
void platform_psp_log(const char *format, ...);
static unsigned long psp_physics_us[4], psp_physics_updates = 0, psp_physics_mass_points = 0;
#define PSP_PHYSICS_GROUND 0
#define PSP_PHYSICS_COMPUTE 1
#define PSP_PHYSICS_SWEEP 2
#define PSP_PHYSICS_UNITS 3
#endif

void physics_compute_new(
	struct physics_instance const *instance,
	struct powered_mass_point_datum const *powered_mass_points,
	struct mass_point_datum *mass_points,
	real_vector3d *total_force,
	real_vector3d *total_torque)
{"""),
("""void physics_update(
	long object_index,
	struct powered_mass_point_datum *powered_mass_points,
	struct mass_point_datum *mass_points,
	real_vector3d const *magic_force,
	real_vector3d const *magic_torque)
{""",
 """void physics_update(
	long object_index,
	struct powered_mass_point_datum *powered_mass_points,
	struct mass_point_datum *mass_points,
	real_vector3d const *magic_force,
	real_vector3d const *magic_torque)
{
#ifdef HALO_PSP
	unsigned long psp_mark;
#endif"""),
("""	physics_compute_new(
		&instance,
		powered_mass_points,
		mass_points,
		&total_force,
		&total_torque);

	{
		struct vehicle_datum *vehicle = vehicle_datum_get(object_index);
""",
 """#ifdef HALO_PSP
	psp_mark = psp_microseconds();
#endif
	physics_compute_new(
		&instance,
		powered_mass_points,
		mass_points,
		&total_force,
		&total_torque);
#ifdef HALO_PSP
	psp_physics_us[PSP_PHYSICS_COMPUTE] += psp_microseconds() - psp_mark;
	psp_physics_mass_points += physics->mass_points.count;
#endif

	{
		struct vehicle_datum *vehicle = vehicle_datum_get(object_index);
"""),
("""	physics_update_new(
		&instance,
		powered_mass_points,
		mass_points,
		&total_force,
		&total_torque);
	physics_compute_unit_collisions(object_index);

	return;
}""",
 """#ifdef HALO_PSP
	psp_mark = psp_microseconds();
#endif
	physics_update_new(
		&instance,
		powered_mass_points,
		mass_points,
		&total_force,
		&total_torque);
#ifdef HALO_PSP
	psp_physics_us[PSP_PHYSICS_SWEEP] += psp_microseconds() - psp_mark;
	psp_mark = psp_microseconds();
#endif
	physics_compute_unit_collisions(object_index);
#ifdef HALO_PSP
	psp_physics_us[PSP_PHYSICS_UNITS] += psp_microseconds() - psp_mark;
	if (++psp_physics_updates % 600 == 0)
	{
		platform_psp_log("physics: %lu vehicle updates, %.1f mass points each: us an update: ground tests %lu (in compute %lu), swept rays and integration %lu, unit collisions %lu",
			psp_physics_updates, (double)psp_physics_mass_points / 600.0, psp_physics_us[PSP_PHYSICS_GROUND] / 600,
			psp_physics_us[PSP_PHYSICS_COMPUTE] / 600, psp_physics_us[PSP_PHYSICS_SWEEP] / 600, psp_physics_us[PSP_PHYSICS_UNITS] / 600);
		psp_physics_us[0] = psp_physics_us[1] = psp_physics_us[2] = psp_physics_us[3] = 0;
		psp_physics_mass_points = 0;
	}
#endif

	return;
}"""),
("""		compute_ground_plane(instance->object_index, mass_point, mass_point_definition);
		mass_point->water_depth = scenario_location_water_depth(&mass_point->location, &mass_point->position);""",
 """#ifdef HALO_PSP
		{
			unsigned long psp_ground_mark = psp_microseconds();

			compute_ground_plane(instance->object_index, mass_point, mass_point_definition);
			psp_physics_us[PSP_PHYSICS_GROUND] += psp_microseconds() - psp_ground_mark;
		}
#else
		compute_ground_plane(instance->object_index, mass_point, mass_point_definition);
#endif
		mass_point->water_depth = scenario_location_water_depth(&mass_point->location, &mass_point->position);"""),
]

# build 44: the game state's 3.3 MB, by allocation (once, at load) and, every
# few heartbeats, how full each data array and memory pool in it gets: the
# sizes are the Xbox's limits (psp_capacity.h), the counts what Blood Gulch
# and the campaign really reach
# build 45: what the biped pill sweep costs, and how much of it is bipeds
# standing still (b30 recon: biped_update_physics was 27% of the tick)
# build 45: collision_get_features_in_sphere is the shared hot path (biped
# pill sweeps, vehicle mass points, projectiles): time its BSP phase and its
# objects phase, and count the objects it walks against the ones in reach
FIXES_36["source/physics/collisions.c#features"] = [
("""		collision_log_usage(_collision_function_vector_bounds_object);
		collision_log_start_time(&collision_usage_times.features);
		radius += 0.0625f;

		if (collision_bsp_test_sphere(
			collision_bsp,
			MAXIMUM_BREAKABLE_SURFACES_PER_MAP,
			breakable_surface_flags_get(),
			center,
			radius,
			&result) &&
			structure_flags)
		{
			collision_bsp_get_features_in_sphere(
				collision_bsp,
				&result,
				NULL,
				height,
				width,
				NONE,
				features);
		}

		if (objects && result.leaf_count > 0)
		{
""",
 """		collision_log_usage(_collision_function_vector_bounds_object);
		collision_log_start_time(&collision_usage_times.features);
		radius += 0.0625f;
#ifdef HALO_PSP
		psp_features_calls++;
		psp_features_mark = psp_microseconds();
#endif

		if (collision_bsp_test_sphere(
			collision_bsp,
			MAXIMUM_BREAKABLE_SURFACES_PER_MAP,
			breakable_surface_flags_get(),
			center,
			radius,
			&result) &&
			structure_flags)
		{
#ifdef HALO_PSP
			{
				unsigned long now = psp_microseconds();

				psp_features_walk_us += now - psp_features_mark;
				psp_features_mark = now;
				psp_features_found += result.vertex_count + result.edge_count + result.surface_count;
			}
#endif
			collision_bsp_get_features_in_sphere(
				collision_bsp,
				&result,
				NULL,
				height,
				width,
				NONE,
				features);
		}
#ifdef HALO_PSP
		{
			unsigned long now = psp_microseconds();

			psp_features_bsp_us += now - psp_features_mark;
			psp_features_mark = now;
			psp_features_leaves += result.leaf_count;
		}
#endif

		if (objects && result.leaf_count > 0)
		{
"""),
("""						if (object_mark_function(object_index))
						{
							object_get_features_in_sphere(
								flags,
								object_index,
								center,
								radius,
								height,
								width,
								ignore_object_index,
								features);
						}
					}
				}
			}
			object_marker_end();
			structure_cluster_marker_end();
		}
""",
 """						if (object_mark_function(object_index))
						{
#ifdef HALO_PSP
							psp_features_objects++;
#endif
							object_get_features_in_sphere(
								flags,
								object_index,
								center,
								radius,
								height,
								width,
								ignore_object_index,
								features);
						}
					}
				}
			}
			object_marker_end();
			structure_cluster_marker_end();
		}
#ifdef HALO_PSP
		psp_features_objects_us += psp_microseconds() - psp_features_mark;
		if (psp_features_calls >= 6000)
		{
			platform_psp_log("collision: features in sphere: %lu calls; BSP walk %lu us each (%.1f leaves), features %lu us (%.1f surfaces, edges and vertices made into shapes), objects %lu us (%.1f objects gathered: since b107, the ones past the sphere test)",
				psp_features_calls, psp_features_walk_us / psp_features_calls, (double)psp_features_leaves / psp_features_calls,
				psp_features_bsp_us / psp_features_calls, (double)psp_features_found / psp_features_calls,
				psp_features_objects_us / psp_features_calls, (double)psp_features_objects / psp_features_calls);
			psp_features_calls = psp_features_bsp_us = psp_features_walk_us = psp_features_objects_us = psp_features_leaves = psp_features_objects = psp_features_found = 0;
		}
#endif
"""),
("""boolean collision_get_features_in_sphere(
	unsigned long flags,""",
 """#ifdef HALO_PSP
/* build 45: the feature gather, timed by phase (psp_fixups.py) */
static unsigned long psp_features_calls, psp_features_bsp_us, psp_features_walk_us, psp_features_objects_us, psp_features_leaves, psp_features_objects, psp_features_found, psp_features_mark;
#endif
boolean collision_get_features_in_sphere(
	unsigned long flags,"""),
]

# build 46: how often a vehicle's slipping mass points make material effects
# (dust, sparks) - every grounded point moving past 0.03 wu/tick, every tick
FIXES_36["source/units/vehicles.c#slip"] = [
("""					material_effect_new(definition->material_effects.index,
						TEST_FLAG(mass_point_definition->flags, 0) ? 10 : 9,""",
 """#ifdef HALO_PSP
					{
						extern unsigned long psp_slip_effects; /* particles.c (psp_fixups.py) */

						psp_slip_effects++;
					}
#endif
					material_effect_new(definition->material_effects.index,
						TEST_FLAG(mass_point_definition->flags, 0) ? 10 : 9,"""),
]

FIXES_36["source/units/bipeds.c#pill"] = [
("""	{
		collision_count = collision_move_pill(
			collision_flags,
			&position,
			&velocity,
			physics->height,
			physics->width,
			physics->biped_index,
			&clipped_position,
			&clipped_velocity,
			16,
			collisions);
	}
""",
 """	{
#ifdef HALO_PSP
		/* timed, by whether the biped is moving (psp_fixups.py) */
		static unsigned long psp_pill_us[3], psp_pill_count[3], psp_pill_hits[3];
		unsigned long psp_mark = psp_microseconds();
		/* "still": little horizontal movement (gravity pulls every standing
		biped into its ground plane each tick, so the vertical part is never
		zero): 2 under 0.001 wu a tick, 1 under 0.01, 0 moving */
		float psp_speed2 = velocity.i * velocity.i + velocity.j * velocity.j;
		int psp_still = TEST_FLAG(physics->in_flags, _biped_physics_in_airborne_bit) ? 0 :
			psp_speed2 < 1e-6f ? 2 : psp_speed2 < 1e-4f ? 1 : 0;
#endif
		collision_count = collision_move_pill(
			collision_flags,
			&position,
			&velocity,
			physics->height,
			physics->width,
			physics->biped_index,
			&clipped_position,
			&clipped_velocity,
			16,
			collisions);
#ifdef HALO_PSP
		psp_pill_us[psp_still] += psp_microseconds() - psp_mark;
		psp_pill_count[psp_still]++;
		psp_pill_hits[psp_still] += collision_count;
		if (psp_pill_count[0] + psp_pill_count[1] + psp_pill_count[2] >= 3000)
		{
			platform_psp_log("bipeds: pill sweeps: moving %lu at %lu us (%.1f collisions each), slow %lu at %lu us (%.1f), still %lu at %lu us (%.1f)",
				psp_pill_count[0], psp_pill_count[0] ? psp_pill_us[0] / psp_pill_count[0] : 0,
				psp_pill_count[0] ? (double)psp_pill_hits[0] / psp_pill_count[0] : 0.0,
				psp_pill_count[1], psp_pill_count[1] ? psp_pill_us[1] / psp_pill_count[1] : 0,
				psp_pill_count[1] ? (double)psp_pill_hits[1] / psp_pill_count[1] : 0.0,
				psp_pill_count[2], psp_pill_count[2] ? psp_pill_us[2] / psp_pill_count[2] : 0,
				psp_pill_count[2] ? (double)psp_pill_hits[2] / psp_pill_count[2] : 0.0);
			memset(psp_pill_us, 0, sizeof(psp_pill_us));
			memset(psp_pill_count, 0, sizeof(psp_pill_count));
			memset(psp_pill_hits, 0, sizeof(psp_pill_hits));
		}
#endif
	}
"""),
]

FIXES_36["source/saved games/game_state.c"] = [
("""void *game_state_malloc(
	const char *name,
	const char *type,
	long size)
{
	byte *pointer;
""",
 """#ifdef HALO_PSP
void platform_psp_log(const char *format, ...);
#define PSP_STATE_RECORDS 96
/* the name is copied: some callers pass a buffer on their stack (b30 recon
showed garbage names a few seconds in) */
static struct { char name[40]; char type[12]; long size; void *pointer; short peak; } psp_state_records[PSP_STATE_RECORDS];
static int psp_state_record_count = 0;

/* the heartbeat's survey (psp_platform.c): every array and pool past 16 KB,
with the most of it ever seen in use */
void psp_game_state_report(void)
{
	char line[1024];
	int length = 0, k;
	static int layout_said = 0;

	/* build 108: once, where each allocation lies, so tools can name the
	game state's addresses (the emulator's cache model, tools/cachemap.py) */
	if (!layout_said && psp_state_record_count > 0)
	{
		layout_said = 1;
		for (k = 0; k < psp_state_record_count; k++)
			platform_psp_log("game state layout: %08lx %ld %s", (unsigned long)psp_state_records[k].pointer,
				psp_state_records[k].size, psp_state_records[k].name[0] ? psp_state_records[k].name : "?");
	}

	for (k = 0; k < psp_state_record_count; k++)
	{
		struct data_array *data = (struct data_array *)psp_state_records[k].pointer;
		struct memory_pool *pool = (struct memory_pool *)psp_state_records[k].pointer;
		int used = -1, of = -1;
		char kind = psp_state_records[k].type[0];
		const char *name = psp_state_records[k].name[0] ? psp_state_records[k].name : "?";

		if (kind == 'd' && data->valid)
		{
			if (data->actual_count > psp_state_records[k].peak)
				psp_state_records[k].peak = data->actual_count;
			used = psp_state_records[k].peak;
			of = data->maximum_count;
		}
		else if (kind == 'm')
		{
			long in_use = pool->size - pool->free_size;

			if (in_use > psp_state_records[k].peak)
				psp_state_records[k].peak = (short)(in_use / 1024);
			used = psp_state_records[k].peak;
			of = pool->size / 1024;
		}
		if (psp_state_records[k].size < 16384)
			continue;
		if (length > (int)sizeof(line) - 96)
		{
			platform_psp_log("game state:%s", line);
			length = 0;
		}
		if (used >= 0)
			length += snprintf(line + length, sizeof(line) - length, " %s %ld KB (peak %d of %d%s);", name,
				psp_state_records[k].size / 1024, used, of, kind == 'm' ? " KB" : "");
		else
			length += snprintf(line + length, sizeof(line) - length, " %s %ld KB;", name, psp_state_records[k].size / 1024);
	}
	if (length)
		platform_psp_log("game state:%s", line);
	platform_psp_log("game state: %ld KB allocated of %d KB", (game_state_globals.cpu_allocation_size + game_state_globals.gpu_allocation_size) / 1024, GAME_STATE_SIZE / 1024);
}

/* build 112: "statecrc": a hash of the whole game state after every tick
(game.c), and of each allocation every 30 ticks. With "fixedticks" (main.c)
and no input, two runs of the same stretch play the same way, so a change
meant to leave the game exactly as it was must leave every line the same;
the first tick that differs, and the allocation, say where it doesn't
(docs/big_levers.md). FNV-1a over 32-bit words: a few cycles a word */
int psp_keep(const char *name);
static unsigned long psp_state_hash(const void *start, long bytes)
{
	/* from the first whole word (an allocation can start anywhere, and an
	unaligned word load faults on the PSP) */
	unsigned long skip = (4 - ((unsigned long)start & 3)) & 3;
	const unsigned long *word = (const unsigned long *)((const char *)start + skip);
	unsigned long hash = 2166136261UL;
	long k;

	for (k = 0; k < (bytes - (long)skip) / 4; k++)
		hash = (hash ^ word[k]) * 16777619UL;
	return hash;
}

void psp_game_state_hash_tick(long tick)
{
	static int asked = -1;
	char line[1024];
	int length = 0, k;

	if (asked < 0)
		asked = psp_keep("statecrc");
	if (!asked || !game_state_globals.base_address)
		return;
	{
		extern unsigned long psp_objects_play_hash(void); /* objects.c (psp_fixups.py) */

		extern void psp_objects_play_dump(long tick); /* objects.c (psp_fixups.py, build 113) */

		/* build 113: the global and local random seeds too, so two runs
		show the tick their draws part ("fixedseed", random_math.c) */
		extern unsigned long get_random_seed(void);
		extern unsigned long *get_global_local_random_seed_address(void);

		platform_psp_log("state: tick %ld %08lx play %08lx seeds %08lx %08lx", tick,
			psp_state_hash(game_state_globals.base_address, game_state_globals.cpu_allocation_size),
			psp_objects_play_hash(), get_random_seed(), *get_global_local_random_seed_address());
		psp_objects_play_dump(tick);
	}
	{
		/* build 113: "staterecordsN": every tick's records for 48 ticks from
		tick N, to find the tick and the allocation where two runs part */
		static long records_from = -2;

		if (records_from == -2)
		{
			extern int psp_keep_number(const char *prefix, int fallback); /* psp_platform.c */

			records_from = psp_keep_number("staterecords", -1);
		}
		if (tick % 30 && !(records_from >= 0 && tick >= records_from && tick < records_from + 48))
			return;
	}
	/* (lines kept under the log's 512 bytes, psp_platform.c) */
	for (k = 0; k < psp_state_record_count; k++)
	{
		if (length > 400)
		{
			platform_psp_log("state: records at tick %ld:%s", tick, line);
			length = 0;
		}
		length += snprintf(line + length, sizeof(line) - length, " %.32s %08lx",
			psp_state_records[k].name[0] ? psp_state_records[k].name : "?",
			psp_state_hash(psp_state_records[k].pointer, psp_state_records[k].size));
	}
	if (length)
		platform_psp_log("state: records at tick %ld:%s", tick, line);
}
#endif

void *game_state_malloc(
	const char *name,
	const char *type,
	long size)
{
	byte *pointer;
"""),
("""	game_state_allocation_record(name, type, size, FALSE);

	pointer = (byte *)game_state_globals.base_address+game_state_globals.cpu_allocation_size;
	game_state_globals.cpu_allocation_size+= size;
""",
 """	game_state_allocation_record(name, type, size, FALSE);

	pointer = (byte *)game_state_globals.base_address+game_state_globals.cpu_allocation_size;
	game_state_globals.cpu_allocation_size+= size;
#ifdef HALO_PSP
	if (psp_state_record_count < PSP_STATE_RECORDS)
	{
		strncpy(psp_state_records[psp_state_record_count].name, name ? name : "?", sizeof(psp_state_records[0].name) - 1);
		strncpy(psp_state_records[psp_state_record_count].type, type ? type : "", sizeof(psp_state_records[0].type) - 1);
		psp_state_records[psp_state_record_count].size = size;
		psp_state_records[psp_state_record_count].pointer = pointer;
		psp_state_records[psp_state_record_count].peak = 0;
		psp_state_record_count++;
	}
#endif
"""),
]

# build 46: the collision code's tag block accessor inlined. Every BSP node
# the sphere and ray walks visit reads its node and plane through
# TAG_BLOCK_GET_ELEMENT, a call to tag_block_get_element_with_size and its
# five checks; in these three files it is now an inline bounds check that
# returns the same address, the out-of-range cases still going through the
# function (so a bad index is noted in debug.txt as before). Left out: the
# check that the tag definition's element size matches the structure's, a
# property of the code, not of the data. At first only in the collision
# files; since build 66 for the whole game (psp_build.py defines
# PSP_FAST_TAG_BLOCKS), so their own defines went in build 109
FIXES_36["source/tag_files/tag_groups.h#fast"] = [
("""void *tag_block_get_element_with_size(struct tag_block const *block, long index, long element_size);
""", """void *tag_block_get_element_with_size(struct tag_block const *block, long index, long element_size);
#if defined(HALO_PSP) && defined(PSP_FAST_TAG_BLOCKS)
static __inline__ void *psp_tag_block_element(struct tag_block const *block, long index, long element_size)
{
	if (__builtin_expect(!block || !block->address || (unsigned long)index >= (unsigned long)block->count, 0))
		return tag_block_get_element_with_size(block, index, element_size);
	return (char *)block->address + index * element_size;
}
#undef TAG_BLOCK_GET_ELEMENT
#define TAG_BLOCK_GET_ELEMENT(block_address, index, type) ((type *)psp_tag_block_element((block_address), (index), sizeof(type)))
#endif
"""),
]
# build 66: Halo's adaptive vertical-blank throttle off on the PSP. It paces
# frames to whole vertical-blank intervals (2 = 30 fps, 3 = 20, 4 = 15) and
# steps the interval down after repeated misses, then SLEEPS to pad every
# frame out to it: Sheepdog01's b64 run spent 3.9 ms a frame in
# main_rasterizer_throttle at 21 fps. Without it the frame runs as fast as it
# can, capped at 30 fps by the millisecond branch. "vblankthrottle": as before
# build 66: verify_tag_reference looked every reference up by NAME (tag_loaded:
# a case-insensitive string compare against every tag of the group, 450
# bitmaps for a bitmap reference) to assert that the index matched, then
# returned the lookup's result. The crosshairs called it per item per frame
# (0.33 ms a frame in the emulator, about 1 ms on the PSP). The reference's
# own index is the answer in a consistent map, which Invader's are; "verifytags"
# in keep.txt restores the lookup. tag_loaded itself is counted per frame
FIXES_36["source/tag_files/tag_groups.c#verify"] = [
Either(universal=("""	match_assert("c:\\\\halo\\\\SOURCE\\\\tag_files\\\\tag_groups.c", 3055, reference);
	index = tag_loaded(reference->group_tag, reference->name);
""",
 """	match_assert("c:\\\\halo\\\\SOURCE\\\\tag_files\\\\tag_groups.c", 3055, reference);
#ifdef HALO_PSP
	{
		extern int psp_keep(const char *name);
		static int verify = -1;

		if (verify < 0)
			verify = psp_keep("verifytags");
		if (!verify)
			return reference->index;
	}
#endif
	index = tag_loaded(reference->group_tag, reference->name);
"""),
 # ChupathingyCE
 chupa=("""	match_assert("c:\\\\halo\\\\SOURCE\\\\tag_files\\\\tag_groups.c", 3055, reference);
#ifdef HALO_CUSTOM_EDITION
""",
  """	match_assert("c:\\\\halo\\\\SOURCE\\\\tag_files\\\\tag_groups.c", 3055, reference);
#ifdef HALO_PSP
	{
		extern int psp_keep(const char *name);
		static int verify = -1;

		if (verify < 0)
			verify = psp_keep("verifytags");
		if (!verify)
			return reference->index;
	}
#endif
#ifdef HALO_CUSTOM_EDITION
""")),
]
FIXES_36["source/cache/cache_files.c#tagloaded"] = [
Either(universal=("""	short absolute_index;
	long result = NONE;

	if (cache_file_globals.tags_loaded)
	{
		match_assert(
			"c:\\\\halo\\\\SOURCE\\\\cache\\\\cache_files.c",
			346,
			global_tag_instances);
""",
 """	short absolute_index;
	long result = NONE;

#ifdef HALO_PSP
	{
		extern unsigned long psp_tag_loaded_calls;

		psp_tag_loaded_calls++;
	}
#endif
	if (cache_file_globals.tags_loaded)
	{
		match_assert(
			"c:\\\\halo\\\\SOURCE\\\\cache\\\\cache_files.c",
			346,
			global_tag_instances);
"""),
 # ChupathingyCE
 chupa=("""	long absolute_index;
	long result = NONE;

	if (cache_file_globals.tags_loaded)
	{""",
  """	long absolute_index;
	long result = NONE;

#ifdef HALO_PSP
	{
		extern unsigned long psp_tag_loaded_calls;

		psp_tag_loaded_calls++;
	}
#endif
	if (cache_file_globals.tags_loaded)
	{""")),
]

FIXES_36["source/main/main.c#throttle"] = [
("""static boolean main_framerate_throttle_enabled(
	void)
{
	return rasterizer_globals.framerate_throttle;
}""",
 """static boolean main_framerate_throttle_enabled(
	void)
{
#ifdef HALO_PSP
	{
		extern int psp_keep(const char *name);
		static int vblank_throttle = -1;

		/* build 90: on by default (Sheepdog01's b87 run 3: with it the warthog's
		rubber banding was almost gone; with interpolation it was worse).
		"novblankthrottle" turns it off, and benchrun runs without it so its
		figures stay comparable with earlier runs'; build 112: ceilingbench
		too (padding to vertical blanks would hide what a switch saves) */
		if (vblank_throttle < 0)
			vblank_throttle = psp_keep("vblankthrottle") || (!psp_keep("novblankthrottle") && !psp_keep("benchrun") && !psp_keep("ceilingbench"));
		if (!vblank_throttle)
			return FALSE;
	}
#endif
	return rasterizer_globals.framerate_throttle;
}"""),
]


FIXES_36["source/effects/particles.c"] = [
("""void particle_new(
	struct new_particle_data const *data)
{""",
 """#ifdef HALO_PSP
/* build 38: the budget for particles in flight (128 refused smoke, debris
and explosion parts with the sparks, Sheepdog01's "missing effects"), and how many
of them run their physics each frame: each costs about 70 us (the wind where
it is and a collision ray); past the budget they take turns, every second
or third frame with the time they missed (the particle's own clock and
animation still run every frame) */
#define PSP_PARTICLE_BUDGET 256
#define PSP_PARTICLE_PHYSICS_BUDGET 40
static float psp_particle_dt[HALO_PORT_MAXIMUM_PARTICLES];
/* build 39: a new particle runs its physics every frame for its first few
(the spark off a pistol hit is gone in a quarter second: waiting a turn in
its first frames left it hanging where it was born, Sheepdog01's "mistimed" shots).
Only older ones take turns */
#define PSP_PARTICLE_YOUNG_FRAMES 6
static unsigned char psp_particle_age[HALO_PORT_MAXIMUM_PARTICLES];
static unsigned long psp_particle_frame = 0, psp_particles_moving_last = 0, psp_particle_turns_skipped = 0;
int psp_keep(const char *name); /* psp_platform.c; psp_fixups.py */
unsigned long psp_microseconds(void);
void platform_psp_log(const char *format, ...);
static int psp_particle_budget = -1;
static long psp_free_particles = 0;
static unsigned long psp_particles_thinned = 0, psp_particles_refused = 0, psp_particle_updates = 0, psp_particle_physics = 0;
static unsigned long psp_particle_us = 0, psp_particles_peak = 0, psp_particles_live_total = 0;
/* build 46: which particles cost the time (Sheepdog01: the sparks off a vehicle's
scrapes look like the assault rifle's): made and physics time per
particle definition, the top ones logged with the particles line */
#define PSP_PARTICLE_KINDS 32
static struct { long definition_index; unsigned long made, physics, us; } psp_particle_kinds[PSP_PARTICLE_KINDS];
unsigned long psp_slip_effects = 0; /* vehicles.c's create_slipping_effects (psp_fixups.py) */
char *tag_get_name(long tag_index);
static int psp_particle_kind(long definition_index)
{
	unsigned int k = ((unsigned long)definition_index * 2654435761u) >> 27, n;

	for (n = 0; n < PSP_PARTICLE_KINDS; n++, k = (k + 1) % PSP_PARTICLE_KINDS)
	{
		if (psp_particle_kinds[k].definition_index == definition_index)
			return k;
		if (!psp_particle_kinds[k].made && !psp_particle_kinds[k].physics)
		{
			psp_particle_kinds[k].definition_index = definition_index;
			return k;
		}
	}
	return -1;
}
#endif

void particle_new(
	struct new_particle_data const *data)
{"""),
("""	particle_index = datum_new(particle_data);

	if (particle_index == NONE)
		return;

	particle = particle_get(particle_index);

	particle->flags = 0;""",
 """#ifdef HALO_PSP
	if (data->object_index == NONE)
	{
		if (psp_particle_budget < 0)
			psp_particle_budget = psp_keep("fullparticles") ? 0x7FFF : PSP_PARTICLE_BUDGET;
		if (psp_free_particles >= psp_particle_budget)
		{
			psp_particles_refused++;
			return;
		}
		/* build 41: past 96 in flight, every second new one is left out,
		past 160 two of three: a big burst (the tank shell's) comes out
		thinner but on time, where the budget alone took it whole and then
		refused everything after (Sheepdog01: dense, and a big cut in frames) */
		if (psp_particle_budget != 0x7FFF && psp_free_particles >= 96)
		{
			static unsigned long psp_thin = 0;

			if (++psp_thin % (psp_free_particles >= 160 ? 3 : 2))
			{
				psp_particles_thinned++;
				return;
			}
		}
	}
#endif
	particle_index = datum_new(particle_data);

	if (particle_index == NONE)
		return;
#ifdef HALO_PSP
	/* counted at once, so one frame's burst meets the budget too (the
	count was only last frame's) */
	if (data->object_index == NONE)
		psp_free_particles++;
	{
		int psp_kind = psp_particle_kind(data->definition_index);

		if (psp_kind >= 0)
			psp_particle_kinds[psp_kind].made++;
	}
	if ((particle_index & 0xFFFF) < HALO_PORT_MAXIMUM_PARTICLES)
	{
		psp_particle_dt[particle_index & 0xFFFF] = 0.0f;
		psp_particle_age[particle_index & 0xFFFF] = 0;
	}
#endif

	particle = particle_get(particle_index);

	particle->flags = 0;"""),
("""void particles_update(
	real dt)
{
	long particle_index;

	profile_enter(particles_update_section);
""",
 """void particles_update(
	real dt)
{
	long particle_index;
#ifdef HALO_PSP
	unsigned long psp_mark = psp_microseconds();
	long psp_free = 0, psp_live = 0, psp_moving = 0;
	/* every k-th frame for each particle, k from last frame's count */
	unsigned long psp_turn = psp_particle_budget == 0x7FFF ? 1 :
		(psp_particles_moving_last + PSP_PARTICLE_PHYSICS_BUDGET - 1) / PSP_PARTICLE_PHYSICS_BUDGET;

	if (psp_turn < 1)
		psp_turn = 1;
	if (psp_turn > 3)
		psp_turn = 3;
	psp_particle_frame++;
#endif

	profile_enter(particles_update_section);
"""),
("""			else
			{
				if (particle_update_frame_time(particle_index, dt))
					particle_update_physics(particle_index, dt);
			}
		}
	}

	profile_exit(particles_update_section);
""",
 """			else
			{
#ifdef HALO_PSP
				real psp_dt = dt;
				int psp_waits = 0;
				long psp_slot = particle_index & 0xFFFF;

				psp_live++;
				if (particle->object_index == NONE)
				{
					psp_free++;
					if (!TEST_FLAG(particle->flags, _particle_datum_at_rest_bit))
					{
						psp_moving++;
						if (psp_slot < HALO_PORT_MAXIMUM_PARTICLES)
						{
							if (psp_particle_age[psp_slot] < PSP_PARTICLE_YOUNG_FRAMES)
								psp_particle_age[psp_slot]++;
							if (psp_turn > 1 && psp_particle_age[psp_slot] >= PSP_PARTICLE_YOUNG_FRAMES &&
								(psp_slot + psp_particle_frame) % psp_turn)
							{
								psp_particle_dt[psp_slot] += dt;
								psp_waits = 1;
								psp_particle_turns_skipped++;
							}
							else
							{
								psp_dt = dt + psp_particle_dt[psp_slot];
								psp_particle_dt[psp_slot] = 0.0f;
							}
						}
						if (!psp_waits)
							psp_particle_physics++;
					}
					else if (psp_slot < HALO_PORT_MAXIMUM_PARTICLES)
						psp_particle_dt[psp_slot] = 0.0f;
				}
				if (particle_update_frame_time(particle_index, dt) && !psp_waits)
				{
					long psp_definition = particle->definition_index;
					unsigned long psp_t0 = psp_microseconds();
					int psp_kind;

					particle_update_physics(particle_index, psp_dt);
					psp_kind = psp_particle_kind(psp_definition);
					if (psp_kind >= 0)
					{
						psp_particle_kinds[psp_kind].physics++;
						psp_particle_kinds[psp_kind].us += psp_microseconds() - psp_t0;
					}
				}
#else
				if (particle_update_frame_time(particle_index, dt))
					particle_update_physics(particle_index, dt);
#endif
			}
		}
	}

#ifdef HALO_PSP
	psp_free_particles = psp_free;
	psp_particles_moving_last = psp_moving;
	psp_particles_live_total += psp_live;
	if ((unsigned long)psp_live > psp_particles_peak)
		psp_particles_peak = psp_live;
	psp_particle_us += psp_microseconds() - psp_mark;
	/* build 46: every 30 s of real time, averaged over the calls made (the
	900-call count reported far less often than expected on hardware) */
	psp_particle_updates++;
	{
		static unsigned long psp_report_mark = 0;
		unsigned long psp_now = psp_microseconds();

		if (!psp_report_mark)
			psp_report_mark = psp_now;
		if (psp_now - psp_report_mark >= 30000000UL)
		{
			unsigned long calls = psp_particle_updates ? psp_particle_updates : 1;

			psp_report_mark = psp_now;
			platform_psp_log("particles: %lu alive on average (peak %lu), %lu physics updates a frame (%lu turns waited), %lu us a frame updating over %lu frames; %lu not made over the budget of %d free, %lu left out thinning bursts",
				psp_particles_live_total / calls, psp_particles_peak, psp_particle_physics / calls, psp_particle_turns_skipped / calls,
				psp_particle_us / calls, psp_particle_updates, psp_particles_refused, psp_particle_budget, psp_particles_thinned);
			psp_particle_updates = 0;
		psp_particles_live_total = psp_particles_peak = psp_particle_physics = psp_particle_us = 0;
		psp_particles_refused = psp_particle_turns_skipped = psp_particles_thinned = 0;
		{
			char line[768];
			int length, shown, k;

			length = snprintf(line, sizeof(line), "particles by kind over 30 s (made, physics updates, us each): vehicle slip effects %lu;", psp_slip_effects);
			psp_slip_effects = 0;
			for (shown = 0; shown < 6; shown++)
			{
				int best = -1;

				for (k = 0; k < PSP_PARTICLE_KINDS; k++)
					if ((psp_particle_kinds[k].made || psp_particle_kinds[k].physics) &&
						(best < 0 || psp_particle_kinds[k].us > psp_particle_kinds[best].us))
						best = k;
				if (best < 0 || length > (int)sizeof(line) - 128)
					break;
				{
					const char *name = tag_get_name(psp_particle_kinds[best].definition_index);
					const char *slash = name ? strrchr(name, '\\\\') : NULL;

					length += snprintf(line + length, sizeof(line) - length, " %s %lu, %lu, %lu;",
						slash ? slash + 1 : name ? name : "?", psp_particle_kinds[best].made, psp_particle_kinds[best].physics,
						psp_particle_kinds[best].physics ? psp_particle_kinds[best].us / psp_particle_kinds[best].physics : 0);
				}
				psp_particle_kinds[best].made = psp_particle_kinds[best].physics = 0;
				psp_particle_kinds[best].us = 0;
			}
			platform_psp_log("%s", line);
			memset(psp_particle_kinds, 0, sizeof(psp_particle_kinds));
		}
		}
	}
#endif
	profile_exit(particles_update_section);
"""),
]

# build 68: object lighting reads three of a material's vertices for their
# texture coordinates. In a map built with its geometry outside the tags
# (invader-build -G) the material's vertex data pointer is 0x0C000000 + an
# offset in the geometry pack; the renderer's copies of the material
# (psp_ge.c's level streams, keyed by that address) hold the coordinates
FIXES_36["source/objects/object_lights.c#geometry"] = [
("""void sample_lightmap(
	struct structure_material *material,
	struct bitmap_data *bitmap,
	struct structure_surface const *surface,
	real s,
	real t,
	real_rgb_color *color)
{
	real_point2d texcoords[3];
	real_point2d texcoord;
""",
"""#ifdef HALO_PSP
extern long psp_structure_vertex_stride; /* psp_geo.c; psp_fixups.py */
int psp_ge_level_vertex_texcoords(unsigned long key, int vertex_index, float *uv, float *lightmap_uv); /* psp_ge.c */
unsigned long psp_object_light_samples_missed = 0;

/* the three vertices' coordinates from the renderer's copies when the data
is in the geometry pack: 1 if they are there, 0 if not this frame */
static int psp_material_texcoords(struct structure_material const *material, struct structure_surface const *surface,
	real_point2d *texcoords, int lightmap)
{
	int k;

	for (k = 0; k < 3; k++)
	{
		float uv[2], lightmap_uv[2];

		if (!psp_ge_level_vertex_texcoords(((unsigned long const *)material->vertices.hardware_format)[1],
			surface->vertex_indices[k], uv, lightmap_uv))
		{
			psp_object_light_samples_missed++;
			return 0;
		}
		texcoords[k].x = lightmap ? lightmap_uv[0] : uv[0];
		texcoords[k].y = lightmap ? lightmap_uv[1] : uv[1];
	}
	return 1;
}

/* build 89: the three vertices' normals (radiosity 0) or lightmap incident
radiosity (1) from the renderer's copies when the data is in the geometry
pack. lights_distant_lighting_at_point read them with the Xbox macros, at a
32-byte stride from the 6-byte position copy -G maps keep in the tags: every
object's distant light direction, shadow vector and radiosity accuracy came
from garbage, and the reads ran past the copy (code review). Straight up
when the copies aren't there this frame */
int psp_ge_level_vertex_lighting(unsigned int key, int vertex_index, unsigned int *normal, unsigned int *radiosity); /* psp_ge.c */
static void psp_material_normals(struct structure_material const *material, struct structure_surface const *surface,
	real_vector3d *normals, int radiosity)
{
	int k;

	for (k = 0; k < 3; k++)
	{
		unsigned int normal, incident;

		if (psp_ge_level_vertex_lighting(((unsigned long const *)material->vertices.hardware_format)[1],
			surface->vertex_indices[k], &normal, &incident))
			normals[k] = uncompress_int32_to_real_vector3d(radiosity ? incident : normal);
		else
		{
			normals[k].i = 0.0f;
			normals[k].j = 0.0f;
			normals[k].k = 1.0f;
			psp_object_light_samples_missed++;
		}
	}
}
#endif

void sample_lightmap(
	struct structure_material *material,
	struct bitmap_data *bitmap,
	struct structure_surface const *surface,
	real s,
	real t,
	real_rgb_color *color)
{
	real_point2d texcoords[3];
	real_point2d texcoord;
"""),
("""	environment_lightmap_vertex_compressed_get_texcoord(
		structure_material_get_lightmap_vertex(material, surface->vertex_indices[0]),
		&texcoords[0]);
	environment_lightmap_vertex_compressed_get_texcoord(
		structure_material_get_lightmap_vertex(material, surface->vertex_indices[1]),
		&texcoords[1]);
	environment_lightmap_vertex_compressed_get_texcoord(
		structure_material_get_lightmap_vertex(material, surface->vertex_indices[2]),
		&texcoords[2]);
""",
"""#ifdef HALO_PSP
	if (psp_structure_vertex_stride == 6 && material->vertices.hardware_format)
	{
		if (!psp_material_texcoords(material, surface, texcoords, 1))
		{
			/* as a bitmap the texture cache never loaded answers (bitmap_2d_get_pixel: NONE) */
			color->red = color->green = color->blue = 1.0f;
			return;
		}
	}
	else
	{
#endif
	environment_lightmap_vertex_compressed_get_texcoord(
		structure_material_get_lightmap_vertex(material, surface->vertex_indices[0]),
		&texcoords[0]);
	environment_lightmap_vertex_compressed_get_texcoord(
		structure_material_get_lightmap_vertex(material, surface->vertex_indices[1]),
		&texcoords[1]);
	environment_lightmap_vertex_compressed_get_texcoord(
		structure_material_get_lightmap_vertex(material, surface->vertex_indices[2]),
		&texcoords[2]);
#ifdef HALO_PSP
	}
#endif
"""),
("""	environment_vertex_compressed_get_texcoord(
		structure_material_get_vertex(material, surface->vertex_indices[0]),
		&texcoords[0]);
	environment_vertex_compressed_get_texcoord(
		structure_material_get_vertex(material, surface->vertex_indices[1]),
		&texcoords[1]);
	environment_vertex_compressed_get_texcoord(
		structure_material_get_vertex(material, surface->vertex_indices[2]),
		&texcoords[2]);
""",
"""#ifdef HALO_PSP
	if (psp_structure_vertex_stride == 6 && material->vertices.hardware_format)
	{
		if (!psp_material_texcoords(material, surface, texcoords, 0))
		{
			color->red = color->green = color->blue = 1.0f;
			return;
		}
	}
	else
	{
#endif
	environment_vertex_compressed_get_texcoord(
		structure_material_get_vertex(material, surface->vertex_indices[0]),
		&texcoords[0]);
	environment_vertex_compressed_get_texcoord(
		structure_material_get_vertex(material, surface->vertex_indices[1]),
		&texcoords[1]);
	environment_vertex_compressed_get_texcoord(
		structure_material_get_vertex(material, surface->vertex_indices[2]),
		&texcoords[2]);
#ifdef HALO_PSP
	}
#endif
"""),
("""						environment_vertex_compressed_get_normal(
							structure_material_get_vertex(material, surface->vertex_indices[0]),
							&normals[0]);
						environment_vertex_compressed_get_normal(
							structure_material_get_vertex(material, surface->vertex_indices[1]),
							&normals[1]);
						environment_vertex_compressed_get_normal(
							structure_material_get_vertex(material, surface->vertex_indices[2]),
							&normals[2]);
""",
"""#ifdef HALO_PSP
						if (psp_structure_vertex_stride == 6 && material->vertices.hardware_format)
							psp_material_normals(material, surface, normals, 0);
						else
						{
#endif
						environment_vertex_compressed_get_normal(
							structure_material_get_vertex(material, surface->vertex_indices[0]),
							&normals[0]);
						environment_vertex_compressed_get_normal(
							structure_material_get_vertex(material, surface->vertex_indices[1]),
							&normals[1]);
						environment_vertex_compressed_get_normal(
							structure_material_get_vertex(material, surface->vertex_indices[2]),
							&normals[2]);
#ifdef HALO_PSP
						}
#endif
"""),
("""						environment_lightmap_vertex_compressed_get_incident_radiosity(
							structure_material_get_lightmap_vertex(material, surface->vertex_indices[0]),
							&normals[0]);
						environment_lightmap_vertex_compressed_get_incident_radiosity(
							structure_material_get_lightmap_vertex(material, surface->vertex_indices[1]),
							&normals[1]);
						environment_lightmap_vertex_compressed_get_incident_radiosity(
							structure_material_get_lightmap_vertex(material, surface->vertex_indices[2]),
							&normals[2]);
""",
"""#ifdef HALO_PSP
						if (psp_structure_vertex_stride == 6 && material->vertices.hardware_format)
							psp_material_normals(material, surface, normals, 1);
						else
						{
#endif
						environment_lightmap_vertex_compressed_get_incident_radiosity(
							structure_material_get_lightmap_vertex(material, surface->vertex_indices[0]),
							&normals[0]);
						environment_lightmap_vertex_compressed_get_incident_radiosity(
							structure_material_get_lightmap_vertex(material, surface->vertex_indices[1]),
							&normals[1]);
						environment_lightmap_vertex_compressed_get_incident_radiosity(
							structure_material_get_lightmap_vertex(material, surface->vertex_indices[2]),
							&normals[2]);
#ifdef HALO_PSP
						}
#endif
"""),
]

# build 102: object lighting's two samples of the level under an object, the
# lightmap's colour and the surface texture's, from the texture pack
# (psp_tex.c's psp_tex_light_texel): the game's bitmaps have no pixels here,
# so bitmap_2d_get_pixel answered "none", white, and every object was lit as
# if it stood in full light (Sheepdog01, b101). "lightwhite" in keep.txt: as before
FIXES_36["source/objects/object_lights.c#lightmapsample"] = [
("""	pixel32_to_real_rgb_color(
		bitmap_2d_get_pixel(bitmap, &texcoord, 1.0f),
		color);
""",
"""#ifdef HALO_PSP
	{
		extern int psp_tex_light_texel(unsigned int key, float u, float v, unsigned int *argb);
		extern int psp_keep(const char *name);
		static int psp_light_white = -1;
		unsigned int psp_argb;

		if (psp_light_white < 0)
			psp_light_white = psp_keep("lightwhite");
		if (!psp_light_white && psp_tex_light_texel((unsigned int)bitmap, texcoord.x, texcoord.y, &psp_argb))
		{
			pixel32_to_real_rgb_color((pixel32)psp_argb, color);
			return;
		}
	}
#endif
	pixel32_to_real_rgb_color(
		bitmap_2d_get_pixel(bitmap, &texcoord, 1.0f),
		color);
"""),
("""	pixel32_to_real_rgb_color(
		bitmap_2d_get_pixel(bitmap, &texcoord, 0.3f),
		color);
""",
"""#ifdef HALO_PSP
	{
		extern int psp_tex_light_average(unsigned int key, unsigned int *argb);
		extern int psp_keep(const char *name);
		static int psp_light_white = -1;
		unsigned int psp_argb;

		if (psp_light_white < 0)
			psp_light_white = psp_keep("lightwhite");
		if (!psp_light_white && psp_tex_light_average((unsigned int)bitmap, &psp_argb))
		{
			pixel32_to_real_rgb_color((pixel32)psp_argb, color);
			return;
		}
	}
#endif
	pixel32_to_real_rgb_color(
		bitmap_2d_get_pixel(bitmap, &texcoord, 0.3f),
		color);
"""),
]

# build 68: the game's own readers of a material's vertices want positions
# (the per-surface frustum test in the visibility walk, the surface under a
# point). In a map built with its geometry outside the tags, the material's
# compressed vertex data is a 12-byte-a-vertex copy of the positions alone
# (invader-build -G); psp_geo_map_opened() tells which layout the map has
# build 68: in a map built with its geometry outside the tags (invader-build
# -G), the game's own copies of level data are packed: a material's vertex
# positions are three int16 relative to the material's centroid times a scale
# kept in the (unused) uncompressed vertex data's size field; leaf surface
# references are two u16 (node 0xFFFF for none); the clusters' and
# subclusters' surface index lists are u16. psp_geo_map_opened() sets
# psp_structure_vertex_stride to 6 for such a map, 32 for an old one, from the
# first model part's descriptor; these readers branch on it
PSP_BSP_READERS = """
#ifdef HALO_PSP
extern long psp_structure_vertex_stride; /* psp_geo.c: 6 packed, 32 as the Xbox had it (psp_fixups.py) */
#define PSP_BSP_PACKED (psp_structure_vertex_stride == 6)

/* a material's vertex position, from either layout */
static void psp_material_vertex_point(struct structure_material const *material, long vertex_index, real_point3d *point)
{
	if (PSP_BSP_PACKED)
	{
		short const *q = (short const *)material->compressed_vertex_data.address + 3 * vertex_index;
		float scale;

		csmemcpy(&scale, &material->uncompressed_vertex_data.size, sizeof(scale));
		point->x = material->centroid.x + q[0] * scale;
		point->y = material->centroid.y + q[1] * scale;
		point->z = material->centroid.z + q[2] * scale;
	}
	else
	{
		csmemcpy(point, (byte const *)material->compressed_vertex_data.address + 32 * vertex_index, sizeof(*point));
	}
}

/* a leaf's surface reference, from either layout */
static void psp_surface_reference(struct tag_block const *block, long index, long *surface_index, long *node_index)
{
	if (PSP_BSP_PACKED)
	{
		unsigned short const *p = (unsigned short const *)block->address + 2 * index;

		*surface_index = p[0];
		*node_index = p[1] == 0xFFFF ? NONE : (long)p[1];
	}
	else
	{
		long const *p = (long const *)block->address + 2 * index;

		*surface_index = p[0];
		*node_index = p[1];
	}
}

/* the k-th entry of a cluster's or subcluster's surface index list */
#define PSP_SURFACE_INDEX(block, k) (PSP_BSP_PACKED ? (long)((unsigned short const *)(block)->address)[k] : ((long const *)(block)->address)[k])
#endif
"""

FIXES_36["source/structures/structure_visibility.c#packed"] = [
Either(universal=("""					real_point3d const *vertex0 = (real_point3d const *)(
						(byte *)material->compressed_vertex_data.address +
						surface->vertex_indices[0] * COMPRESSED_STRUCTURE_VERTEX_SIZE);
					real_point3d const *vertex1 = (real_point3d const *)(
						(byte *)material->compressed_vertex_data.address +
						surface->vertex_indices[1] * COMPRESSED_STRUCTURE_VERTEX_SIZE);
					real_point3d const *vertex2 = (real_point3d const *)(
						(byte *)material->compressed_vertex_data.address +
						surface->vertex_indices[2] * COMPRESSED_STRUCTURE_VERTEX_SIZE);

					if (render_frustum_triangle_visible(
						frustum,
						vertex0,
						vertex1,
						vertex2))
""",
"""#ifdef HALO_PSP
					real_point3d points[3];
					real_point3d const *vertex0 = &points[0];
					real_point3d const *vertex1 = &points[1];
					real_point3d const *vertex2 = &points[2];

					psp_material_vertex_point(material, surface->vertex_indices[0], &points[0]);
					psp_material_vertex_point(material, surface->vertex_indices[1], &points[1]);
					psp_material_vertex_point(material, surface->vertex_indices[2], &points[2]);
#else
					real_point3d const *vertex0 = (real_point3d const *)(
						(byte *)material->compressed_vertex_data.address +
						surface->vertex_indices[0] * COMPRESSED_STRUCTURE_VERTEX_SIZE);
					real_point3d const *vertex1 = (real_point3d const *)(
						(byte *)material->compressed_vertex_data.address +
						surface->vertex_indices[1] * COMPRESSED_STRUCTURE_VERTEX_SIZE);
					real_point3d const *vertex2 = (real_point3d const *)(
						(byte *)material->compressed_vertex_data.address +
						surface->vertex_indices[2] * COMPRESSED_STRUCTURE_VERTEX_SIZE);
#endif

					if (render_frustum_triangle_visible(
						frustum,
						vertex0,
						vertex1,
						vertex2))
"""),
 # ChupathingyCE
 chupa=("""					real_point3d const *vertex0 = (real_point3d const *)(
						(byte *)xbox_pointer(material->compressed_vertex_data.address) +
						surface->vertex_indices[0] * COMPRESSED_STRUCTURE_VERTEX_SIZE);
					real_point3d const *vertex1 = (real_point3d const *)(
						(byte *)xbox_pointer(material->compressed_vertex_data.address) +
						surface->vertex_indices[1] * COMPRESSED_STRUCTURE_VERTEX_SIZE);
					real_point3d const *vertex2 = (real_point3d const *)(
						(byte *)xbox_pointer(material->compressed_vertex_data.address) +
						surface->vertex_indices[2] * COMPRESSED_STRUCTURE_VERTEX_SIZE);
""",
  """#ifdef HALO_PSP
					real_point3d points[3];
					real_point3d const *vertex0 = &points[0];
					real_point3d const *vertex1 = &points[1];
					real_point3d const *vertex2 = &points[2];

					psp_material_vertex_point(material, surface->vertex_indices[0], &points[0]);
					psp_material_vertex_point(material, surface->vertex_indices[1], &points[1]);
					psp_material_vertex_point(material, surface->vertex_indices[2], &points[2]);
#else
					real_point3d const *vertex0 = (real_point3d const *)(
						(byte *)xbox_pointer(material->compressed_vertex_data.address) +
						surface->vertex_indices[0] * COMPRESSED_STRUCTURE_VERTEX_SIZE);
					real_point3d const *vertex1 = (real_point3d const *)(
						(byte *)xbox_pointer(material->compressed_vertex_data.address) +
						surface->vertex_indices[1] * COMPRESSED_STRUCTURE_VERTEX_SIZE);
					real_point3d const *vertex2 = (real_point3d const *)(
						(byte *)xbox_pointer(material->compressed_vertex_data.address) +
						surface->vertex_indices[2] * COMPRESSED_STRUCTURE_VERTEX_SIZE);
#endif
""")),
# the cluster's list, consumed per material
Either(universal=("""				surface_index = *surface_index_buffer++;
				match_assert(
					"c:\\\\halo\\\\SOURCE\\\\structures\\\\structure_visibility.c",
					0x1A0,
					surface_index_buffer-(long *) cluster->surface_indices.address<=cluster->surface_indices.count);
""",
"""#ifdef HALO_PSP
				surface_index = PSP_SURFACE_INDEX(&cluster->surface_indices, consumed_surface_index_count);
				(void)surface_index_buffer;
#else
				surface_index = *surface_index_buffer++;
				match_assert(
					"c:\\\\halo\\\\SOURCE\\\\structures\\\\structure_visibility.c",
					0x1A0,
					surface_index_buffer-(long *) cluster->surface_indices.address<=cluster->surface_indices.count);
#endif
"""),
 # ChupathingyCE
 chupa=("""				surface_index = *surface_index_buffer++;
#ifdef HALO_64BIT
				match_assert(
					"c:\\\\halo\\\\SOURCE\\\\structures\\\\structure_visibility.c",
					0x1A0,
					surface_index_buffer-(long *) xbox_pointer(cluster->surface_indices.address)<=cluster->surface_indices.count);
#else
				match_assert(
					"c:\\\\halo\\\\SOURCE\\\\structures\\\\structure_visibility.c",
					0x1A0,
					surface_index_buffer-(long *) cluster->surface_indices.address<=cluster->surface_indices.count);
#endif
""",
  """#ifdef HALO_PSP
				surface_index = PSP_SURFACE_INDEX(&cluster->surface_indices, consumed_surface_index_count);
				(void)surface_index_buffer;
#else
				surface_index = *surface_index_buffer++;
#ifdef HALO_64BIT
				match_assert(
					"c:\\\\halo\\\\SOURCE\\\\structures\\\\structure_visibility.c",
					0x1A0,
					surface_index_buffer-(long *) xbox_pointer(cluster->surface_indices.address)<=cluster->surface_indices.count);
#else
				match_assert(
					"c:\\\\halo\\\\SOURCE\\\\structures\\\\structure_visibility.c",
					0x1A0,
					surface_index_buffer-(long *) cluster->surface_indices.address<=cluster->surface_indices.count);
#endif
#endif
""")),
# ChupathingyCE only: its vertex count check, in the packed layout's vertex size
Either(chupa=("""			vertex_limit = material->compressed_vertex_data.size / COMPRESSED_STRUCTURE_VERTEX_SIZE;
""",
  """#ifdef HALO_PSP
			/* psp_fixups.py: a packed map's vertices are 6 bytes (PSP_BSP_PACKED) */
			vertex_limit = material->compressed_vertex_data.size / (PSP_BSP_PACKED ? 6 : COMPRESSED_STRUCTURE_VERTEX_SIZE);
#else
			vertex_limit = material->compressed_vertex_data.size / COMPRESSED_STRUCTURE_VERTEX_SIZE;
#endif
""")),
# the subclusters' lists: the frustum pass
Either(universal=("""			surface_index_buffer = TAG_BLOCK_GET_ELEMENT(&subcluster->surface_indices, 0, long);
			for (surface_list_index = 0;
				(short)surface_list_index < subcluster->surface_indices.count;
				surface_index_buffer++, surface_list_index++)
			{
				if (!BIT_VECTOR_TEST_FLAG(render.environment_surface_flags, *surface_index_buffer))
				{
					if (render.environment_surface_count >= MAXIMUM_RENDERED_ENVIRONMENT_SURFACES)
					{
						break;
					}

					BIT_VECTOR_SET_FLAG(render.environment_surface_flags, *surface_index_buffer, TRUE);
""",
"""#ifdef HALO_PSP
			surface_index_buffer = NULL;
			for (surface_list_index = 0;
				(short)surface_list_index < subcluster->surface_indices.count;
				surface_list_index++)
			{
				long psp_surface_index = PSP_SURFACE_INDEX(&subcluster->surface_indices, surface_list_index);

				if (!BIT_VECTOR_TEST_FLAG(render.environment_surface_flags, psp_surface_index))
				{
					if (render.environment_surface_count >= MAXIMUM_RENDERED_ENVIRONMENT_SURFACES)
					{
						break;
					}

					BIT_VECTOR_SET_FLAG(render.environment_surface_flags, psp_surface_index, TRUE);
#else
			surface_index_buffer = TAG_BLOCK_GET_ELEMENT(&subcluster->surface_indices, 0, long);
			for (surface_list_index = 0;
				(short)surface_list_index < subcluster->surface_indices.count;
				surface_index_buffer++, surface_list_index++)
			{
				if (!BIT_VECTOR_TEST_FLAG(render.environment_surface_flags, *surface_index_buffer))
				{
					if (render.environment_surface_count >= MAXIMUM_RENDERED_ENVIRONMENT_SURFACES)
					{
						break;
					}

					BIT_VECTOR_SET_FLAG(render.environment_surface_flags, *surface_index_buffer, TRUE);
#endif
"""),
 # ChupathingyCE
 chupa=("""			surface_index_buffer = TAG_BLOCK_GET_ELEMENT(&subcluster->surface_indices, 0, long);
			for (surface_list_index = 0;
				(short)surface_list_index < subcluster->surface_indices.count;
				surface_index_buffer++, surface_list_index++)
			{
				/* port: a surface index (from the map) that is no surface is
				skipped (the bsp's surfaces fit the flags, as
				structure_bsp_port_verify checked) */
				if ((unsigned long)*surface_index_buffer >= (unsigned long)structure->surfaces.count)
				{
					continue;
				}

				if (!BIT_VECTOR_TEST_FLAG(render.environment_surface_flags, *surface_index_buffer))
				{
					if (render.environment_surface_count >= MAXIMUM_RENDERED_ENVIRONMENT_SURFACES)
					{
						break;
					}

					BIT_VECTOR_SET_FLAG(render.environment_surface_flags, *surface_index_buffer, TRUE);
""",
  """#ifdef HALO_PSP
			surface_index_buffer = NULL;
			for (surface_list_index = 0;
				(short)surface_list_index < subcluster->surface_indices.count;
				surface_list_index++)
			{
				long psp_surface_index = PSP_SURFACE_INDEX(&subcluster->surface_indices, surface_list_index);

				/* port: a surface index (from the map) that is no surface is
				skipped (the bsp's surfaces fit the flags, as
				structure_bsp_port_verify checked) */
				if ((unsigned long)psp_surface_index >= (unsigned long)structure->surfaces.count)
				{
					continue;
				}

				if (!BIT_VECTOR_TEST_FLAG(render.environment_surface_flags, psp_surface_index))
				{
					if (render.environment_surface_count >= MAXIMUM_RENDERED_ENVIRONMENT_SURFACES)
					{
						break;
					}

					BIT_VECTOR_SET_FLAG(render.environment_surface_flags, psp_surface_index, TRUE);
#else
			surface_index_buffer = TAG_BLOCK_GET_ELEMENT(&subcluster->surface_indices, 0, long);
			for (surface_list_index = 0;
				(short)surface_list_index < subcluster->surface_indices.count;
				surface_index_buffer++, surface_list_index++)
			{
				/* port: a surface index (from the map) that is no surface is
				skipped (the bsp's surfaces fit the flags, as
				structure_bsp_port_verify checked) */
				if ((unsigned long)*surface_index_buffer >= (unsigned long)structure->surfaces.count)
				{
					continue;
				}

				if (!BIT_VECTOR_TEST_FLAG(render.environment_surface_flags, *surface_index_buffer))
				{
					if (render.environment_surface_count >= MAXIMUM_RENDERED_ENVIRONMENT_SURFACES)
					{
						break;
					}

					BIT_VECTOR_SET_FLAG(render.environment_surface_flags, *surface_index_buffer, TRUE);
#endif
""")),
# the subclusters' lists: the bounded-surfaces pass
Either(universal=("""				long *surface_index_buffer = TAG_BLOCK_GET_ELEMENT(&subcluster->surface_indices, 0, long);
				long surface_list_index;

				for (surface_list_index = 0;
					(short)surface_list_index < subcluster->surface_indices.count;
					surface_index_buffer++, surface_list_index++)
				{
					if (BIT_VECTOR_TEST_FLAG(render.environment_surface_flags, *surface_index_buffer) &&
						!BIT_VECTOR_TEST_FLAG(surface_flags, *surface_index_buffer))
					{
						if (found_count >= maximum_count)
						{
							break;
						}

						BIT_VECTOR_SET_FLAG(surface_flags, *surface_index_buffer, TRUE);
""",
"""#ifdef HALO_PSP
				long surface_list_index;

				for (surface_list_index = 0;
					(short)surface_list_index < subcluster->surface_indices.count;
					surface_list_index++)
				{
					long psp_surface_index = PSP_SURFACE_INDEX(&subcluster->surface_indices, surface_list_index);

					if (BIT_VECTOR_TEST_FLAG(render.environment_surface_flags, psp_surface_index) &&
						!BIT_VECTOR_TEST_FLAG(surface_flags, psp_surface_index))
					{
						if (found_count >= maximum_count)
						{
							break;
						}

						BIT_VECTOR_SET_FLAG(surface_flags, psp_surface_index, TRUE);
#else
				long *surface_index_buffer = TAG_BLOCK_GET_ELEMENT(&subcluster->surface_indices, 0, long);
				long surface_list_index;

				for (surface_list_index = 0;
					(short)surface_list_index < subcluster->surface_indices.count;
					surface_index_buffer++, surface_list_index++)
				{
					if (BIT_VECTOR_TEST_FLAG(render.environment_surface_flags, *surface_index_buffer) &&
						!BIT_VECTOR_TEST_FLAG(surface_flags, *surface_index_buffer))
					{
						if (found_count >= maximum_count)
						{
							break;
						}

						BIT_VECTOR_SET_FLAG(surface_flags, *surface_index_buffer, TRUE);
#endif
"""),
 # ChupathingyCE
 chupa=("""				long *surface_index_buffer = TAG_BLOCK_GET_ELEMENT(&subcluster->surface_indices, 0, long);
				long surface_list_index;

				for (surface_list_index = 0;
					(short)surface_list_index < subcluster->surface_indices.count;
					surface_index_buffer++, surface_list_index++)
				{
					/* port: (as in structure_visibility_traverse_subclusters) */
					if ((unsigned long)*surface_index_buffer >= (unsigned long)structure->surfaces.count)
					{
						continue;
					}

					if (BIT_VECTOR_TEST_FLAG(render.environment_surface_flags, *surface_index_buffer) &&
						!BIT_VECTOR_TEST_FLAG(surface_flags, *surface_index_buffer))
					{
						if (found_count >= maximum_count)
						{
							break;
						}

						BIT_VECTOR_SET_FLAG(surface_flags, *surface_index_buffer, TRUE);
						surface_indices[found_count++] = *surface_index_buffer;
""",
  """#ifdef HALO_PSP
				long surface_list_index;

				for (surface_list_index = 0;
					(short)surface_list_index < subcluster->surface_indices.count;
					surface_list_index++)
				{
					long psp_surface_index = PSP_SURFACE_INDEX(&subcluster->surface_indices, surface_list_index);

					/* port: (as in structure_visibility_traverse_subclusters) */
					if ((unsigned long)psp_surface_index >= (unsigned long)structure->surfaces.count)
					{
						continue;
					}

					if (BIT_VECTOR_TEST_FLAG(render.environment_surface_flags, psp_surface_index) &&
						!BIT_VECTOR_TEST_FLAG(surface_flags, psp_surface_index))
					{
						if (found_count >= maximum_count)
						{
							break;
						}

						BIT_VECTOR_SET_FLAG(surface_flags, psp_surface_index, TRUE);
						surface_indices[found_count++] = psp_surface_index;
#else
				long *surface_index_buffer = TAG_BLOCK_GET_ELEMENT(&subcluster->surface_indices, 0, long);
				long surface_list_index;

				for (surface_list_index = 0;
					(short)surface_list_index < subcluster->surface_indices.count;
					surface_index_buffer++, surface_list_index++)
				{
					/* port: (as in structure_visibility_traverse_subclusters) */
					if ((unsigned long)*surface_index_buffer >= (unsigned long)structure->surfaces.count)
					{
						continue;
					}

					if (BIT_VECTOR_TEST_FLAG(render.environment_surface_flags, *surface_index_buffer) &&
						!BIT_VECTOR_TEST_FLAG(surface_flags, *surface_index_buffer))
					{
						if (found_count >= maximum_count)
						{
							break;
						}

						BIT_VECTOR_SET_FLAG(surface_flags, *surface_index_buffer, TRUE);
						surface_indices[found_count++] = *surface_index_buffer;
#endif
""")),
Either(universal=("""						BIT_VECTOR_SET_FLAG(surface_flags, *surface_index_buffer, TRUE);
#endif
						surface_indices[found_count++] = *surface_index_buffer;
""",
"""						BIT_VECTOR_SET_FLAG(surface_flags, *surface_index_buffer, TRUE);
#endif
#ifdef HALO_PSP
						surface_indices[found_count++] = psp_surface_index;
#else
						surface_indices[found_count++] = *surface_index_buffer;
#endif
"""),
 # ChupathingyCE: the store is in the pair above
 chupa=None),
# leaf surface references in the bounded-surfaces pass
("""			struct structure_visibility_surface_reference *reference = TAG_BLOCK_GET_ELEMENT(
				&structure->surface_references,
				reference_index,
				struct structure_visibility_surface_reference);
			long surface_index = reference->surface_index;
""",
"""#ifdef HALO_PSP
			long surface_index, psp_node_index;

			psp_surface_reference(&structure->surface_references, reference_index, &surface_index, &psp_node_index);
#else
			struct structure_visibility_surface_reference *reference = TAG_BLOCK_GET_ELEMENT(
				&structure->surface_references,
				reference_index,
				struct structure_visibility_surface_reference);
			long surface_index = reference->surface_index;
#endif
"""),
]

FIXES_36["source/structures/structures.c#packed"] = [
("""		struct structure_surface_reference *surface_reference = TAG_BLOCK_GET_ELEMENT(
			&structure->surface_references,
			surface_reference_index,
			struct structure_surface_reference);

		if (surface_reference->bsp3d_node_index != NONE)
		{
			struct bsp3d_node *node = TAG_BLOCK_GET_ELEMENT(
				&TAG_BLOCK_GET_ELEMENT(
					&structure->collision_bsp,
					0,
					struct collision_bsp)->bsp3d.nodes,
				surface_reference->bsp3d_node_index,
				struct bsp3d_node);

			if (node->plane_designator == plane_index)
			{
				struct structure_surface *surface = TAG_BLOCK_GET_ELEMENT(
					&structure->surfaces,
					surface_reference->surface_index,
					struct structure_surface);
""",
"""#ifdef HALO_PSP
		struct structure_surface_reference psp_reference, *surface_reference = &psp_reference;

		psp_surface_reference(&structure->surface_references, surface_reference_index,
			&psp_reference.surface_index, &psp_reference.bsp3d_node_index);
#else
		struct structure_surface_reference *surface_reference = TAG_BLOCK_GET_ELEMENT(
			&structure->surface_references,
			surface_reference_index,
			struct structure_surface_reference);
#endif

		if (surface_reference->bsp3d_node_index != NONE)
		{
			struct bsp3d_node *node = TAG_BLOCK_GET_ELEMENT(
				&TAG_BLOCK_GET_ELEMENT(
					&structure->collision_bsp,
					0,
					struct collision_bsp)->bsp3d.nodes,
				surface_reference->bsp3d_node_index,
				struct bsp3d_node);

			if (node->plane_designator == plane_index)
			{
				struct structure_surface *surface = TAG_BLOCK_GET_ELEMENT(
					&structure->surfaces,
					surface_reference->surface_index,
					struct structure_surface);
"""),
("""					environment_vertex_compressed_get_point(
						structure_material_get_vertex(
							material,
							surface->vertex_indices[0]),
						&triangle_points[0]);
					environment_vertex_compressed_get_point(
						structure_material_get_vertex(
							material,
							surface->vertex_indices[1]),
						&triangle_points[1]);
					environment_vertex_compressed_get_point(
						structure_material_get_vertex(
							material,
							surface->vertex_indices[2]),
						&triangle_points[2]);
""",
"""#ifdef HALO_PSP
					psp_material_vertex_point(material, surface->vertex_indices[0], &triangle_points[0]);
					psp_material_vertex_point(material, surface->vertex_indices[1], &triangle_points[1]);
					psp_material_vertex_point(material, surface->vertex_indices[2], &triangle_points[2]);
#else
					environment_vertex_compressed_get_point(
						structure_material_get_vertex(
							material,
							surface->vertex_indices[0]),
						&triangle_points[0]);
					environment_vertex_compressed_get_point(
						structure_material_get_vertex(
							material,
							surface->vertex_indices[1]),
						&triangle_points[1]);
					environment_vertex_compressed_get_point(
						structure_material_get_vertex(
							material,
							surface->vertex_indices[2]),
						&triangle_points[2]);
#endif
"""),
]

FIXES_36["source/structures/structures.c#readers"] = [
Either(universal=("""#define structure_material_get_vertex(material, vertex_index) \\
	((struct environment_vertex_compressed const *) \\
		(material)->compressed_vertex_data.address + (vertex_index))
""",
"""#define structure_material_get_vertex(material, vertex_index) \\
	((struct environment_vertex_compressed const *) \\
		(material)->compressed_vertex_data.address + (vertex_index))
""" + PSP_BSP_READERS),
 # ChupathingyCE
 chupa=("""#define structure_material_get_vertex(material, vertex_index) \\
	((struct environment_vertex_compressed const *) \\
		xbox_pointer((material)->compressed_vertex_data.address) + (vertex_index))
""",
  """#define structure_material_get_vertex(material, vertex_index) \\
	((struct environment_vertex_compressed const *) \\
		xbox_pointer((material)->compressed_vertex_data.address) + (vertex_index))
""" + PSP_BSP_READERS)),
]
FIXES_36["source/structures/structure_visibility.c#readers"] = [
("""	COMPRESSED_STRUCTURE_VERTEX_SIZE = 0x20,
	_shader_type_environment = 3,
};
""",
"""	COMPRESSED_STRUCTURE_VERTEX_SIZE = 0x20,
	_shader_type_environment = 3,
};
""" + PSP_BSP_READERS),
]

# build 70: the collision BSP's edges, surfaces and bsp2d references at 16
# bits (invader-build -G packs them; every map on the PSP is built that way,
# the Xbox originals of all 23 maps being on hand). Every campaign BSP's
# counts fit: edges to 57,375 (unsigned), vertices and surfaces to 28,751,
# planes to 19,123 and bsp2d nodes to 28,445 with their flag bit at bit 15;
# no index in those tables is ever NONE. The flagged fields (a surface's and
# a reference's plane designator, a reference's root) are read through
# PSP_DESIGNATOR(), which gives the 32-bit convention the code was written
# for (the flag at LONG_MIN); bsp3d nodes stay 32-bit (to 51,300, with a
# leaf flag). Edges 24 -> 12 bytes, surfaces 12 -> 8, references 8 -> 4.
FIXES_36["source/physics/collision_bsp_definitions.h#packed"] = [
("""struct collision_surface
{
	long plane_designator;
	long first_edge_index;
	byte flags;
	byte breakable_surface_index;
	short material_index;
};

struct collision_edge
{
	long vertex_indices[2];
	long edge_indices[2];
	long surface_indices[2];
};
""",
"""#if defined(HALO_PSP) && !defined(PSP_UNPACKED_COLLISION)
/* packed (psp_fixups.py, build 70): the plane designator's flag at bit 15 */
struct collision_surface
{
	short plane_designator;
	unsigned short first_edge_index;
	byte flags;
	byte breakable_surface_index;
	short material_index;
};

struct collision_edge
{
	unsigned short vertex_indices[2];
	unsigned short edge_indices[2];
	unsigned short surface_indices[2];
};
#define PSP_DESIGNATOR(x) ((long)(x) < 0 ? (long)(LONG_MIN | ((x) & 0x7FFF)) : (long)(x))
#else
struct collision_surface
{
	long plane_designator;
	long first_edge_index;
	byte flags;
	byte breakable_surface_index;
	short material_index;
};

struct collision_edge
{
	long vertex_indices[2];
	long edge_indices[2];
	long surface_indices[2];
};
#define PSP_DESIGNATOR(x) ((long)(x))
#endif
"""),
("""typedef char collision_surface_size_assert[
	sizeof(struct collision_surface) == 0x0C ? 1 : -1];
typedef char collision_surface_first_edge_index_offset_assert[
	offsetof(struct collision_surface, first_edge_index) == 0x04 ? 1 : -1];
typedef char collision_surface_material_index_offset_assert[
	offsetof(struct collision_surface, material_index) == 0x0A ? 1 : -1];
typedef char collision_edge_size_assert[
	sizeof(struct collision_edge) == 0x18 ? 1 : -1];
typedef char collision_edge_edge_indices_offset_assert[
	offsetof(struct collision_edge, edge_indices) == 0x08 ? 1 : -1];
typedef char collision_edge_surface_indices_offset_assert[
	offsetof(struct collision_edge, surface_indices) == 0x10 ? 1 : -1];
""",
"""#if defined(HALO_PSP) && !defined(PSP_UNPACKED_COLLISION)
typedef char collision_surface_size_assert[
	sizeof(struct collision_surface) == 0x08 ? 1 : -1];
typedef char collision_edge_size_assert[
	sizeof(struct collision_edge) == 0x0C ? 1 : -1];
#else
typedef char collision_surface_size_assert[
	sizeof(struct collision_surface) == 0x0C ? 1 : -1];
typedef char collision_surface_first_edge_index_offset_assert[
	offsetof(struct collision_surface, first_edge_index) == 0x04 ? 1 : -1];
typedef char collision_surface_material_index_offset_assert[
	offsetof(struct collision_surface, material_index) == 0x0A ? 1 : -1];
typedef char collision_edge_size_assert[
	sizeof(struct collision_edge) == 0x18 ? 1 : -1];
typedef char collision_edge_edge_indices_offset_assert[
	offsetof(struct collision_edge, edge_indices) == 0x08 ? 1 : -1];
typedef char collision_edge_surface_indices_offset_assert[
	offsetof(struct collision_edge, surface_indices) == 0x10 ? 1 : -1];
#endif
"""),
]
FIXES_36["source/physics/collision_bsp.c#packed"] = [
("""struct bsp2d_reference
{
	long plane_designator;
	long root_index;
};
""",
"""#if defined(HALO_PSP) && !defined(PSP_UNPACKED_COLLISION)
/* packed (psp_fixups.py, build 70): both flags at bit 15 */
struct bsp2d_reference
{
	short plane_designator;
	short root_index;
};
#else
struct bsp2d_reference
{
	long plane_designator;
	long root_index;
};
#endif
"""),
("""typedef char bsp2d_reference_size_assert[
	sizeof(struct bsp2d_reference) == 0x08 ? 1 : -1];
""",
"""#if defined(HALO_PSP) && !defined(PSP_UNPACKED_COLLISION)
typedef char bsp2d_reference_size_assert[
	sizeof(struct bsp2d_reference) == 0x04 ? 1 : -1];
#else
typedef char bsp2d_reference_size_assert[
	sizeof(struct bsp2d_reference) == 0x08 ? 1 : -1];
#endif
"""),
]

# the flagged fields' readers: every "<surface-ish>->plane_designator" and
# "reference->root_index" wrapped in PSP_DESIGNATOR() (a regex pass, since
# there are 33 of them across 7 files; the left-hand side of an assignment is
# never one of these)
PACKED_DESIGNATOR_FILES = ["source/physics/collision_bsp.c", "source/physics/collision_features.c",
    "source/physics/breakable_surfaces.c", "source/physics/collision_debug.c", "source/effects/decals.c",
    "source/units/bipeds.c", "source/items/items.c"]
def packed_designators(tree):
    import re
    changed = 0
    pattern = re.compile(r"\b((?:[a-z0-9_]*surface[a-z0-9_]*|reference)->(?:plane_designator|root_index))\b")
    for rel in PACKED_DESIGNATOR_FILES:
        path = os.path.join(tree, rel)
        text = open(path, errors="surrogateescape").read()
        if "PSP_DESIGNATOR(" in text and rel.endswith("collision_bsp.c") is False:
            pass
        new, count = pattern.subn(r"PSP_DESIGNATOR(\1)", text)
        # never wrap a left-hand side (none exist in the game code; the check keeps it that way)
        if re.search(r"PSP_DESIGNATOR\([^)]*\)\s*=[^=]", new):
            sys.exit("psp_fixups: %s: a packed designator is assigned to" % rel)
        if count:
            if "#include" in new:
                first = new.index("#include")
                line_end = new.index("\n", first) + 1
                if "collision_bsp_definitions.h" not in new[:line_end + 2000]:
                    new = new[:line_end] + '#include "physics/collision_bsp_definitions.h" /* PSP_DESIGNATOR (psp_fixups.py) */\n' + new[line_end:]
            open(path, "w", errors="surrogateescape").write(new)
            changed += count
    return changed

# build 71: the game's clocks from the PSP's microsecond counter since boot.
# The Linux layer's GetTickCount and QueryPerformanceCounter read
# CLOCK_MONOTONIC, which the PSP's C library answers with the wall clock
# (gettimeofday): milliseconds since 1970, cut to 32 bits. With the vblank
# throttle off (build 66) the frame loop measures its first frame against a
# start time of 0, and that wall-clock value read as a signed long is
# negative for 24.8 days out of every 49.7, depending on the PSP's date: the
# game then asked to Sleep() for weeks before its first tick (Sheepdog01's b70 run,
# 2026-10-05: game time 0 forever, step main_update_time). The emulator's
# date happened to give a positive value
FIXES_36["port/linux/src/xbox_kernel.c#clock"] = [
Either(universal=("""DWORD WINAPI GetTickCount(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (DWORD)((unsigned long long)now.tv_sec * 1000ULL + (unsigned long long)now.tv_nsec / 1000000ULL);
}
""",
"""#ifdef HALO_PSP
long long sceKernelGetSystemTimeWide(void); /* microseconds since the PSP booted (psp_fixups.py) */
#endif
DWORD WINAPI GetTickCount(void)
{
#ifdef HALO_PSP
	return (DWORD)((unsigned long long)sceKernelGetSystemTimeWide() / 1000ULL);
#else
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (DWORD)((unsigned long long)now.tv_sec * 1000ULL + (unsigned long long)now.tv_nsec / 1000000ULL);
#endif
}
"""),
 # ChupathingyCE
 chupa=("""static unsigned long long platform_clock_nanoseconds(void)
{
	static unsigned long long start;""",
  """#ifdef HALO_PSP
long long sceKernelGetSystemTimeWide(void); /* microseconds since the PSP booted (psp_fixups.py) */

static unsigned long long platform_clock_nanoseconds(void)
{
	return (unsigned long long)sceKernelGetSystemTimeWide() * 1000ULL;
}
#else
static unsigned long long platform_clock_nanoseconds(void)
{
	static unsigned long long start;""")),
# ChupathingyCE only: the end of the desktop clock
Either(chupa=("""	return value - __atomic_load_n(&start, __ATOMIC_RELAXED);
}
""",
  """	return value - __atomic_load_n(&start, __ATOMIC_RELAXED);
}
#endif
""")),
Either(universal=("""BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER *count)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
""",
"""BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER *count)
{
	struct timespec now;

#ifdef HALO_PSP
	count->QuadPart = (LONGLONG)sceKernelGetSystemTimeWide();
	return TRUE;
#endif
	clock_gettime(CLOCK_MONOTONIC, &now);
"""),
 # ChupathingyCE
 chupa=("""	count->QuadPart = (LONGLONG)(platform_clock_nanoseconds() / (1000000000ULL / PLATFORM_PERFORMANCE_FREQUENCY));""",
  """#ifdef HALO_PSP
	/* the PSP's counter is in microseconds already: no 64-bit division for
	a clock read hundreds of times a frame (psp_fixups.py) */
	count->QuadPart = (LONGLONG)sceKernelGetSystemTimeWide();
#else
	count->QuadPart = (LONGLONG)(platform_clock_nanoseconds() / (1000000000ULL / PLATFORM_PERFORMANCE_FREQUENCY));
#endif""")),
]
# and the frame pacing never sleeps longer than a frame, whatever the clock says
FIXES_36["source/main/main.c#sleep"] = [
("""		if (milliseconds_elapsed < 33)
		{
			profile_idle_start();
			if (global_frame_rate_throttle)
				Sleep(33 - milliseconds_elapsed);
			profile_idle_end();
		}
""",
"""#ifdef HALO_PSP
		if (milliseconds_elapsed >= 0 && milliseconds_elapsed < 33)
#else
		if (milliseconds_elapsed < 33)
#endif
		{
			profile_idle_start();
			if (global_frame_rate_throttle)
				Sleep(33 - milliseconds_elapsed);
			profile_idle_end();
		}
"""),
]

# build 101: "crumbs" (psp_trace.c): the last steps drawn straight onto the
# screen, so a freeze that takes the whole PSP down (Sheepdog01's b100 runs: no
# crash report, no sound, no input; the crash handler's own test froze the
# same way) leaves the last thing the game started on the frozen picture
def _crumb(name, detail="0"):
    # build 106: the detail (often a tag name) is worked out only while crumbs are on
    return "#ifdef HALO_PSP\n\tchar *tag_get_name(long tag_index); int psp_crumb_mark(const char *what, const char *detail); extern int psp_crumbs_on; int psp_crumbed = psp_crumbs_on > 0 ? psp_crumb_mark(\"%s\", %s) : 0;\n#endif\n" % (name, detail)

FIXES_36["source/items/projectiles.c#crumbs"] = [
("static void projectile_detonate(\n\tlong projectile_index,\n\tboolean first_collision,\n\treal time_left)\n{\n",
 "static void projectile_detonate(\n\tlong projectile_index,\n\tboolean first_collision,\n\treal time_left)\n{\n" + _crumb("projectile_detonate")),
]
FIXES_36["source/objects/damage.c#crumbs"] = [
("void area_of_effect_cause_damage(\n\tstruct damage_data *damage,\n\tlong unused_object_index)\n{\n",
 "void area_of_effect_cause_damage(\n\tstruct damage_data *damage,\n\tlong unused_object_index)\n{\n" + _crumb("area_of_effect_cause_damage", "tag_get_name(damage->definition_index)")),
("\tshort material_index,\n\treal_vector3d const *object_normal)\n{\n",
 "\tshort material_index,\n\treal_vector3d const *object_normal)\n{\n" + _crumb("object_cause_damage", "tag_get_name(damage->definition_index)")),
]
FIXES_36["source/physics/breakable_surfaces.c#crumbs"] = [
("void breakable_surface_damage_area_of_effect(\n\tconst struct damage_data *damage_data)\n{\n",
 "void breakable_surface_damage_area_of_effect(\n\tconst struct damage_data *damage_data)\n{\n" + _crumb("breakable_surface_damage_area")),
]
FIXES_36["source/effects/effects.c#crumbs"] = [
("long effect_new_from_object(\n\tlong definition_index,\n\tlong owner_object_index,\n\tlong object_index,\n\tshort force_local_player_index,\n\treal scale_a,\n\treal scale_b,\n\treal_rgb_color const *color,\n\tstruct effect_vector_field const *impulse_field)\n{\n",
 "long effect_new_from_object(\n\tlong definition_index,\n\tlong owner_object_index,\n\tlong object_index,\n\tshort force_local_player_index,\n\treal scale_a,\n\treal scale_b,\n\treal_rgb_color const *color,\n\tstruct effect_vector_field const *impulse_field)\n{\n" + _crumb("effect_new_from_object", "tag_get_name(definition_index)")),
]

# build 101: the breakable surfaces' "still there" bits are read and written
# as 32-bit words (BIT_VECTOR_TEST_FLAG through a long *), but the array sat
# one byte into its struct, after a one-byte boolean: on x86 a misaligned
# word just works, on the PSP's MIPS it is an address error. The strict
# emulator caught it on a grenade's blast (breakable_surface_extant, from
# breakable_surface_damage_area_of_effect); Sheepdog01's b97-b100 freezes in The
# Library came at grenades and shots near the Flood. Aligned on 4 bytes now
FIXES_36["source/physics/breakable_surfaces.c#aligned"] = [
("\tboolean enabled;\n\tbyte breakable_surface_flags[MAXIMUM_STRUCTURE_BSPS_PER_SCENARIO][32];\n",
 "\tboolean enabled;\n#ifdef HALO_PSP\n\tbyte breakable_surface_flags[MAXIMUM_STRUCTURE_BSPS_PER_SCENARIO][32] __attribute__((aligned(4))); /* psp_fixups.py: read as words */\n#else\n\tbyte breakable_surface_flags[MAXIMUM_STRUCTURE_BSPS_PER_SCENARIO][32];\n#endif\n"),
("static struct breakable_surface_globals *globals;\n",
 "static struct breakable_surface_globals *globals;\n#ifdef HALO_PSP\n_Static_assert(__builtin_offsetof(struct breakable_surface_globals, breakable_surface_flags) % 4 == 0, \"breakable surface flags read as words\");\n#endif\n"),
]

# build 105: file descriptors. b104 run 1 showed the PSP lets the game hold
# only about ten files at once (descriptors 3-12; the 13th was refused), and
# two of the ten were the game's own logs, opened at start-up and never
# closed. Their lines now go through the port's log, which opens its file
# only for the moment of each batch write.
FIXES_105 = {}
FIXES_105["source/cseries/errors.c"] = [
("""static void write_to_debug_file(
	char const *string,
	boolean date)
{
	static FILE *volatile debug_file = NULL;
""",
 """#ifdef HALO_PSP
void platform_psp_log(const char *format, ...);

/* psp_fixups.py (build 105): debug.txt's lines go to the port's log, one
line each, without the file's own date prefix (the log has its own) */
static void write_to_debug_file(
	char const *string,
	boolean date)
{
	char line[512];
	size_t length;

	(void)date;
	snprintf(line, sizeof(line), "%s", string);
	length = strlen(line);
	while (length > 0 && (line[length - 1] == '\\n' || line[length - 1] == '\\r'))
		line[--length] = 0;
	if (length > 0)
		platform_psp_log("debug.txt: %s", line);
}
#else
static void write_to_debug_file(
	char const *string,
	boolean date)
{
	static FILE *volatile debug_file = NULL;
"""),
("""	fprintf(file, "%s%s", prefix, string);
	fflush(file);
}
""",
 """	fprintf(file, "%s%s", prefix, string);
	fflush(file);
}
#endif
"""),
]
# gamestate.txt listed every game-state allocation at start-up and kept the
# file open; the heartbeat's "game state:" survey (build 36) already reports
# each pool with its size and peak, so the PSP writes nothing here
FIXES_105["source/saved games/game_state.c"] = [
("""	// The January compiler inlines this logger into both arena allocators while
	// retaining one out-of-line copy under its private address-derived name.
	FILE *file = bss_004d27b0;
""",
 """	// The January compiler inlines this logger into both arena allocators while
	// retaining one out-of-line copy under its private address-derived name.
	FILE *file = bss_004d27b0;

#ifdef HALO_PSP
	/* psp_fixups.py (build 105): see the heartbeat's game state survey */
	(void)name; (void)type; (void)size; (void)gpu; (void)file;
	return;
#endif
"""),
]

# build 105: the game's render-debug cache (lines, spheres and text that
# developer tools draw into the world) holds 512 entries, 29 KB. Nothing in a
# PSP build draws debug geometry (b94 cut the debug vertex buffer for the same
# reason), so the shipping build keeps 16 and the heap gets the rest; a build
# with PSP_DEBUG_TOOLS=1 keeps Bungie's size. A full cache is already handled
# (the entry is dropped and the overflow reported once). The Xbox layout
# checks after the entries only hold at Bungie's size
FIXES_105["source/render/render_debug.c"] = [
("\tMAXIMUM_RENDER_DEBUG_CACHE_ENTRIES = 512,\n",
 "#if defined(HALO_PSP) && !PSP_DEBUG_TOOLS\n\tMAXIMUM_RENDER_DEBUG_CACHE_ENTRIES = 16, /* psp_fixups.py (build 105) */\n#else\n\tMAXIMUM_RENDER_DEBUG_CACHE_ENTRIES = 512,\n#endif\n"),
("typedef char render_debug_globals_size_check[\n\tsizeof(struct render_debug_globals_definition) == 0x740C ? 1 : -1];\n",
 "#if !(defined(HALO_PSP) && !PSP_DEBUG_TOOLS) /* psp_fixups.py (build 105): Bungie's size only */\ntypedef char render_debug_globals_size_check[\n\tsizeof(struct render_debug_globals_definition) == 0x740C ? 1 : -1];\n#endif\n"),
("typedef char render_debug_globals_game_time_offset_check[\n",
 "#if !(defined(HALO_PSP) && !PSP_DEBUG_TOOLS)\ntypedef char render_debug_globals_game_time_offset_check[\n"),
("\toffsetof(struct render_debug_globals_definition, string_overflow_reported) == 0x740B ? 1 : -1];\n",
 "\toffsetof(struct render_debug_globals_definition, string_overflow_reported) == 0x740B ? 1 : -1];\n#endif\n"),
]

# build 106: what the garbage collector does, for the corpse-sleep tests
# (b105 run 1: Sheepdog01 saw bodies vanish once he looked away, well below the 50
# active ones that start a collection on their own). Counted by reason; the
# heartbeat's objects line reports them
FIXES_106 = {}
# build 106: a corpse's aim. The unit's aiming and looking servo keeps
# running on a dead unit and, with nothing to aim at, overshoots back and
# forth between two values every tick (the emulator's scripted Flood fight:
# aiming_vector, aiming_velocity, looking_vector and looking_velocity flip
# each tick), so no corpse was ever byte-identical from tick to tick and none
# slept (b105 run 1). A dying unit's pose ignores them: the aiming and
# looking overlays are off in the dying states (unit_animation_aiming_screen,
# _unit_state_hard_ping.._unit_state_resurrect_back). So objects.c's corpse
# checksum leaves this range out; while a corpse sleeps it keeps its last values
FIXES_106["source/units/units.c#aim"] = [
("""void unit_get_aiming_vector(
	long unit_index,
	real_vector3d *aiming_vector)
{""",
 """#ifdef HALO_PSP
/* psp_fixups.py (build 106): the bytes of a unit's aim and look servo
(aiming_vector to the end of looking_velocity), for the corpse checksum in
objects.c, which can't see this structure */
const long psp_unit_aim_bytes[2] =
{
	offsetof(struct unit_datum, unit.aiming_vector),
	offsetof(struct unit_datum, unit.looking_velocity) + sizeof(real_vector3d)
};
#endif

void unit_get_aiming_vector(
	long unit_index,
	real_vector3d *aiming_vector)
{"""),
]
FIXES_106["source/objects/objects.c#garbage"] = [
("""	if (garbage_collect_mode!=NONE)
	{
		long garbage_collect_mode_wide;""",
 """#ifdef HALO_PSP
	{
		/* psp_fixups.py (build 106): runs by reason (forced by a script or
		the console, for space, for too many active), objects removed */
		extern long psp_garbage_runs[3];

		if (garbage_collect_mode == _garbage_collect_everything)
			psp_garbage_runs[0]++;
		else if (garbage_collect_mode == _garbage_collect_for_space)
			psp_garbage_runs[1]++;
		else if (garbage_collect_mode == _garbage_collect_active_objects)
			psp_garbage_runs[2]++;
	}
#endif
	if (garbage_collect_mode!=NONE)
	{
		long garbage_collect_mode_wide;"""),
("""					object_set_garbage(object_index, FALSE);
					object_delete_immediately(object_index);""",
 """#ifdef HALO_PSP
					{
						extern long psp_garbage_removed, psp_garbage_bipeds_removed;

						psp_garbage_removed++;
						if (header->type == _object_type_biped)
							psp_garbage_bipeds_removed++;
					}
#endif
					object_set_garbage(object_index, FALSE);
					object_delete_immediately(object_index);"""),
]

# build 106: animation frame data outside the tags. A map built with
# invader-build -M (and -G) keeps each animation's frame data at the front of
# its geometry pack, and the animation's data pointer is 0x0C000000 + its
# offset there (the tag window can't hold it with the rest of a campaign
# map's tags: campaign_window_fit.md). The game reads frame data in two
# places: animation_get_frame_data (a compressed animation's compressed block,
# or one uncompressed frame) and the three keyframe readers (the compressed
# block). Both now ask psp_animation_data for exactly the bytes they read; it
# answers from the tags, or through the animation cache (psp_anim.c). The
# pointer is good for the length of the caller's call, as before
FIXES_106["source/models/model_animation_definitions.h"] = [
Either(universal=("""#define animation_get_default_data(animation) ((animation)->default_data.address)
""",
 """#define animation_get_default_data(animation) ((animation)->default_data.address)

#ifdef HALO_PSP
/* psp_fixups.py (build 106): size bytes of an animation's frame data, offset
bytes in: in the tags, or, for a map built with its animations outside its
tags (invader-build -M), in its geometry pack, through the animation cache
(psp_anim.c), which reads them in if they aren't in RAM. The pointer stays
good while the caller works on this animation (the cache keeps the last
few ranges it handed out) */
void *psp_anim_lock(unsigned long address, unsigned long size);
static __inline__ byte *psp_animation_data(struct tag_data const *data, long offset, long size)
{
	unsigned long address = (unsigned long)data->address + (unsigned long)offset;

	if (__builtin_expect(address < 0x0C000000UL, 1))
		return (byte *)address;
	return (byte *)psp_anim_lock(address, (unsigned long)size);
}
#endif
"""),
 # ChupathingyCE
 chupa=("""#define animation_get_default_data(animation) xbox_pointer((animation)->default_data.address)
""",
  """#define animation_get_default_data(animation) xbox_pointer((animation)->default_data.address)

#ifdef HALO_PSP
/* psp_fixups.py (build 106): size bytes of an animation's frame data, offset
bytes in: in the tags, or, for a map built with its animations outside its
tags (invader-build -M), in its geometry pack, through the animation cache
(psp_anim.c), which reads them in if they aren't in RAM. The pointer stays
good while the caller works on this animation (the cache keeps the last
few ranges it handed out) */
void *psp_anim_lock(unsigned long address, unsigned long size);
static __inline__ byte *psp_animation_data(struct tag_data const *data, long offset, long size)
{
	unsigned long address = (unsigned long)data->address + (unsigned long)offset;

	if (__builtin_expect(address < 0x0C000000UL, 1))
		return (byte *)address;
	return (byte *)psp_anim_lock(address, (unsigned long)size);
}
#endif
""")),
]
FIXES_106["source/models/model_animation_definitions.c"] = [
("""	compressed = TEST_FLAG(animation->flags, _animation_compressed_bit) && hs_model_animation_compression_enabled;
	frame_data = tag_data_get_pointer(&animation->data, 0, 0);

	match_assert("c:\\\\halo\\\\SOURCE\\\\models\\\\model_animation_definitions.c", 1147, frame_index>=0 && frame_index<animation->frame_count);

	if (compressed)
		frame_data += animation->compressed_data_offset;
	else
		frame_data += frame_index * animation->frame_size;
""",
 """	compressed = TEST_FLAG(animation->flags, _animation_compressed_bit) && hs_model_animation_compression_enabled;
#ifndef HALO_PSP
	frame_data = tag_data_get_pointer(&animation->data, 0, 0);
#endif

	match_assert("c:\\\\halo\\\\SOURCE\\\\models\\\\model_animation_definitions.c", 1147, frame_index>=0 && frame_index<animation->frame_count);

#ifdef HALO_PSP
	/* psp_fixups.py (build 106): only the bytes this call's reader uses (the
	compressed block, or this frame), from the tags or the animation cache */
	if (compressed)
		frame_data = psp_animation_data(&animation->data, animation->compressed_data_offset,
			animation->data.size - animation->compressed_data_offset);
	else
		frame_data = psp_animation_data(&animation->data, frame_index * animation->frame_size, animation->frame_size);
#else
	if (compressed)
		frame_data += animation->compressed_data_offset;
	else
		frame_data += frame_index * animation->frame_size;
#endif
"""),
]
FIXES_106["source/models/model_animations.c#frames"] = [
("""	real_quaternion *rotation)
{
	byte *data = tag_data_get_pointer(&animation->data, animation->compressed_data_offset, 0);
""",
 """	real_quaternion *rotation)
{
#ifdef HALO_PSP
	/* psp_fixups.py (build 106): the compressed block, from the tags or the
	animation cache (model_animation_definitions.h) */
	byte *data = psp_animation_data(&animation->data, animation->compressed_data_offset,
		animation->data.size - animation->compressed_data_offset);
#else
	byte *data = tag_data_get_pointer(&animation->data, animation->compressed_data_offset, 0);
#endif
"""),
("""	real_point3d *translation)
{
	byte *data = tag_data_get_pointer(&animation->data, animation->compressed_data_offset, 0);
""",
 """	real_point3d *translation)
{
#ifdef HALO_PSP
	byte *data = psp_animation_data(&animation->data, animation->compressed_data_offset,
		animation->data.size - animation->compressed_data_offset);
#else
	byte *data = tag_data_get_pointer(&animation->data, animation->compressed_data_offset, 0);
#endif
"""),
("""	real *scale)
{
	byte *data = tag_data_get_pointer(&animation->data, animation->compressed_data_offset, 0);
""",
 """	real *scale)
{
#ifdef HALO_PSP
	byte *data = psp_animation_data(&animation->data, animation->compressed_data_offset,
		animation->data.size - animation->compressed_data_offset);
#else
	byte *data = tag_data_get_pointer(&animation->data, animation->compressed_data_offset, 0);
#endif
"""),
]

# build 106: the script node table at the size the map's scripts need. The
# Xbox's maps carry a table of 19,001 nodes (380 KB) in the scenario's tag
# data, the engine's maximum; b30 uses 4,557. A map built with
# invader-build -Y <spare> carries the nodes in use plus a spare share (the
# console's expressions take nodes while they run; hs_node_gc gives them back
# every 16 ticks). hs_allocate used the tag's table only when it was exactly
# the maximum's size, and otherwise put a full-size table in the heap and
# freed the tag's (with free(), on tag memory): it now takes any table that
# agrees with its own header
FIXES_106["source/hs/hs.c"] = [
Either(universal=("""	scenario = global_scenario_index != NONE ? global_scenario_get() : NULL;
	if (scenario &&
		scenario->hs_syntax_data.size ==
			sizeof(struct data_array)+MAXIMUM_HS_SYNTAX_NODES_PER_SCENARIO*sizeof(struct hs_syntax_node))
	{
		return;
	}
""",
 """	scenario = global_scenario_index != NONE ? global_scenario_get() : NULL;
	if (scenario &&
		scenario->hs_syntax_data.size ==
			sizeof(struct data_array)+MAXIMUM_HS_SYNTAX_NODES_PER_SCENARIO*sizeof(struct hs_syntax_node))
	{
		return;
	}
#ifdef HALO_PSP
	/* psp_fixups.py (build 106): a table sized to the map (invader-build -Y) */
	if (scenario && scenario->hs_syntax_data.address &&
		scenario->hs_syntax_data.size > (long)sizeof(struct data_array) &&
		((struct data_array *)scenario->hs_syntax_data.address)->maximum_count <= MAXIMUM_HS_SYNTAX_NODES_PER_SCENARIO &&
		scenario->hs_syntax_data.size == (long)(sizeof(struct data_array) +
			((struct data_array *)scenario->hs_syntax_data.address)->maximum_count * sizeof(struct hs_syntax_node)))
	{
		return;
	}
#endif
"""),
 # ChupathingyCE
 chupa=("""	/* port: as the map holds it only when it is sound */
	if (scenario &&
		scenario->hs_syntax_data.size ==
			sizeof(struct data_array)+MAXIMUM_HS_SYNTAX_NODES_PER_SCENARIO*sizeof(struct hs_syntax_node) &&
		hs_scenario_syntax_data_valid(scenario))
""",
  """	/* port: as the map holds it only when it is sound */
	if (scenario &&
#ifndef HALO_PSP
		/* (psp_fixups.py, build 106: on the PSP a table sized to the map,
		invader-build -Y; hs_scenario_syntax_data_valid checks its size) */
		scenario->hs_syntax_data.size ==
			sizeof(struct data_array)+MAXIMUM_HS_SYNTAX_NODES_PER_SCENARIO*sizeof(struct hs_syntax_node) &&
#endif
		hs_scenario_syntax_data_valid(scenario))
""")),
# ChupathingyCE only: its script data check, for a table sized to the map (its header)
Either(chupa=("""	return data->signature == 'd@t@' &&
		data->maximum_count == MAXIMUM_HS_SYNTAX_NODES_PER_SCENARIO &&
		data->size == sizeof(struct hs_syntax_node) &&
		data->valid &&
""",
  """	return data->signature == 'd@t@' &&
#ifdef HALO_PSP
		/* psp_fixups.py (build 106): any maximum up to the engine's, the
		table holding exactly that many nodes */
		data->maximum_count > 0 &&
		data->maximum_count <= MAXIMUM_HS_SYNTAX_NODES_PER_SCENARIO &&
		syntax_data_size == (long)(sizeof(struct data_array) + data->maximum_count * sizeof(struct hs_syntax_node)) &&
#else
		data->maximum_count == MAXIMUM_HS_SYNTAX_NODES_PER_SCENARIO &&
#endif
		data->size == sizeof(struct hs_syntax_node) &&
		data->valid &&
""")),
# ChupathingyCE only: its script data check, for a table sized to the map
Either(chupa=("""	long const syntax_data_size =
		sizeof(struct data_array)+MAXIMUM_HS_SYNTAX_NODES_PER_SCENARIO*sizeof(struct hs_syntax_node);
""",
  """#ifdef HALO_PSP
	/* psp_fixups.py (build 106): a table sized to the map (invader-build -Y),
	so its size is checked against its own header below */
	long const syntax_data_size = scenario->hs_syntax_data.size >= (long)sizeof(struct data_array) ?
		scenario->hs_syntax_data.size : -1;
#else
	long const syntax_data_size =
		sizeof(struct data_array)+MAXIMUM_HS_SYNTAX_NODES_PER_SCENARIO*sizeof(struct hs_syntax_node);
#endif
""")),
]

# build 106: datum_get, cheaper. The emulator's PC samples (tools/pcprofile.py)
# put 11% of the game thread's time on b30's beach in datum_get itself: every
# object, light and cluster reference lookup goes through it (object_get, the
# cluster partitions' lists, the object and light markers), millions of times
# a second. The lookup is a dozen instructions, but the release build's
# failure message (a 128-byte buffer on the stack) gave every call a 160-byte
# frame and a frame pointer. The message moved into a cold helper of its own,
# so the found-it path has no frame. (Inlining it at every caller instead cost
# 48 KB of code, 64 KB of heap)
FIXES_106["source/memory/data.c#fast"] = [
("""void *datum_get(
	struct data_array *data,
	long index)
{
	struct datum_header *header;
	short identifier;
	short absolute_index;
""",
 """#if defined(HALO_PSP) && defined(HALO_RELEASE)
/* psp_fixups.py (build 106): datum_get's failure message, out of its way */
static void __attribute__((noinline, cold)) psp_datum_get_failed(
	struct data_array *data,
	long index,
	void *caller)
{
	char message[128];

	csprintf(
		message,
		"%.32s index #%d (0x%x) is unused or changed (asked from %p)",
		data->name,
		index&0xFFFF,
		index,
		caller);
	release_assert_failed(message, "c:\\\\halo\\\\SOURCE\\\\memory\\\\data.c", 412, TRUE);
}
/* and no frame pointer for this one (the build keeps them for the crash
handler's walk; a lookup this small doesn't need one) */
void *datum_get(struct data_array *data, long index) __attribute__((optimize("omit-frame-pointer")));
#endif

void *datum_get(
	struct data_array *data,
	long index)
{
	struct datum_header *header;
	short identifier;
	short absolute_index;
"""),
("""#ifdef HALO_RELEASE
	/* port: a release build's message too, and where it was asked from
	(debug.txt's reference address places it in the build): the caller
	goes on with the NULL and crashes, with nothing else to say which
	array and which caller it was */
	{
		char message[128];
""",
 """#if defined(HALO_PSP) && defined(HALO_RELEASE)
	psp_datum_get_failed(data, index, __builtin_return_address(0));
#elif defined(HALO_RELEASE)
	/* port: a release build's message too, and where it was asked from
	(debug.txt's reference address places it in the build): the caller
	goes on with the NULL and crashes, with nothing else to say which
	array and which caller it was */
	{
		char message[128];
"""),
]

# build 106: Bungie's collision usage log (collision_usage.c) times every
# collision call with the performance counter (a system call each) and counts
# it by user, for a debug display the PSP build never shows: 2% of b30's
# tick in the emulator's PC samples went to the clock alone. Off unless the
# build keeps the debug tools (PSP_DEBUG_TOOLS)
FIXES_106["source/physics/collision_usage.c"] = [
("""void collision_log_start_time(
	LARGE_INTEGER *start_time)
{
	QueryPerformanceCounter(start_time);
""",
 """void collision_log_start_time(
	LARGE_INTEGER *start_time)
{
#if defined(HALO_PSP) && !defined(PSP_DEBUG_TOOLS)
	/* psp_fixups.py (build 106): the debug display's timing, off */
	start_time->QuadPart = 0;
	return;
#endif
	QueryPerformanceCounter(start_time);
"""),
("""	LARGE_INTEGER end_time;
	short user;

	QueryPerformanceCounter(&end_time);
	user = collision_log_get_current_user(collision_function);""",
 """	LARGE_INTEGER end_time;
	short user;

#if defined(HALO_PSP) && !defined(PSP_DEBUG_TOOLS)
	return;
#endif
	QueryPerformanceCounter(&end_time);
	user = collision_log_get_current_user(collision_function);"""),
("""	short user;

	user = collision_log_get_current_user(collision_function);
	if (user != NONE)
	{
		collision_usage_current.function[collision_function]
			.total_all_users.calls++;""",
 """	short user;

#if defined(HALO_PSP) && !defined(PSP_DEBUG_TOOLS)
	return;
#endif
	user = collision_log_get_current_user(collision_function);
	if (user != NONE)
	{
		collision_usage_current.function[collision_function]
			.total_all_users.calls++;"""),
]

# build 106: tag_get's usual case first, with no frame. Every object
# definition, shader and bitmap lookup goes through it (object_definition_get
# a dozen times per object per tick): the emulator's PC samples put 2.8% of
# b30's beach fight in tag_get and cache_get_tag_instance, most of it the
# frame the assert messages' buffers need. A found tag of the right group is
# returned at once; anything else goes to the original, unchanged, for its
# asserts
FIXES_106["source/cache/cache_files.c#fast"] = [
("""void *tag_get(
	long group_tag,
	long tag_index)
{
	char expected_group[16];
	char returned_group[16];
""",
 """#ifdef HALO_PSP
/* psp_fixups.py (build 106): the found-it path, then the checked original */
static void *psp_tag_get_checked(long group_tag, long tag_index);
void *tag_get(long group_tag, long tag_index) __attribute__((optimize("omit-frame-pointer")));
void *tag_get(
	long group_tag,
	long tag_index)
{
	short absolute_index = (short)tag_index;

	if (cache_file_globals.tags_loaded && global_tag_instances && cache_file_globals.tag_header &&
		absolute_index >= 0 && absolute_index < cache_file_globals.tag_header->tag_count)
	{
		struct cache_file_tag_instance *instance = &global_tag_instances[absolute_index];

		if ((!(tag_index & 0xFFFF0000) || instance->tag_index == tag_index) &&
			(instance->group_tag == group_tag || instance->parent_group_tags[0] == group_tag ||
				instance->parent_group_tags[1] == group_tag) &&
			instance->base_address)
		{
			return instance->base_address;
		}
	}
	return psp_tag_get_checked(group_tag, tag_index);
}

static void * __attribute__((noinline)) psp_tag_get_checked(
#else
void *tag_get(
#endif
	long group_tag,
	long tag_index)
{
	char expected_group[16];
	char returned_group[16];
"""),
]

# build 106: two more debug-only costs on hot paths (the emulator's PC
# samples, b30's beach fight): data_verify, an assert-only check of a data
# array's header, run on every step of every data array iteration and every
# datum_new (0.8%); and the frame profiler's texture timer around every
# object-lighting texture load (two clock reads a call). Both kept with the
# debug tools (PSP_DEBUG_TOOLS)
FIXES_106["source/memory/data.c#verify"] = [
("""void data_verify(
	struct data_array *data)
{
	match_assert("c:\\\\halo\\\\SOURCE\\\\memory\\\\data.c", 457, data);
""",
 """void data_verify(
	struct data_array *data)
{
#if defined(HALO_PSP) && !defined(PSP_DEBUG_TOOLS)
	/* psp_fixups.py (build 106): asserts only */
	return;
#endif
	match_assert("c:\\\\halo\\\\SOURCE\\\\memory\\\\data.c", 457, data);
"""),
]
FIXES_106["source/cseries/profile.c#texture"] = [
Either(universal=("""void profile_texture_start(
	void)
{
	profile_timesection_begin_now(&profile_globals.current_frame.texture);
""",
 """void profile_texture_start(
	void)
{
#if defined(HALO_PSP) && !defined(PSP_DEBUG_TOOLS)
	/* psp_fixups.py (build 106): the frame profiler's texture time, unused */
	return;
#endif
	profile_timesection_begin_now(&profile_globals.current_frame.texture);
"""),
 # ChupathingyCE: times the texture only while the profiler's graph or dumps read it
 chupa=None),
Either(universal=("""void profile_texture_end(
	void)
{
	profile_timesection_end_now(&profile_globals.current_frame.texture);
""",
 """void profile_texture_end(
	void)
{
#if defined(HALO_PSP) && !defined(PSP_DEBUG_TOOLS)
	return;
#endif
	profile_timesection_end_now(&profile_globals.current_frame.texture);
"""),
 # ChupathingyCE: (as profile_texture_start)
 chupa=None),
]

# build 106: datum_get inline where it is hottest. datum_get itself is
# cheaper now (above), but its call volume stays: on b30's beach the
# emulator's PC samples still put 9-11% of the game thread in it, from the
# cluster partitions' reference lists (an object's move takes it out of each
# of its clusters' lists and puts it back, walking each list), object_get
# and the object and light markers. In those files (and the reference list
# helpers every list walk uses) the found-it path is inline; elsewhere the
# call stays, so the code grows only where it pays
FIXES_106["source/memory/data.h#inline"] = [
("""void *datum_get(struct data_array *data, long index);
""",
 """void *datum_get(struct data_array *data, long index);
#ifdef HALO_PSP
/* psp_fixups.py (build 106): datum_get's found-it path for the hottest
callers (cluster partitions, reference lists, objects.c, object_lights.c);
anything else is datum_get's, with its message */
static __inline__ void *psp_datum_get_inline(struct data_array *data, long index)
{
	short absolute_index = (short)index;
	short identifier = (short)(index >> 16);

	if (absolute_index >= 0 && absolute_index < data->count)
	{
		struct datum_header *header = (struct datum_header *)((byte *)data->data + data->size * absolute_index);

		if (header->identifier && (!identifier || identifier == header->identifier))
			return header;
	}
	return datum_get(data, index);
}
#endif
"""),
]
FIXES_106["source/objects/reference_lists.h"] = [
("""	if (*reference_index!=NONE)
	{
		reference = (struct data_reference *)datum_get(array, *reference_index);""",
 """	if (*reference_index!=NONE)
	{
#ifdef HALO_PSP
		/* psp_fixups.py (build 106): inline (memory/data.h) */
		reference = (struct data_reference *)psp_datum_get_inline(array, *reference_index);
#else
		reference = (struct data_reference *)datum_get(array, *reference_index);
#endif"""),
]
_PSP_DATUM_INLINE = """
#ifdef HALO_PSP
/* psp_fixups.py (build 106): datum_get's found-it path inline in this file
(memory/data.h) */
#define datum_get(data, index) psp_datum_get_inline((data), (index))
#endif
"""
FIXES_106["source/structures/cluster_partitions.c#inline"] = [
("""#include "structures/structures.h"

/* ---------- constants */""",
 """#include "structures/structures.h"
""" + _PSP_DATUM_INLINE + """
/* ---------- constants */"""),
]
FIXES_106["source/objects/objects.c#inline"] = [
("""#include "units/units.h"

/* ---------- constants */""",
 """#include "units/units.h"
""" + _PSP_DATUM_INLINE + """
/* ---------- constants */"""),
]
FIXES_106["source/objects/object_lights.c#inline"] = [
Either(universal=("""#include "units/units.h"

/* ---------- constants */""",
 """#include "units/units.h"
""" + _PSP_DATUM_INLINE + """
/* ---------- constants */"""),
 # ChupathingyCE
 chupa=("""#include "units/units.h"

/* port: port/linux/src (halo.log) */""",
 """#include "units/units.h"
""" + _PSP_DATUM_INLINE + """
/* port: port/linux/src (halo.log) */""")),
]
# build 106: game_sound_update in four steps for the profile. Sheepdog01's b105
# run: 11.5 ms a frame (median of 94 windows; up to 16.4), against about 1 ms
# in the emulator on b30. Its parts: the cluster's sound environment, the
# potentially audible set (every cluster against the camera's), the
# background music and every looping sound's update
FIXES_106["source/sound/game_sound.c#steps"] = [
("""	sound_manager_set_sound_environment(sound_environment);
	compute_combined_pas();
""",
 """#ifdef HALO_PSP
	{
		void psp_step(const char *name); /* psp_fixups.py (build 106) */

		psp_step("game_sound_update: set environment");
		sound_manager_set_sound_environment(sound_environment);
		psp_step("game_sound_update: audible clusters");
		compute_combined_pas();
		psp_step("game_sound_update: music");
	}
#else
	sound_manager_set_sound_environment(sound_environment);
	compute_combined_pas();
#endif
"""),
("""	for (looping_sound_index = data_next_index(game_looping_sound_data, NONE);
		looping_sound_index != NONE;
		looping_sound_index = data_next_index(game_looping_sound_data, looping_sound_index))
	{
		struct game_looping_sound_datum *sound = game_looping_sound_get(looping_sound_index);

		if (sound->object_index == NONE)""",
 """#ifdef HALO_PSP
	{
		void psp_step(const char *name); /* psp_fixups.py (build 106) */

		psp_step("game_sound_update: looping sounds");
	}
#endif
	for (looping_sound_index = data_next_index(game_looping_sound_data, NONE);
		looping_sound_index != NONE;
		looping_sound_index = data_next_index(game_looping_sound_data, looping_sound_index))
	{
		struct game_looping_sound_datum *sound = game_looping_sound_get(looping_sound_index);

		if (sound->object_index == NONE)"""),
]
# build 106: the sounds' obstruction tests. Each sound in an audible cluster
# gets a collision test from the camera (structure, water, objects, scenery,
# machines) every frame it is refreshed: the emulator's step-tagged PC samples
# put The Library's "game_sound_update: looping sounds" step at 6.3% of the
# time, two thirds of it these tests; Sheepdog01's b105 run measured the step at
# 11.5 ms a frame. Two changes:
# - source_audible: a sound past its maximum distance is out whatever the test
#   says, so it isn't tested (exact: the same sounds are heard);
# - compute_sound_obstruction: a result is reused for up to
#   PSP_OBSTRUCTION_FRESH_TICKS (the Xbox reused it within a tick); older ones
#   are tested again while the frame's PSP_OBSTRUCTION_TESTS last, and reused
#   past that (a muffling that follows the camera 100-200 ms late; b107 caps
#   how late: FIXES_107 game_sound.c#stale)
# The counts go to the log every 300 frames ("sound obstruction:")
FIXES_106["source/sound/sound_manager.c#obstruction"] = [
("""		if (nearest_listener_index != NONE)
		{
			compute_sound_obstruction(
				nearest_listener_index,
				source,
				square_root(nearest_distance_squared));
		}
""",
 """		if (nearest_listener_index != NONE)
		{
#ifdef HALO_PSP
			/* psp_fixups.py (build 106): past its reach, out whatever the
			test says (below); left obstructed and occluded */
			extern unsigned long psp_obstruction_beyond;

			if (nearest_distance_squared > maximum_distance * maximum_distance)
			{
				source->obstruction = 0.6f;
				source->occlusion = 1.0f;
				psp_obstruction_beyond++;
			}
			else
#endif
			compute_sound_obstruction(
				nearest_listener_index,
				source,
				square_root(nearest_distance_squared));
		}
"""),
]
FIXES_106["source/sound/game_sound.c#obstruction"] = [
("""void compute_sound_obstruction(
	short local_player_index,
	struct sound_source *source,
	real distance)
{
	struct observer_result const *camera = observer_get_camera(local_player_index);
	long slot = obstruction_cache_slot(local_player_index, &source->location.position);
	long now = game_time_get();

	if (obstruction_cache[slot].valid &&
		obstruction_cache[slot].game_time == now &&
		obstruction_cache[slot].local_player_index == local_player_index &&
		obstruction_cache[slot].position.x == source->location.position.x &&
		obstruction_cache[slot].position.y == source->location.position.y &&
		obstruction_cache[slot].position.z == source->location.position.z)
	{
		source->obstruction = obstruction_cache[slot].obstruction;
		source->occlusion = obstruction_cache[slot].occlusion;
		return;
	}
""",
 """#ifdef HALO_PSP
/* psp_fixups.py (build 106): see FIXES_106 game_sound.c#obstruction */
#define PSP_OBSTRUCTION_FRESH_TICKS 4
#define PSP_OBSTRUCTION_TESTS 6
static long psp_obstruction_frame = -1;
static int psp_obstruction_frame_tests = 0;
unsigned long psp_obstruction_tests = 0, psp_obstruction_reused = 0, psp_obstruction_beyond = 0;
static unsigned long psp_obstruction_frames = 0;

void psp_obstruction_report(void)
{
	void platform_psp_log(const char *format, ...);

	if (++psp_obstruction_frames < 300)
		return;
	platform_psp_log("sound obstruction: %.1f tests a frame, %.1f reused (up to %d ticks old, or older past %d tests), %.1f past their reach and not tested",
		psp_obstruction_tests / 300.0, psp_obstruction_reused / 300.0, PSP_OBSTRUCTION_FRESH_TICKS, PSP_OBSTRUCTION_TESTS,
		psp_obstruction_beyond / 300.0);
	psp_obstruction_frames = psp_obstruction_tests = psp_obstruction_reused = psp_obstruction_beyond = 0;
}
#endif

void compute_sound_obstruction(
	short local_player_index,
	struct sound_source *source,
	real distance)
{
	struct observer_result const *camera = observer_get_camera(local_player_index);
	long slot = obstruction_cache_slot(local_player_index, &source->location.position);
	long now = game_time_get();

#ifdef HALO_PSP
	if (psp_obstruction_frame != game_sound_globals->update_index)
	{
		psp_obstruction_frame = game_sound_globals->update_index;
		psp_obstruction_frame_tests = 0;
	}
	if (obstruction_cache[slot].valid &&
		obstruction_cache[slot].local_player_index == local_player_index &&
		obstruction_cache[slot].position.x == source->location.position.x &&
		obstruction_cache[slot].position.y == source->location.position.y &&
		obstruction_cache[slot].position.z == source->location.position.z &&
		/* unsigned: a game time gone back (a checkpoint revert) counts as old */
		((unsigned long)(now - obstruction_cache[slot].game_time) < PSP_OBSTRUCTION_FRESH_TICKS ||
		psp_obstruction_frame_tests >= PSP_OBSTRUCTION_TESTS))
	{
		source->obstruction = obstruction_cache[slot].obstruction;
		source->occlusion = obstruction_cache[slot].occlusion;
		if (obstruction_cache[slot].game_time != now)
			psp_obstruction_reused++;
		return;
	}
	psp_obstruction_frame_tests++;
	psp_obstruction_tests++;
#else
	if (obstruction_cache[slot].valid &&
		obstruction_cache[slot].game_time == now &&
		obstruction_cache[slot].local_player_index == local_player_index &&
		obstruction_cache[slot].position.x == source->location.position.x &&
		obstruction_cache[slot].position.y == source->location.position.y &&
		obstruction_cache[slot].position.z == source->location.position.z)
	{
		source->obstruction = obstruction_cache[slot].obstruction;
		source->occlusion = obstruction_cache[slot].occlusion;
		return;
	}
#endif
"""),
("""	game_sound_globals->update_index++;

	return;
}
""",
 """	game_sound_globals->update_index++;
#ifdef HALO_PSP
	{
		void psp_obstruction_report(void); /* psp_fixups.py (build 106), below */

		psp_obstruction_report();
	}
#endif

	return;
}
"""),
]
# build 117: looked for again by default: the GE lights models by them now
# (b116 test 1: Sheepdog01 saw no lighting on actors). "nopointlights": as before.
# build 106: an object's dynamic point lights (muzzle flashes, plasma bolts)
# are looked for only to pick the Xbox's vertex shader with
# per-vertex point lights (rasterizer_xbox_models.c); the GE draws a model
# with the ambient colour and the two distant lights alone (psp_ge.c,
# psp_ge_model_begin), and the dynamic lights' share of an object's light
# reaches it through the lightmap sample, which looks for them separately.
# The search walked every light in each of the object's clusters for every
# object drawn, every frame: 2.4% of the emulator's time at b30's beach (the
# "object_get_level_of_detail_pixels" step). "objectpointlights" in keep.txt
# looks for them again
FIXES_106["source/objects/object_lights.c#pointlights"] = [
("""void lights_prepare_for_object_dynamic(
	long object_index,
	struct render_lighting *lighting)
{
	struct object_cluster_iterator iterator;
	real_point3d center;
	real radius;
	real light_intensities[MAXIMUM_RENDERED_POINT_LIGHTS];
	real light_attenuations[MAXIMUM_RENDERED_POINT_LIGHTS];
	short cluster_index;
	short light_index;
""",
 """void lights_prepare_for_object_dynamic(
	long object_index,
	struct render_lighting *lighting)
{
	struct object_cluster_iterator iterator;
	real_point3d center;
	real radius;
	real light_intensities[MAXIMUM_RENDERED_POINT_LIGHTS];
	real light_attenuations[MAXIMUM_RENDERED_POINT_LIGHTS];
	short cluster_index;
	short light_index;
#ifdef HALO_PSP
	/* psp_fixups.py (build 106): see FIXES_106 object_lights.c#pointlights.
	Build 117: looked for again by default, as the GE now lights models by
	them (rasterizer.c, psp_ge_model_point_light); "nopointlights" skips
	them, as builds 106-116 did */
	static int psp_point_lights = -1;

	if (psp_point_lights < 0)
	{
		int psp_keep(const char *name);

		psp_point_lights = !psp_keep("nopointlights");
	}
	if (!psp_point_lights)
	{
		lighting->point_light_count = 0;
		return;
	}
#endif
"""),
]
# build 106: no frame pointer for the object lookups (object_get is
# object_get_and_verify_type): about 4% of all instructions in the emulator's
# Library and b30 samples go to them, called from everywhere. The build keeps
# frame pointers for the crash handler's walk; these are leaves on their fast
# path (the type check's message is cold), as datum_get and tag_get already are
FIXES_106["source/objects/objects.c#frameless"] = [
("""void *object_get_and_verify_type(
	long object_index,
	unsigned long valid_type_flags)
{""",
 """#ifdef HALO_PSP
/* psp_fixups.py (build 106) */
void *object_get_and_verify_type(long object_index, unsigned long valid_type_flags) __attribute__((optimize("omit-frame-pointer")));
void *object_header_block_get(long object_index, struct object_header_block_reference *reference) __attribute__((optimize("omit-frame-pointer")));
#endif

void *object_get_and_verify_type(
	long object_index,
	unsigned long valid_type_flags)
{"""),
]
# build 106: lightning (the Library's beams and arcs, on objects' markers)
# cleared all of its 4,097-point scratch array, 147 KB on the stack, for
# each chain of each instance every frame, so that its checks could find
# unused points marked not valid. The emulator's step-tagged PC samples:
# 2.6% of The Library's time in that memset (under "widgets render", the
# step before it), and on the PSP it also pushes 147 KB through the data
# cache each time. Only the points a chain uses are read, and each is
# written before it is read, so only the flags of those are cleared, as each
# segment begins: the same points come out
FIXES_106["source/objects/widgets/lightning.c"] = [
("""							point_count = 0;
							csmemset(points, 0, sizeof(points));""",
 """							point_count = 0;
#ifdef HALO_PSP
							/* psp_fixups.py (build 106): the first point is
							written in full below; the segments' flags are
							cleared as each begins */
							points[0].valid = FALSE;
#else
							csmemset(points, 0, sizeof(points));
#endif"""),
("""							short octave_index;

							match_assert("c:\\\\halo\\\\SOURCE\\\\objects\\\\widgets\\\\lightning.c", 383, !points[point_count+segment_point_count].valid);""",
 """							short octave_index;

#ifdef HALO_PSP
							{
								/* psp_fixups.py (build 106): this segment's points,
								not valid until written (above) */
								short clear_index;

								for (clear_index = 1; clear_index <= segment_point_count &&
									point_count + clear_index < MAXIMUM_LIGHTNING_POINTS; clear_index++)
									points[point_count + clear_index].valid = FALSE;
							}
#endif
							match_assert("c:\\\\halo\\\\SOURCE\\\\objects\\\\widgets\\\\lightning.c", 383, !points[point_count+segment_point_count].valid);"""),
]
# build 106: the cache file thread (the map's reads: sounds, the second BSP,
# the tag reads the loader queues) above the game thread. The PSP runs the
# highest-priority ready thread and never shares the CPU between equals, so a
# read that has finished in the driver waits for the game thread to block
# before its thread can issue the next one, and it keeps the stick's lock
# meanwhile. Sheepdog01's b105 run: 87 ms a call for reads the stick does in about
# 4 ms. The thread sleeps in the driver nearly all the time, so it costs the
# game thread little CPU. psp_platform.c's psp_cache_thread_priority has the
# rule and the keep word ("readerslow" leaves it where pthreads put it)
FIXES_106["source/cache/cache_files_windows.c#priority"] = [
("""static void cache_file_windows_thread_proc(
	void)
{
	while (TRUE)
	{
		while (WaitForSingleObjectEx(
			cache_file_globals.sleep_event,
			INFINITE,
			TRUE) == WAIT_IO_COMPLETION)
			;
""",
 """static void cache_file_windows_thread_proc(
	void)
{
	while (TRUE)
	{
		while (WaitForSingleObjectEx(
			cache_file_globals.sleep_event,
			INFINITE,
			TRUE) == WAIT_IO_COMPLETION)
			;
#ifdef HALO_PSP
		{
			/* psp_fixups.py (build 106): once keep.txt has been read */
			extern void psp_cache_thread_priority(void);

			psp_cache_thread_priority();
		}
#endif
"""),
]

FIXES_107 = {}
# build 107: no debug record for an actor's path search. Each search took a
# path_debug_storage (82 KB, most of it a copy of the search's own state)
# from ai_debug_get_path_storage, which cleared all of it, and the search
# copied its 82 KB state into it at the end, for the debug display of paths
# a PSP build never shows: two 82 KB memory passes per path search. The
# path code takes a NULL record everywhere (path.c tests state->debug before
# each use), so with no record nothing else changes. The emulator's PC
# samples: 3% of b30's "ai update" step in that memset alone. Kept with the
# debug tools (PSP_DEBUG_TOOLS), as is the records' array (114 KB of heap)
FIXES_107["source/ai/ai_debug.c#pathstorage"] = [
("""struct path_debug_storage *ai_debug_get_path_storage(
	long actor_index)
{
	short storage_index = NONE;
	short index;
""",
 """struct path_debug_storage *ai_debug_get_path_storage(
	long actor_index)
{
	short storage_index = NONE;
	short index;

#if defined(HALO_PSP) && !PSP_DEBUG_TOOLS
	/* psp_fixups.py (build 107): see FIXES_107 ai_debug.c#pathstorage */
	(void)actor_index;
	(void)storage_index;
	(void)index;
	return NULL;
#endif
"""),
# and its array (114 KB of heap) isn't made: nothing asks for it. (The
# sizes are written as the game has them: ai_debug_records, after these
# fixups, wraps both, the allocation's and the memset's, in PSP_DEBUG_COUNT)
("""	actor_path_debug_array = actor_path_debug_array==NULL ? (struct path_debug_storage *)debug_malloc(sizeof(*actor_path_debug_array) * MAXIMUM_NUMBER_OF_ACTOR_PATHS, FALSE, "c:\\\\halo\\\\SOURCE\\\\ai\\\\ai_debug.c", 148) : actor_path_debug_array;

	match_assert("c:\\\\halo\\\\SOURCE\\\\ai\\\\ai_debug.c", 150, actor_debug_array && actor_path_debug_array);""",
 """#if defined(HALO_PSP) && !PSP_DEBUG_TOOLS
	/* psp_fixups.py (build 107): see FIXES_107 ai_debug.c#pathstorage */
	actor_path_debug_array = NULL;
#else
	actor_path_debug_array = actor_path_debug_array==NULL ? (struct path_debug_storage *)debug_malloc(sizeof(*actor_path_debug_array) * MAXIMUM_NUMBER_OF_ACTOR_PATHS, FALSE, "c:\\\\halo\\\\SOURCE\\\\ai\\\\ai_debug.c", 148) : actor_path_debug_array;
#endif

#if defined(HALO_PSP) && !PSP_DEBUG_TOOLS
	match_assert("c:\\\\halo\\\\SOURCE\\\\ai\\\\ai_debug.c", 150, actor_debug_array);
#else
	match_assert("c:\\\\halo\\\\SOURCE\\\\ai\\\\ai_debug.c", 150, actor_debug_array && actor_path_debug_array);
#endif"""),
# (and the five that read it take no array as no records)
("""	match_assert("c:\\\\halo\\\\SOURCE\\\\ai\\\\ai_debug.c", 211, actor_path_debug_array)

	memset(actor_path_debug_array, 0, sizeof(*actor_path_debug_array) * MAXIMUM_NUMBER_OF_ACTOR_PATHS);
""",
 """	if (actor_path_debug_array)
		memset(actor_path_debug_array, 0, sizeof(*actor_path_debug_array) * MAXIMUM_NUMBER_OF_ACTOR_PATHS);
"""),
("""void ai_debug_actor_deleted(
	long actor_index)
{
	short path_index;
""",
 """void ai_debug_actor_deleted(
	long actor_index)
{
	short path_index;

	if (!actor_path_debug_array)
		return;
"""),
("""	short found_path_index = NONE;
	long found_path_time = NONE;
""",
 """	short found_path_index = NONE;
	long found_path_time = NONE;

	if (!actor_path_debug_array)
		return NULL;
"""),
# (ai_debug_update calls this every tick)
("""static void ai_debug_path_storage_update(
	void)
{
	short index;
	short other_index;
""",
 """static void ai_debug_path_storage_update(
	void)
{
	short index;
	short other_index;

	if (!actor_path_debug_array)
		return;
"""),
("""static void ai_debug_render_paths_failed(
	void)
{
	short index;
""",
 """static void ai_debug_render_paths_failed(
	void)
{
	short index;

	if (!actor_path_debug_array)
		return;
"""),
]
# build 107: the case-blind string compare without a tolower call per
# character. Marker lookups (model_find_marker's binary search: weapons,
# attachments, the AI's aim) and the scripts' name lookups go through
# _stricmp; the emulator's PC samples put _stricmp and tolower at about 1%
# of b30's beach, 4% of The Library's "widgets render" step (lightning's
# markers). The game never changes the C locale, where tolower maps A-Z
# alone, so folding A-Z by hand gives the same answers
FIXES_107["port/linux/src/msvc_crt.c"] = [
("""int _stricmp(const char *string1, const char *string2)
{
	for (;; string1++, string2++)
	{
		int c1 = tolower((unsigned char)*string1);
		int c2 = tolower((unsigned char)*string2);

		if (c1 != c2 || !c1)
			return c1 - c2;
	}
}

int _strnicmp(const char *string1, const char *string2, size_t count)
{
	for (; count; count--, string1++, string2++)
	{
		int c1 = tolower((unsigned char)*string1);
		int c2 = tolower((unsigned char)*string2);
""",
 """#ifdef HALO_PSP
/* psp_fixups.py (build 107): tolower in the C locale, inline */
#define PSP_FOLD(c) ((c) >= 'A' && (c) <= 'Z' ? (c) + ('a' - 'A') : (c))
#define tolower(c) PSP_FOLD(c)
#endif

int _stricmp(const char *string1, const char *string2)
{
	for (;; string1++, string2++)
	{
		int c1 = tolower((unsigned char)*string1);
		int c2 = tolower((unsigned char)*string2);

		if (c1 != c2 || !c1)
			return c1 - c2;
	}
}

int _strnicmp(const char *string1, const char *string2, size_t count)
{
	for (; count; count--, string1++, string2++)
	{
		int c1 = tolower((unsigned char)*string1);
		int c2 = tolower((unsigned char)*string2);
"""),
("""char *_strdup(const char *string)
{""",
 """#ifdef HALO_PSP
#undef tolower
#endif

char *_strdup(const char *string)
{"""),
]
# build 107: each object's bounding sphere also kept in a dense array (game
# state, so a checkpoint saves and restores it with the objects), for rays
# and sphere queries to reject an object without reading its datum. A ray's
# objects (collision_test_vector: the AI's lines of sight, projectiles,
# sounds) and a sphere query's (collision_get_features_in_sphere: the
# bipeds' and vehicles' physics) are every object in each cluster they touch,
# about 300 on b30's beach, mostly sleeping scenery; each visit read the
# object's header and datum (the emulator's samples: object_get_and_verify_
# type 16.8% and datum_get 10.7% of b30's "ai update" step; on the PSP, cache
# misses the emulator doesn't charge). The sphere is written in one place,
# object_compute_node_matrices, and the copy beside it; a new object's
# datum and copy both start zeroed. The test on the copy is the same test,
# on the same numbers, that the original makes first (its other conditions
# are and-ed with it, and a cluster's objects are roots, whose children are
# tested only inside it), so the same objects are tested. "spherecheck" in
# keep.txt compares the copy with the datum at every use
FIXES_107["source/objects/objects.h#spheres"] = [
("""

#endif // __OBJECTS_H""",
 """

#ifdef HALO_PSP
/* psp_fixups.py (build 107): see FIXES_107 objects.h#spheres */
struct psp_object_sphere
{
	real_point3d center;
	real radius;
};
extern struct psp_object_sphere *psp_object_spheres;
#endif

#endif // __OBJECTS_H"""),
]
FIXES_107["source/objects/objects.c#spheres"] = [
("""	object_globals = (struct object_globals *)game_state_malloc("object globals", NULL, sizeof(*object_globals));
	match_assert("c:\\\\halo\\\\SOURCE\\\\objects\\\\objects.c", 219, object_globals);
""",
 """	object_globals = (struct object_globals *)game_state_malloc("object globals", NULL, sizeof(*object_globals));
	match_assert("c:\\\\halo\\\\SOURCE\\\\objects\\\\objects.c", 219, object_globals);
#ifdef HALO_PSP
	/* psp_fixups.py (build 107): see FIXES_107 objects.h#spheres */
	psp_object_spheres = (struct psp_object_sphere *)game_state_malloc("object spheres", NULL,
		MAXIMUM_OBJECTS_PER_MAP * sizeof(struct psp_object_sphere));
	match_assert("c:\\\\halo\\\\SOURCE\\\\objects\\\\objects.c", 219, psp_object_spheres);
#endif
"""),
("""		if (memory_pool_block_allocate(object_memory_pool, (void**)&header->datum, size))
		{
			header->data_size = size;
			memset(header->datum, 0, size);
""",
 """		if (memory_pool_block_allocate(object_memory_pool, (void**)&header->datum, size))
		{
			header->data_size = size;
			memset(header->datum, 0, size);
#ifdef HALO_PSP
			/* psp_fixups.py (build 107): the sphere's copy, zeroed with it */
			if (psp_object_spheres && data == object_header_data &&
				DATUM_INDEX_TO_ABSOLUTE_INDEX(index) < MAXIMUM_OBJECTS_PER_MAP)
				memset(&psp_object_spheres[DATUM_INDEX_TO_ABSOLUTE_INDEX(index)], 0, sizeof(struct psp_object_sphere));
#endif
"""),
("""	if (object->object.scale > 0.f)
	{
		object->object.bounding_sphere_radius *= object->object.scale;
	}

	return;
}""",
 """	if (object->object.scale > 0.f)
	{
		object->object.bounding_sphere_radius *= object->object.scale;
	}
#ifdef HALO_PSP
	/* psp_fixups.py (build 107): and its copy */
	if (psp_object_spheres && DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index) < MAXIMUM_OBJECTS_PER_MAP)
	{
		psp_object_spheres[DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index)].center = object->object.bounding_sphere_center;
		psp_object_spheres[DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index)].radius = object->object.bounding_sphere_radius;
	}
#endif

	return;
}"""),
("""static long object_header_new(
	struct data_array *data, 
	long index,
	short size)
{""",
 """#ifdef HALO_PSP
/* psp_fixups.py (build 107): see FIXES_107 objects.h#spheres */
struct psp_object_sphere *psp_object_spheres = NULL;
#endif

static long object_header_new(
	struct data_array *data, 
	long index,
	short size)
{"""),
]
# build 107: a recorded animation's event stream (the cinematics' actors) is
# packed byte by byte, so a short-difference event's two shorts can start at
# an odd address. The Xbox reads them anyway; the PSP's CPU raises an address
# error on a halfword load from an odd address (the strict emulator's
# "unaligned load of 2 bytes", update_controller_short, 35 s into a10's
# opening). Packed, the struct is read a byte at a time, the same 4 bytes.
# The other events are read with memcpy, or are single bytes
FIXES_107["source/cutscene/recorded_animation_playback.c"] = [
("""struct vector_short_difference_data
{
	short delta_yaw;
	short delta_pitch;
};""",
 """struct vector_short_difference_data
{
	short delta_yaw;
	short delta_pitch;
}
#ifdef HALO_PSP
__attribute__((packed))
#endif
;"""),
]
# build 107: a BSP load filled the window's free part, from the tags' end to
# the window's end, with 0xCD (the Xbox debug build's fill) before reading the
# BSP over its share of it. Since b106 the animation cache keeps pages in the
# part no BSP ever reaches (the pack's header gives it): a10's first BSP
# switch, 40 s into its opening, filled those pages with 0xCD under the
# cache's feet, and the next keyframe read went through 0xCDCDCDCD offsets
# (b106 too: "animnowindow" kept it alive). Nothing reads the fill, so on the
# PSP it is left out: the BSP's read writes its own share, and the bytes
# between stay the cache's. Also up to 3.8 MB of memory writes a BSP switch
FIXES_107["source/cache/cache_files.c#bspfill"] = [
Either(universal=("""	tag_cache_base_address = physical_memory_get_tag_cache_base_address();
	csmemset(
		tag_cache_base_address + cache_file_globals.header.tag_data_size,
		0xCD,
		PSP_TAG_CACHE_SIZE - cache_file_globals.header.tag_data_size);""",
 """	tag_cache_base_address = physical_memory_get_tag_cache_base_address();
#ifndef HALO_PSP
	csmemset(
		tag_cache_base_address + cache_file_globals.header.tag_data_size,
		0xCD,
		PSP_TAG_CACHE_SIZE - cache_file_globals.header.tag_data_size);
#else
	(void)tag_cache_base_address;
#endif"""),
 # ChupathingyCE
 chupa=("""	tag_cache_base_address = physical_memory_get_tag_cache_base_address();
#ifdef HALO_CUSTOM_EDITION
	/* port: (not a Custom Edition map's: its tags are in a tag cache of their
	own, and may be more than the Xbox's holds) */
	if (!cache_file_is_ce)
#endif
	csmemset(
		tag_cache_base_address + cache_file_globals.header.tag_data_size,
		0xCD,
		PSP_TAG_CACHE_SIZE - cache_file_globals.header.tag_data_size);""",
  """	tag_cache_base_address = physical_memory_get_tag_cache_base_address();
#ifndef HALO_PSP
#ifdef HALO_CUSTOM_EDITION
	/* port: (not a Custom Edition map's: its tags are in a tag cache of their
	own, and may be more than the Xbox's holds) */
	if (!cache_file_is_ce)
#endif
	csmemset(
		tag_cache_base_address + cache_file_globals.header.tag_data_size,
		0xCD,
		PSP_TAG_CACHE_SIZE - cache_file_globals.header.tag_data_size);
#else
	(void)tag_cache_base_address;
#endif""")),
]
# build 107: a read request is published by setting its slot's "pending", and
# the reader thread takes any slot that is pending and not running. The C
# order (fields, then pending) isn't what runs: nothing tells the compiler
# another thread looks, so it may store the fields in any order, and in b107s
# it stores pending first, before the event, size, offset and buffer. Since
# b106 the reader runs above the game thread (0x1C over 0x20), so if it wakes
# in those few instructions (a read finishing), it preempts and reads a
# half-written slot: the old request's buffer and offset, or none at all (the
# "slot pending with no buffer" log). A compiler barrier keeps pending last;
# one core, so the CPU itself doesn't reorder what the other thread sees
FIXES_107["source/cache/cache_files_windows.c#publish"] = [
("""	request->pending = TRUE;
	request->blocking = blocking;
	request->running = FALSE;
	cache_file_windows_thread_wake();""",
 """#ifndef HALO_PSP
	request->pending = TRUE;
	request->blocking = blocking;
	request->running = FALSE;
#else
	/* psp_fixups.py (build 107): every field before pending (see FIXES_107
	cache_files_windows.c#publish) */
	request->blocking = blocking;
	request->running = FALSE;
	__asm__ __volatile__("" ::: "memory");
	request->pending = TRUE;
#endif
	cache_file_windows_thread_wake();"""),
]
# build 107: b106's obstruction cache (FIXES_106 game_sound.c#obstruction)
# reused a result of any age once a frame's 6 tests were spent. The sounds
# come in the same order every frame, so with many of them the first ones go
# stale and take the budget again before the last ones get a turn: at 2
# ticks a frame, past the 12th sound a sound kept the muffling of wherever
# the camera was when it first played (or, after a checkpoint revert, from
# before it). Past the budget a result is now reused only while it is under
# PSP_OBSTRUCTION_STALE_TICKS (half a second) old; an older one is tested
# whatever the budget. So a sound past the 12th is tested about every 8
# frames instead of never. The log's "past the budget" counts every test
# past it: those, and a new sound's first test (never reused, b106 too)
FIXES_107["source/sound/game_sound.c#stale"] = [
("""#define PSP_OBSTRUCTION_TESTS 6
""",
 """#define PSP_OBSTRUCTION_TESTS 6
#define PSP_OBSTRUCTION_STALE_TICKS 15 /* build 107: see FIXES_107 game_sound.c#stale */
"""),
("""unsigned long psp_obstruction_tests = 0, psp_obstruction_reused = 0, psp_obstruction_beyond = 0;""",
 """unsigned long psp_obstruction_tests = 0, psp_obstruction_reused = 0, psp_obstruction_beyond = 0, psp_obstruction_late = 0;"""),
("""	platform_psp_log("sound obstruction: %.1f tests a frame, %.1f reused (up to %d ticks old, or older past %d tests), %.1f past their reach and not tested",
		psp_obstruction_tests / 300.0, psp_obstruction_reused / 300.0, PSP_OBSTRUCTION_FRESH_TICKS, PSP_OBSTRUCTION_TESTS,
		psp_obstruction_beyond / 300.0);
	psp_obstruction_frames = psp_obstruction_tests = psp_obstruction_reused = psp_obstruction_beyond = 0;""",
 """	platform_psp_log("sound obstruction: %.1f tests a frame (%.1f of them past the budget of %d), %.1f reused (up to %d ticks old, or %d past the budget), %.1f past their reach and not tested",
		psp_obstruction_tests / 300.0, psp_obstruction_late / 300.0, PSP_OBSTRUCTION_TESTS,
		psp_obstruction_reused / 300.0, PSP_OBSTRUCTION_FRESH_TICKS, PSP_OBSTRUCTION_STALE_TICKS,
		psp_obstruction_beyond / 300.0);
	psp_obstruction_frames = psp_obstruction_tests = psp_obstruction_reused = psp_obstruction_beyond = psp_obstruction_late = 0;"""),
("""		((unsigned long)(now - obstruction_cache[slot].game_time) < PSP_OBSTRUCTION_FRESH_TICKS ||
		psp_obstruction_frame_tests >= PSP_OBSTRUCTION_TESTS))""",
 """		((unsigned long)(now - obstruction_cache[slot].game_time) < PSP_OBSTRUCTION_FRESH_TICKS ||
		(psp_obstruction_frame_tests >= PSP_OBSTRUCTION_TESTS &&
		(unsigned long)(now - obstruction_cache[slot].game_time) < PSP_OBSTRUCTION_STALE_TICKS)))"""),
("""	psp_obstruction_frame_tests++;
	psp_obstruction_tests++;""",
 """	if (psp_obstruction_frame_tests >= PSP_OBSTRUCTION_TESTS)
		psp_obstruction_late++;
	psp_obstruction_frame_tests++;
	psp_obstruction_tests++;"""),
]
FIXES_107["source/physics/collisions.c#spheres"] = [
("""boolean collision_test_vector(
	unsigned long flags,
	real_point3d const *point,""",
 """#ifdef HALO_PSP
/* psp_fixups.py (build 107): the objects' sphere copies, first (FIXES_107
objects.h#spheres). "spherecheck": each use compared with the datum */
static int psp_sphere_check = -1;
unsigned long psp_sphere_tests = 0, psp_sphere_rejects = 0, psp_sphere_checked = 0, psp_sphere_wrong = 0;

static struct psp_object_sphere const *psp_sphere_of(long object_index)
{
	struct psp_object_sphere const *sphere = &psp_object_spheres[DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index)];

	if (psp_sphere_check < 0)
	{
		int psp_keep(const char *name);

		psp_sphere_check = psp_keep("spherecheck");
	}
	if (psp_sphere_check)
	{
		struct object_datum const *object = object_get(object_index);

		psp_sphere_checked++;
		if (memcmp(&object->object.bounding_sphere_center, &sphere->center, sizeof(sphere->center)) ||
			memcmp(&object->object.bounding_sphere_radius, &sphere->radius, sizeof(sphere->radius)))
			psp_sphere_wrong++;
	}
	psp_sphere_tests++;
	return sphere;
}

/* object_test_vector's own sphere test */
static boolean psp_sphere_may_cross(
	long object_index,
	real_point3d const *point,
	real_vector3d const *vector)
{
	struct psp_object_sphere const *sphere = psp_sphere_of(object_index);

	if (fast_vector_intersects_sphere(point, vector, &sphere->center, sphere->radius))
		return TRUE;
	psp_sphere_rejects++;
	return FALSE;
}

/* object_get_features_in_sphere's own sphere test */
static boolean psp_sphere_may_touch(
	long object_index,
	real_point3d const *center,
	real radius)
{
	struct psp_object_sphere const *sphere = psp_sphere_of(object_index);
	real reach = sphere->radius;

	reach += radius;
	if (point_in_sphere(center, &sphere->center, reach))
		return TRUE;
	psp_sphere_rejects++;
	return FALSE;
}
#endif
boolean collision_test_vector(
	unsigned long flags,
	real_point3d const *point,"""),
("""						if (object_mark_function(object_index) &&
							object_test_vector(""",
 """						if (
#ifdef HALO_PSP
							psp_sphere_may_cross(object_index, point, vector) &&
#endif
							object_mark_function(object_index) &&
							object_test_vector("""),
("""						if (object_mark_function(object_index))
						{
#ifdef HALO_PSP
							psp_features_objects++;
#endif""",
 """						if (
#ifdef HALO_PSP
							psp_sphere_may_touch(object_index, center, radius) &&
#endif
							object_mark_function(object_index))
						{
#ifdef HALO_PSP
							psp_features_objects++;
#endif"""),
]
# build 107: a cluster partition's element unlinked without walking its
# cluster's list. Every object that moves is disconnected from its clusters
# and connected again each tick (cluster_partition_reconnect), and the
# removal walked the cluster's whole list to find the element before it:
# b30's beach cluster holds about 300 objects, so each moving object's
# removal read about 150 elements, each a likely cache miss on the PSP. The
# emulator's PC samples: reference_list_remove 1.35% of all the time, 4% of
# "objects update". Each element now keeps the element before it in its
# list in its spare word (pad: an absolute index, 0xFFFF for the list's
# head), and the object's own reference to the cluster keeps where its
# element is. The unlink checks both links before it trusts them and falls
# back to the walk otherwise, and removes the same element the walk would
# (an object is in a cluster's list once), so the lists come out the same
FIXES_107["source/structures/cluster_partitions.c#unlink"] = [
("""void cluster_partition_reconnect(
	struct cluster_partition *partition,""",
 """#ifdef HALO_PSP
/* psp_fixups.py (build 107): see FIXES_107 cluster_partitions.c#unlink */
#define PSP_REFERENCE_HEAD 0xFFFF
static unsigned long psp_unlinks_direct = 0, psp_unlinks_walked = 0;
/* "unlinkcheck" in keep.txt: each direct unlink checked against the walk */
static int psp_unlink_check = -1;
static unsigned long psp_unlinks_checked = 0, psp_unlinks_wrong = 0;

static struct data_reference *psp_reference_at(
	struct data_array *array,
	long absolute_index)
{
	struct data_reference *reference;

	if (absolute_index < 0 || absolute_index >= array->maximum_count)
		return NULL;
	reference = (struct data_reference *)((char *)array->data + absolute_index * array->size);
	return reference->identifier ? reference : NULL;
}

/* after both adds: the new element's place before the old head, and the
object's cluster reference pointing at it */
static void psp_cluster_links(
	struct cluster_partition *partition,
	long cluster_reference_before,
	long *first_cluster_reference,
	long data_head_before,
	short cluster_index)
{
	long data_head = *cluster_partition_get_first_reference(partition, cluster_index);
	struct data_reference *reference;

	if (data_head == NONE || data_head == data_head_before)
		return;
	reference = (struct data_reference *)datum_get(partition->data_reference_data, data_head);
	reference->pad = PSP_REFERENCE_HEAD;
	if (data_head_before != NONE)
		((struct data_reference *)datum_get(partition->data_reference_data, data_head_before))->pad =
			(word)DATUM_INDEX_TO_ABSOLUTE_INDEX(data_head);
	if (*first_cluster_reference != NONE && *first_cluster_reference != cluster_reference_before)
		((struct data_reference *)datum_get(partition->cluster_reference_data, *first_cluster_reference))->pad =
			(word)DATUM_INDEX_TO_ABSOLUTE_INDEX(data_head);
}

/* the object's element in the cluster's list, at hint, unlinked if both its
links check out; FALSE: the caller walks the list */
static boolean psp_cluster_unlink(
	struct cluster_partition *partition,
	short cluster_index,
	long datum_index,
	word hint)
{
	struct data_array *array = partition->data_reference_data;
	long *head = cluster_partition_get_first_reference(partition, cluster_index);
	struct data_reference *reference = psp_reference_at(array, hint), *before = NULL;
	long index;

	if (!reference || reference->datum_index != datum_index)
		return FALSE;
	index = DATUM_INDEX_NEW(hint, reference->identifier);
	if (psp_unlink_check < 0)
	{
		int psp_keep(const char *name);

		psp_unlink_check = psp_keep("unlinkcheck");
	}
	if (psp_unlink_check)
	{
		/* "unlinkcheck": the element the walk would remove, compared */
		long walk = *head;

		while (walk != NONE && ((struct data_reference *)datum_get(array, walk))->datum_index != datum_index)
			walk = ((struct data_reference *)datum_get(array, walk))->next_reference_index;
		psp_unlinks_checked++;
		if (walk != index)
			psp_unlinks_wrong++;
	}
	if (reference->pad == PSP_REFERENCE_HEAD)
	{
		if (*head != index)
			return FALSE;
	}
	else
	{
		before = psp_reference_at(array, reference->pad);
		if (!before || before->next_reference_index != index)
			return FALSE;
	}
	datum_delete(array, index);
	if (before)
		before->next_reference_index = reference->next_reference_index;
	else
		*head = reference->next_reference_index;
	if (reference->next_reference_index != NONE)
		((struct data_reference *)datum_get(array, reference->next_reference_index))->pad = reference->pad;
	return TRUE;
}

/* the counts, for the log (psp_platform.c's object report) */
void psp_cluster_unlink_counts(unsigned long *direct, unsigned long *walked, unsigned long *checked, unsigned long *wrong)
{
	*direct = psp_unlinks_direct;
	*walked = psp_unlinks_walked;
	*checked = psp_unlinks_checked;
	*wrong = psp_unlinks_wrong;
	psp_unlinks_direct = psp_unlinks_walked = 0;
}
#endif

void cluster_partition_reconnect(
	struct cluster_partition *partition,"""),
Either(universal=("""		short const cluster_index = cluster_indices[cluster_index_index];

		reference_list_add(
			partition->cluster_reference_data,
			first_cluster_reference,
			cluster_index);

		reference_list_add(
			partition->data_reference_data,
			cluster_partition_get_first_reference(partition, cluster_index),
			datum_index);
""",
 """		short const cluster_index = cluster_indices[cluster_index_index];
#ifdef HALO_PSP
		long psp_cluster_before = *first_cluster_reference;
		long psp_data_before = *cluster_partition_get_first_reference(partition, cluster_index);
#endif

		reference_list_add(
			partition->cluster_reference_data,
			first_cluster_reference,
			cluster_index);

		reference_list_add(
			partition->data_reference_data,
			cluster_partition_get_first_reference(partition, cluster_index),
			datum_index);
#ifdef HALO_PSP
		psp_cluster_links(partition, psp_cluster_before, first_cluster_reference, psp_data_before, cluster_index);
#endif
"""),
 # ChupathingyCE
 chupa=("""		short const cluster_index = cluster_indices[cluster_index_index];
		/* port: reference_list_add's, noting where the references are */
		long cluster_reference_index = cluster_partition_port_add(
			partition->cluster_reference_data,
			first_cluster_reference,
			cluster_index,
			NULL);
		long data_reference_index = cluster_partition_port_add(
			partition->data_reference_data,
			cluster_partition_get_first_reference(partition, cluster_index),
			datum_index,
			partition->port_previous_references);

		partition->port_modification_count++;
		if (cluster_reference_index != NONE && partition->port_cluster_data_references)
		{
			partition->port_cluster_data_references[DATUM_INDEX_TO_ABSOLUTE_INDEX(cluster_reference_index)] =
				data_reference_index;
		}
""",
  """		short const cluster_index = cluster_indices[cluster_index_index];
#ifdef HALO_PSP
		/* psp_fixups.py (build 107): the places in the references' spare
		words (FIXES_107 cluster_partitions.c#unlink), not in arrays */
		long psp_cluster_before = *first_cluster_reference;
		long psp_data_before = *cluster_partition_get_first_reference(partition, cluster_index);

		reference_list_add(
			partition->cluster_reference_data,
			first_cluster_reference,
			cluster_index);
		reference_list_add(
			partition->data_reference_data,
			cluster_partition_get_first_reference(partition, cluster_index),
			datum_index);
		partition->port_modification_count++;
		psp_cluster_links(partition, psp_cluster_before, first_cluster_reference, psp_data_before, cluster_index);
#else
		/* port: reference_list_add's, noting where the references are */
		long cluster_reference_index = cluster_partition_port_add(
			partition->cluster_reference_data,
			first_cluster_reference,
			cluster_index,
			NULL);
		long data_reference_index = cluster_partition_port_add(
			partition->data_reference_data,
			cluster_partition_get_first_reference(partition, cluster_index),
			datum_index,
			partition->port_previous_references);

		partition->port_modification_count++;
		if (cluster_reference_index != NONE && partition->port_cluster_data_references)
		{
			partition->port_cluster_data_references[DATUM_INDEX_TO_ABSOLUTE_INDEX(cluster_reference_index)] =
				data_reference_index;
		}
#endif
""")),
Either(universal=("""		datum_delete(partition->cluster_reference_data, cluster_reference_index);

		reference_list_remove(
			partition->data_reference_data,
			cluster_partition_get_first_reference(partition, cluster_index),
			datum_index);
""",
 """		datum_delete(partition->cluster_reference_data, cluster_reference_index);

#ifdef HALO_PSP
		if (psp_cluster_unlink(partition, cluster_index, datum_index, cluster_reference->pad))
			psp_unlinks_direct++;
		else
		{
			psp_unlinks_walked++;
#endif
		reference_list_remove(
			partition->data_reference_data,
			cluster_partition_get_first_reference(partition, cluster_index),
			datum_index);
#ifdef HALO_PSP
		}
#endif
"""),
 # ChupathingyCE
 chupa=("""		datum_delete(partition->cluster_reference_data, cluster_reference_index);

		/* port: reference_list_remove's, starting where the reference was
		put */
		cluster_partition_port_remove(
			partition,
			cluster_partition_get_first_reference(partition, cluster_index),
			datum_index,
			data_reference_index);
""",
  """		datum_delete(partition->cluster_reference_data, cluster_reference_index);

#ifdef HALO_PSP
		/* psp_fixups.py (build 107): FIXES_107 cluster_partitions.c#unlink */
		(void)data_reference_index;
		partition->port_modification_count++;
		if (psp_cluster_unlink(partition, cluster_index, datum_index, cluster_reference->pad))
			psp_unlinks_direct++;
		else
		{
			psp_unlinks_walked++;
			reference_list_remove(
				partition->data_reference_data,
				cluster_partition_get_first_reference(partition, cluster_index),
				datum_index);
		}
#else
		/* port: reference_list_remove's, starting where the reference was
		put */
		cluster_partition_port_remove(
			partition,
			cluster_partition_get_first_reference(partition, cluster_index),
			datum_index,
			data_reference_index);
#endif
""")),
# ChupathingyCE only: its side arrays of reference places, which the PSP keeps in the references themselves
Either(chupa=("""	partition->port_previous_references = debug_malloc(HALO_PORT_MAXIMUM_CLUSTER_REFERENCES * sizeof(long), FALSE,
		__FILE__, __LINE__);
	partition->port_cluster_data_references = debug_malloc(HALO_PORT_MAXIMUM_CLUSTER_REFERENCES * sizeof(long), FALSE,
		__FILE__, __LINE__);
""",
  """#ifdef HALO_PSP
	/* psp_fixups.py (build 107): the places live in the references' spare
	words on the PSP (FIXES_107 cluster_partitions.c#unlink): no 16 KB of
	heap a partition for them */
	partition->port_previous_references = NULL;
	partition->port_cluster_data_references = NULL;
#else
	partition->port_previous_references = debug_malloc(HALO_PORT_MAXIMUM_CLUSTER_REFERENCES * sizeof(long), FALSE,
		__FILE__, __LINE__);
	partition->port_cluster_data_references = debug_malloc(HALO_PORT_MAXIMUM_CLUSTER_REFERENCES * sizeof(long), FALSE,
		__FILE__, __LINE__);
#endif
""")),
("""		if (reference->datum_index == datum_index)
		{
			datum_delete(array, *reference_index);
			*reference_index = reference->next_reference_index;
""",
 """		if (reference->datum_index == datum_index)
		{
			datum_delete(array, *reference_index);
			*reference_index = reference->next_reference_index;
#ifdef HALO_PSP
			/* psp_fixups.py (build 107): the next element's link back */
			if (reference->next_reference_index != NONE)
				((struct data_reference *)datum_get(array, reference->next_reference_index))->pad = reference->pad;
#endif
"""),
]

# build 108: Sheepdog01's b107 run of b30, to the level's end (docs/hardware_b107.md)
FIXES_108 = {}
# b30's floors are mirrors: the security room's at z 9.95 (BSP b30a, cluster
# 48) and the map room's at z -14.92 (b30b, cluster 14). With one in view the
# Xbox first renders the level from the mirror's camera into a second target
# (the camera reflected through the floor), then the player's window, and
# lays the first over the mirror's surface. The PSP draws one window a frame
# from the first window's camera (psp_ge_window_begin), so the whole frame
# came out from under the floor with the look's pitch turned over: the camera
# "under the map" at those spots and directions. No mirror window now; the
# floors draw as the Xbox draws them with no mirror found (has_mirror FALSE)
FIXES_108["source/render/render.c#mirror"] = [
("""	if (main_get_window_count() == 1)
	{
		if (structure_visibility_find_mirror(camera, &frustum, &mirror))""",
 """#ifdef HALO_PSP
	/* psp_fixups.py (build 108): no mirror window (see FIXES_108) */
	if (FALSE)
#else
	if (main_get_window_count() == 1)
#endif
	{
		if (structure_visibility_find_mirror(camera, &frustum, &mirror))"""),
]
# the next level's map missing from the stick: the run ends at the level's
# end (psp_next_map_check, psp_platform.c) rather than in the game's
# "damaged disc" loop
FIXES_108["source/main/main.c#nextmap"] = [
("""		if (game_in_progress() && main_globals.connection == _game_connection_local)
		{
			struct game_options options;
			short local_player_index;

			game_options_new(&options);""",
 """		if (game_in_progress() && main_globals.connection == _game_connection_local)
		{
			struct game_options options;
			short local_player_index;

#ifdef HALO_PSP
			{
				/* psp_fixups.py (build 108): see FIXES_108 main.c#nextmap */
				extern void psp_next_map_check(const char *scenario_path);

				psp_next_map_check(main_globals.soloplayer_map_name);
			}
#endif
			game_options_new(&options);"""),
]

# Four tag blocks without the bytes the game never reads on the PSP
# (invader-build -Z, build 108): a sound permutation's name and subtitle data
# (124 -> 72 bytes), a pitch range's name (72 -> 40), an AI squad's unused
# fields (232 -> 144) and a firing position's padding (24 -> 20). On a marine
# level 0.4-0.6 MiB of the 9 MiB tag window; b40 doesn't fit without it
# (docs/overnight_b108.md). The PSP's structs leave those bytes out, so the
# game reads the blocks as it always has; a map baked before build 108 is
# made slim in place as its tags load (psp/src/psp_tag_layouts.c, which has
# the table the bake uses too). The names read in messages and debug dumps
# print the tag's name or nothing (below); a squad keeps its name
FIXES_108["source/sound/sound_definitions.h#layouts"] = [
("""struct sound_pitch_range
{
	char name[32];""",
 """/* psp_fixups.py (build 108): pitch ranges and permutations on the PSP
without their names and a permutation without its subtitle data (FIXES_108,
psp/src/psp_tag_layouts.c) */
struct sound_pitch_range
{
#ifndef HALO_PSP
	char name[32];
#endif"""),
("""struct sound_permutation
{
	char name[32];
	real skip_fraction;""",
 """struct sound_permutation
{
#ifndef HALO_PSP
	char name[32];
#endif
	real skip_fraction;"""),
("""	struct tag_data samples;
	struct tag_data mouth_data;
	struct tag_data subtitle_data;
};""",
 """	struct tag_data samples;
	struct tag_data mouth_data;
#ifndef HALO_PSP
	struct tag_data subtitle_data;
#endif
};"""),
]
FIXES_108["source/ai/ai_scenario_definitions.h#layouts"] = [
("""	word pad;
	unsigned long unused1[7];
	word pad5;
	short maneuver_squad_index;""",
 """	word pad;
#ifndef HALO_PSP
	unsigned long unused1[7]; /* psp_fixups.py (build 108): not on the PSP (FIXES_108) */
#endif
	word pad5;
	short maneuver_squad_index;"""),
("""	real respawn_time_upper_bound;
	unsigned long unused3[12];
	struct tag_block move_positions;
	struct tag_block starting_locations;
	struct tag_block unused_block;
};""",
 """	real respawn_time_upper_bound;
#ifndef HALO_PSP
	unsigned long unused3[12];
#endif
	struct tag_block move_positions;
	struct tag_block starting_locations;
#ifndef HALO_PSP
	struct tag_block unused_block;
#endif
};"""),
("""	short group_index;
	short cluster_index;
	byte unresolved[4];
	long surface_index;
};""",
 """	short group_index;
	short cluster_index;
#ifndef HALO_PSP
	byte unresolved[4]; /* psp_fixups.py (build 108): not on the PSP (FIXES_108) */
#endif
	long surface_index;
};"""),
]
# the layout checks, at the PSP's sizes and offsets
FIXES_108["source/ai/actor_firing_position.c#layouts"] = [
("""typedef char actor_firing_position_definition_size_assert[
	sizeof(struct firing_position_definition) == 0x18 ? 1 : -1];""",
 """#ifdef HALO_PSP
/* psp_fixups.py (build 108): the PSP's slim layouts (FIXES_108) */
typedef char actor_firing_position_definition_size_assert[
	sizeof(struct firing_position_definition) == 0x14 ? 1 : -1];
#else
typedef char actor_firing_position_definition_size_assert[
	sizeof(struct firing_position_definition) == 0x18 ? 1 : -1];
#endif"""),
("""typedef char actor_firing_position_squad_groups_offset_assert[
	offsetof(struct squad_definition, firing_position_groups) == 0x54 ? 1 : -1];
typedef char actor_firing_position_squad_size_assert[
	sizeof(struct squad_definition) == 0xE8 ? 1 : -1];""",
 """#ifdef HALO_PSP
typedef char actor_firing_position_squad_groups_offset_assert[
	offsetof(struct squad_definition, firing_position_groups) == 0x54 - 28 ? 1 : -1];
typedef char actor_firing_position_squad_size_assert[
	sizeof(struct squad_definition) == 0xE8 - 88 ? 1 : -1];
#else
typedef char actor_firing_position_squad_groups_offset_assert[
	offsetof(struct squad_definition, firing_position_groups) == 0x54 ? 1 : -1];
typedef char actor_firing_position_squad_size_assert[
	sizeof(struct squad_definition) == 0xE8 ? 1 : -1];
#endif"""),
]
# as soon as a map's tags are read, before anything reads them: an older
# bake's blocks in the PSP's layouts
FIXES_108["source/cache/cache_files.c#layouts"] = [
("""				pal_tags_loaded(cache_file_globals.header.build);
			}""",
 """				pal_tags_loaded(cache_file_globals.header.build);
			}
#ifdef HALO_PSP
			{
				/* psp_fixups.py (build 108): FIXES_108 sound_definitions.h#layouts */
				extern void psp_tag_layouts_loaded(void);

				psp_tag_layouts_loaded();
			}
#endif"""),
]
# Memory pools (the objects' 1 MB pool above all) check their whole block
# list on every allocation, free and lookup (memory_pool_verify), and a
# lookup walks the list again to assert the block is in it: with ~1,000
# objects' blocks, each object created or deleted (every projectile, every
# effect) read ~2,000 block headers. The emulator's cache model put 4.9% of
# the tick's data misses there on b30's beach (docs/overnight_b108.md). The
# walks only assert; on the PSP they run with keep word "poolverify"
FIXES_108["source/memory/memory_pool.c#verify"] = [
("""static void memory_pool_verify(
	struct memory_pool *pool)
{
	struct memory_pool_block *block;
	struct memory_pool_block *previous_block;

	match_assert("c:\\\\halo\\\\SOURCE\\\\memory\\\\memory_pool.c", 340, pool->signature==POOL_SIGNATURE);
	match_assert("c:\\\\halo\\\\SOURCE\\\\memory\\\\memory_pool.c", 341, pool->size>0);
""",
 """#ifdef HALO_PSP
int psp_keep(const char *name);
static int psp_pool_verify = -1; /* psp_fixups.py (build 108): FIXES_108 memory_pool.c#verify */
#endif

static void memory_pool_verify(
	struct memory_pool *pool)
{
	struct memory_pool_block *block;
	struct memory_pool_block *previous_block;

	match_assert("c:\\\\halo\\\\SOURCE\\\\memory\\\\memory_pool.c", 340, pool->signature==POOL_SIGNATURE);
	match_assert("c:\\\\halo\\\\SOURCE\\\\memory\\\\memory_pool.c", 341, pool->size>0);
#ifdef HALO_PSP
	if (psp_pool_verify < 0)
		psp_pool_verify = psp_keep("poolverify");
	if (!psp_pool_verify)
		return;
#endif
"""),
("""	for (other_block = pool->first_block; other_block && block != other_block; other_block = other_block->next_block)
	{
	}
	match_assert("c:\\\\halo\\\\SOURCE\\\\memory\\\\memory_pool.c", 388, other_block);""",
 """#ifdef HALO_PSP
	if (psp_pool_verify)
#endif
	{
	for (other_block = pool->first_block; other_block && block != other_block; other_block = other_block->next_block)
	{
	}
	match_assert("c:\\\\halo\\\\SOURCE\\\\memory\\\\memory_pool.c", 388, other_block);
	}"""),
]

# A full data array was searched from its first free index to its end on
# every datum_new that found no room, and again on the next: the AI's props
# with many actors, the effects and particles at their caps (the PSP's are
# lower, psp_capacity.h). datum_new had 1.6% of b30's beach's tick data misses
# (docs/overnight_b108.md). Now a search that finds no room leaves the first
# free index at the end, until a datum is deleted (datum_delete lowers it).
# ChupathingyCE's fix (source/memory/data.c there), as the PSP's
FIXES_108["source/memory/data.c#full"] = [
Either(universal=("""		absolute_index++;
		header = (struct datum_header *)((byte *)header+size);
	}

	return result;
}""",
 """		absolute_index++;
		header = (struct datum_header *)((byte *)header+size);
	}
#ifdef HALO_PSP
	/* psp_fixups.py (build 108): FIXES_108 data.c#full */
	if (result == NONE)
		data->first_free_absolute_index = data->maximum_count;
#endif

	return result;
}"""),
 # ChupathingyCE: the fix is there already; nothing changes
 chupa=("""	if (result == NONE)
		data->first_free_absolute_index = data->maximum_count;

	return result;
}""",
  """	if (result == NONE)
		data->first_free_absolute_index = data->maximum_count;

	return result;
}""")),
]

# The main menu (ui.map) starts its intro movie, and the attract mode its
# movies, through Bink, which the PSP doesn't have (bink_null.c: BinkOpen
# fails). bink_playback_start stole the movie's memory pool from the texture
# cache first: more than the PSP's cache has, so its page count went to zero
# or below (asserts xbox_texture_cache.c#319 and lruv_cache.c#603 in the
# emulator's main menu run), before the failed open gave it back. Now nothing
# is stolen: the movie isn't started, which is where the failed open ended
FIXES_108["source/bink/bink_playback.c#psp"] = [
("""	if (!bink_globals.initialized)
		return;
	if (cache_files_precache_in_progress())
		return;
""",
 """	if (!bink_globals.initialized)
		return;
	if (cache_files_precache_in_progress())
		return;
#ifdef HALO_PSP
	/* psp_fixups.py (build 108): no Bink on the PSP (FIXES_108) */
	platform_psp_log("bink: no movies on the PSP; skipping %s", full_pathname ? full_pathname : "");
	return;
#endif
"""),
]

# the five places the engine reads a permutation's name: messages and debug
# dumps, now with the sound's tag name or nothing (FIXES_108
# sound_definitions.h#layouts above)
FIXES_108["source/cache/xbox_sound_cache.c#permutations"] = [
("""	sprintf(
		xbox_sound_cache_globals.debug_block_name,
		"%s (%s)",
		tag_get_name(cache_sound->sound->runtime_tag_index),
		cache_sound->sound);""",
 """#ifdef HALO_PSP
	/* psp_fixups.py (build 108): permutations carry no names (FIXES_108) */
	sprintf(
		xbox_sound_cache_globals.debug_block_name,
		"%s",
		tag_get_name(cache_sound->sound->runtime_tag_index));
#else
	sprintf(
		xbox_sound_cache_globals.debug_block_name,
		"%s (%s)",
		tag_get_name(cache_sound->sound->runtime_tag_index),
		cache_sound->sound);
#endif"""),
("""			"d:\\\\stabbed.txt",
			sound->name,""",
 """			"d:\\\\stabbed.txt",
#ifdef HALO_PSP
			tag_get_name(sound->runtime_tag_index), /* psp_fixups.py (build 108) */
#else
			sound->name,
#endif"""),
]
FIXES_108["source/sound/sound_definitions.c#permutations"] = [
("""		"but how can you speak if you have no mouth data? (permutation %s)",
		permutation->name);""",
 """		"but how can you speak if you have no mouth data? (permutation %s)",
#ifdef HALO_PSP
		""); /* psp_fixups.py (build 108): permutations carry no names */
#else
		permutation->name);
#endif"""),
]
FIXES_108["source/sound/sound_dsound_xbox.c#permutations"] = [
("""						channel->playing_permutation ? channel->playing_permutation->name : "",
						channel->queued_permutation ? channel->queued_permutation->name : "");""",
 """#ifdef HALO_PSP
						"", ""); /* psp_fixups.py (build 108): permutations carry no names */
#else
						channel->playing_permutation ? channel->playing_permutation->name : "",
						channel->queued_permutation ? channel->queued_permutation->name : "");
#endif"""),
Either(universal=("""					"trying to queue sound %s but it's outside the valid range. (%ld)",
					channel->playing_permutation->name,""",
 """					"trying to queue sound %s but it's outside the valid range. (%ld)",
#ifdef HALO_PSP
					"", /* psp_fixups.py (build 108): permutations carry no names */
#else
					channel->playing_permutation->name,
#endif"""),
 # ChupathingyCE
 chupa=("""					"trying to queue sound %.32s but it's outside the valid range. (%ld)",
					channel->playing_permutation->name,""",
  """					"trying to queue sound %.32s but it's outside the valid range. (%ld)",
#ifdef HALO_PSP
					"", /* psp_fixups.py (build 108): permutations carry no names */
#else
					channel->playing_permutation->name,
#endif""")),
]
# build 109: the marker lookups remembered. A unit's weapon, an AI's aim, an
# effect's attachment each look their markers up by name every tick
# (model_find_marker: a binary search by _stricmp through the model's markers);
# in b30's beach the lookups had 2.5% of the tick's misses, instructions and
# data alike (b108l, docs/overnight_b108.md). The answer for a model and a
# name pointer is kept in a small table when the name can't change under it:
# a string constant (the program's read-only data) or a tag's string (the
# tags' part of the tag window, not the BSP's, until the next map:
# psp_model_markers_forget, as the tags load).
# Names in any other memory (a buffer built at run time) search as before.
# "markersearch" (keep.txt): every lookup searches
FIXES_109 = {}
FIXES_109["source/models/models.c#markers"] = [
("""short model_find_marker(
	long model_index,
	char const *name)
{
	if (model_index != NONE && name && *name)
	{""",
 """#ifdef HALO_PSP
/* psp_fixups.py (build 109): see FIXES_109 models.c#markers */
#define PSP_MARKER_MEMO 256
#define PSP_TAG_WINDOW_BASE 0x09600000UL /* physical_memory_map.c's TAG_CACHE_BASE_ADDRESS on the PSP */
int psp_keep(const char *name);

static struct
{
	long model_index;
	char const *name;
	short marker_index;
	short pad;
} psp_marker_memo[PSP_MARKER_MEMO];
static int psp_marker_search = -1;
static unsigned long psp_tag_data_end = 0; /* the tags' end in the window: the BSP after it changes */
unsigned long psp_marker_memo_hits = 0, psp_marker_memo_searches = 0;

static short psp_model_find_marker_search(
	long model_index,
	char const *name);

/* a map's tags were read: the tag window's names and the models are new;
tag_data_end: where its tags end (a structure BSP is loaded after them) */
void psp_model_markers_forget(
	unsigned long tag_data_end)
{
	csmemset(psp_marker_memo, 0, sizeof(psp_marker_memo));
	psp_tag_data_end = tag_data_end;
}

short model_find_marker(
	long model_index,
	char const *name)
{
	extern char _etext[], _fdata[];
	unsigned long at = (unsigned long)name;
	unsigned int slot;
	short marker_index;

	if (psp_marker_search < 0)
		psp_marker_search = psp_keep("markersearch");
	if (psp_marker_search || model_index == NONE || !name ||
		!((at >= (unsigned long)_etext && at < (unsigned long)_fdata) ||
			(at >= PSP_TAG_WINDOW_BASE && at < psp_tag_data_end)))
	{
		psp_marker_memo_searches++;
		return psp_model_find_marker_search(model_index, name);
	}
	slot = ((at >> 2) ^ ((unsigned long)model_index * 2654435761UL >> 7)) & (PSP_MARKER_MEMO - 1);
	if (psp_marker_memo[slot].name == name && psp_marker_memo[slot].model_index == model_index)
	{
		psp_marker_memo_hits++;
		return psp_marker_memo[slot].marker_index;
	}
	psp_marker_memo_searches++;
	marker_index = psp_model_find_marker_search(model_index, name);
	psp_marker_memo[slot].model_index = model_index;
	psp_marker_memo[slot].name = name;
	psp_marker_memo[slot].marker_index = marker_index;
	return marker_index;
}

static short psp_model_find_marker_search(
#else
short model_find_marker(
#endif
	long model_index,
	char const *name)
{
	if (model_index != NONE && name && *name)
	{"""),
]
# the marker table forgotten with the rest, as a map's tags load
FIXES_109["source/cache/cache_files.c#markers"] = [
("""				psp_tag_layouts_loaded();
			}
#endif""",
 """				psp_tag_layouts_loaded();
			}
			{
				/* psp_fixups.py (build 109): FIXES_109 models.c#markers */
				extern void psp_model_markers_forget(unsigned long tag_data_end);

				psp_model_markers_forget((unsigned long)tag_cache_base_address + cache_file_globals.header.tag_data_size);
			}
#endif"""),
]

# ---------- build 110: static memory the campaign never uses, on either base
# The rebase onto ChupathingyCE costs heap (its desktop features' code and
# tables: docs/chupa_rebase.md), so these come back first. Both are debug
# displays the PSP never shows, kept working at a smaller size:
# - the profile graph's table of values: 64 of 524 bytes each (33.5 KB of
#   initialised data), of which the game fills 14; 16 here. Drawing the graph
#   also put 64 x 512 points on the stack (128 KB, more than a PSP thread's);
# - the collision spray (collision_debug_spray): 40 x 30 rays a frame, their
#   points and normals kept (28 KB); 8 x 6 here
FIXES_110 = {}
FIXES_110["source/interface/interface.c#values"] = [
("""	MAXIMUM_PROFILE_VALUES = 64,""",
 """#ifdef HALO_PSP
	/* psp_fixups.py (build 110): the game fills 14 */
	MAXIMUM_PROFILE_VALUES = 16,
#else
	MAXIMUM_PROFILE_VALUES = 64,
#endif"""),
]
FIXES_110["source/physics/collision_debug.c#spray"] = [
("""	COLLISION_DEBUG_SPRAY_COLUMNS = 40,
	COLLISION_DEBUG_SPRAY_ROWS = 30,""",
 """#ifdef HALO_PSP
	/* psp_fixups.py (build 110): a debug display the PSP never shows */
	COLLISION_DEBUG_SPRAY_COLUMNS = 8,
	COLLISION_DEBUG_SPRAY_ROWS = 6,
#else
	COLLISION_DEBUG_SPRAY_COLUMNS = 40,
	COLLISION_DEBUG_SPRAY_ROWS = 30,
#endif"""),
]

# ---------- build 112: the ceiling bench's switches (psp_platform.c,
# "ceilingbench"; the bits are psp/include/psp_bench.h's). Each wraps one big
# part of the tick or the frame in a test of psp_bench_skip, which is 0
# unless the bench is running, so the game is unchanged otherwise
FIXES_112 = {}
FIXES_112["source/game/game.c#bench"] = [
("""#include "units/vehicles.h"
""",
 """#include "units/vehicles.h"
#ifdef HALO_PSP
#include "psp_bench.h" /* the ceiling bench's switches (psp_fixups.py, build 112) */
#endif
"""),
("""	if (!network_game_distributed_client())
		ai_update();
	else
		network_actors_drive();
""",
 """	if (!network_game_distributed_client())
	{
#ifdef HALO_PSP
		if (!(psp_bench_skip & PSP_SKIP_AI))
#endif
		ai_update();
	}
	else
		network_actors_drive();
"""),
("""	effects_update(seconds_per_tick);
""",
 """#ifdef HALO_PSP
	if (!(psp_bench_skip & PSP_SKIP_EFFECTS))
#endif
	effects_update(seconds_per_tick);
"""),
("""	recorded_animations_update();
	objects_update();
""",
 """	recorded_animations_update();
#ifdef HALO_PSP
	if (!(psp_bench_skip & PSP_SKIP_OBJECTS))
#endif
	objects_update();
"""),
("""	particles_update(frame_dt);
""",
 """#ifdef HALO_PSP
	if (!(psp_bench_skip & PSP_SKIP_EFFECTS))
#endif
	particles_update(frame_dt);
"""),
]
# build 112: the tick-by-tick comparison (docs/big_levers.md): "statecrc"
# hashes the game state after every tick (game_state.c), and "fixedticks"
# makes every frame two ticks and a fifteenth of a second of game time, so
# runs of different builds play the same way
FIXES_112["source/game/game.c#statecrc"] = [
("""	collision_log_end_period();
	profile_tick_end();""",
 """#ifdef HALO_PSP
	{
		extern void psp_game_state_hash_tick(long tick); /* game_state.c (psp_fixups.py, build 112) */

		psp_game_state_hash_tick(game_time_get());
	}
#endif
	collision_log_end_period();
	profile_tick_end();"""),
]
FIXES_112["source/objects/objects.c#statecrc"] = [
("void objects_update(\n\tvoid)\n{",
 """#ifdef HALO_PSP
/* build 112: statecrc's hash of what the game plays with (game_state.c): each
object's kind, place, motion, facing, health, parent and animation frame,
leaving out what drawing writes into objects (render flags, the render state
cache, node matrices), so two runs compare on the game alone (psp_fixups.py) */
static int psp_object_play_words(long index, struct object_datum const *object, unsigned long *words)
{
	int count = 0;

	words[count++] = (unsigned long)index;
	words[count++] = (unsigned long)object->definition_index;
	words[count++] = (unsigned long)object->object.type;
	/* position, translational velocity, forward, up, angular velocity */
	csmemcpy(&words[count], &object->object.position, 15 * sizeof(real));
	count += 15;
	csmemcpy(&words[count], &object->object.body_vitality, 2 * sizeof(real));
	count += 2;
	words[count++] = (unsigned long)object->object.parent_object_index;
	words[count++] = ((unsigned long)(unsigned short)object->object.animation.state.index << 16) |
		(unsigned short)object->object.animation.state.frame_index;
	return count;
}

unsigned long psp_objects_play_hash(void)
{
	struct object_iterator iterator;
	struct object_datum *object;
	unsigned long hash = 2166136261UL;

	object_iterator_new(&iterator, _object_mask_all, 0);
	while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
	{
		unsigned long words[24];
		int count = psp_object_play_words(iterator.index, object, words), k;

		for (k = 0; k < count; k++)
			hash = (hash ^ words[k]) * 16777619UL;
	}
	return hash;
}

/* build 113: "stateobjectsN" (with statecrc; e.g. stateobjects1300): for 48 ticks from tick N, each
object's own play hash, so two runs that part ways at a tick can be compared
object by object (tools/statecmp.py --objects). Each definition is named once,
the first time it appears in a dump */
void platform_psp_log(const char *format, ...);
char *tag_get_name(long tag_index);
void psp_objects_play_dump(long tick)
{
	static long first = -2, watched = -1, whole = -1;
	static long named[256];
	static int named_count = 0;
	struct object_iterator iterator;
	struct object_datum *object;
	char line[512];
	int length = 0;

	if (first == -2)
	{
		extern int psp_keep_number(const char *prefix, int fallback); /* psp_platform.c */

		first = psp_keep_number("stateobjects", -1);
		if (first >= 0)
			platform_psp_log("state: each object's play hash for ticks %ld to %ld (stateobjects%ld)", first, first + 47, first);
		/* build 114: "statewordsN": object N's (its slot, the identifier's
		low half) play words too, to see which part first (#25);
		"statewordsall": every object's */
		watched = psp_keep("statewordsall") ? -2 : psp_keep_number("statewords", -1);
		/* "statefullN": object N's whole datum, every word, 32 a line (the
		play words leave out what drives them: a unit's desired facing, its
		AI's target...) */
		whole = psp_keep_number("statefull", -1);
	}
	if (first < 0 || tick < first || tick >= first + 48)
		return;
	object_iterator_new(&iterator, _object_mask_all, 0);
	while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
	{
		unsigned long words[24], hash = 2166136261UL;
		int count = psp_object_play_words(iterator.index, object, words), k;

		for (k = 0; k < count; k++)
			hash = (hash ^ words[k]) * 16777619UL;
		for (k = 0; k < named_count && named[k] != object->definition_index; k++)
			;
		if (k == named_count && named_count < 256)
		{
			named[named_count++] = object->definition_index;
			platform_psp_log("state: definition %08lx is %.100s", (unsigned long)object->definition_index,
				tag_get_name(object->definition_index));
		}
		if (length > 400)
		{
			platform_psp_log("state: objects at tick %ld:%s", tick, line);
			length = 0;
		}
		length += snprintf(line + length, sizeof(line) - length, " %08lx %08lx %08lx", (unsigned long)iterator.index,
			(unsigned long)object->definition_index, hash);
		if (watched == -2 || (watched >= 0 && (iterator.index & 0xFFFF) == watched))
		{
			char text[24 * 9 + 1];
			int at = 0;

			for (k = 0; k < count; k++)
				at += snprintf(text + at, sizeof(text) - at, " %08lx", words[k]);
			platform_psp_log("state: words of object %08lx at tick %ld:%s", (unsigned long)iterator.index, tick, text);
		}
		if (whole >= 0 && (iterator.index & 0xFFFF) == whole)
		{
			struct object_header_datum const *header = object_header_get(iterator.index);
			unsigned long const *datum = (unsigned long const *)header->datum;
			long total = header->data_size / 4, from;

			for (from = 0; from < total; from += 32)
			{
				char text[32 * 9 + 1];
				int at = 0;
				long w;

				for (w = from; w < total && w < from + 32; w++)
					at += snprintf(text + at, sizeof(text) - at, " %08lx", datum[w]);
				platform_psp_log("state: datum of object %08lx at tick %ld, word %ld:%s", (unsigned long)iterator.index,
					tick, from, text);
			}
		}
	}
	if (length)
		platform_psp_log("state: objects at tick %ld:%s", tick, line);
}
#endif

void objects_update(
	void)
{"""),
]
FIXES_112["source/main/main.c#fixedticks"] = [
("""	main_globals.frame_start_milliseconds = end_milliseconds;
	main_globals.rasterizer_target_index = target_index;
	main_globals.seconds_elapsed = seconds_elapsed;""",
 """	main_globals.frame_start_milliseconds = end_milliseconds;
	main_globals.rasterizer_target_index = target_index;
#ifdef HALO_PSP
	/* build 112: "fixedticks": every frame two ticks and a fifteenth of a
	second, whatever the clock says, as the game's own film recorder steps.
	With no input the game then plays the same way however fast a build
	runs, so runs compare tick by tick ("statecrc", game_state.c). Tests
	only: game time no longer follows the clock (psp_fixups.py) */
	{
		extern int psp_keep(const char *name);
		static int fixed = -1;

		if (fixed < 0)
			fixed = psp_keep("fixedticks");
		if (fixed && main_globals.connection == _game_connection_local && !main_globals.movie)
			seconds_elapsed = 0.06666667014360428f;
	}
#endif
	main_globals.seconds_elapsed = seconds_elapsed;"""),
]
FIXES_112["source/render/render.c#bench"] = [
("""#include "structures/structures.h"
""",
 """#include "structures/structures.h"
#ifdef HALO_PSP
#include "psp_bench.h" /* the ceiling bench's switches (psp_fixups.py, build 112) */
#endif
"""),
("""		render_objects();
""",
 """#ifdef HALO_PSP
		if (!(psp_bench_skip & PSP_SKIP_MODELS))
#endif
		render_objects();
"""),
("""		render_object_shadows();
""",
 """#ifdef HALO_PSP
		if (!(psp_bench_skip & PSP_SKIP_MODELS))
#endif
		render_object_shadows();
"""),
("""		structure_render_lightmaps();
""",
 """#ifdef HALO_PSP
		if (!(psp_bench_skip & PSP_SKIP_LEVEL))
#endif
		structure_render_lightmaps();
"""),
("""		structure_render_transparent_geometry();
""",
 """#ifdef HALO_PSP
		if (!(psp_bench_skip & PSP_SKIP_LEVEL))
#endif
		structure_render_transparent_geometry();
"""),
("""		render_particles();
""",
 """#ifdef HALO_PSP
		if (!(psp_bench_skip & PSP_SKIP_EFFECTS))
#endif
		render_particles();
"""),
]

# ---------- ChupathingyCE only (the rebase)
# Changes only ChupathingyCE's own code needs: what it added since its fork
# that reads a field the PSP's slim layouts leave out, or allocates what the
# PSP's heap can't hold. Each is Either(chupa=...): halo-ce-universal has
# nothing to change. docs/chupa_rebase.md has the rebase as a whole
FIXES_CHUPA = {}
# ChupathingyCE reads a saved game into a second game state and takes it
# once game_state_image_accept has checked it (pools, lists and caches as
# they were made: a damaged save is refused, not run). That second game state
# is 3.3 MB, half the PSP's heap; malloc fails, and the persistent storage
# read would then delete the save. On the PSP the image is the game state
# itself: read in place, checked in place, and not copied onto itself
FIXES_CHUPA["source/saved games/game_state_xbox.c#image"] = [
Either(chupa=("""boolean game_state_read_from_file(
	void)
{""",
 """#ifdef HALO_PSP
/* psp_fixups.py (FIXES_CHUPA): no room on the PSP's heap for a second game
state (3.3 MB), so a saved game is read in place and checked there. One the
check refuses has already been read over the game state, as every saved
game was before ChupathingyCE's check */
#define GAME_STATE_IMAGE_NEW(place, size) ((void)(size), (void *)(place))
#define GAME_STATE_IMAGE_DELETE(image) ((void)(image))
#else
#define GAME_STATE_IMAGE_NEW(place, size) ((void)(place), malloc(size))
#define GAME_STATE_IMAGE_DELETE(image) free(image)
#endif

boolean game_state_read_from_file(
	void)
{""")),
Either(chupa=("""	free(image);

	return result;""",
 """	GAME_STATE_IMAGE_DELETE(image);

	return result;""")),
# the core load (core_load)
Either(chupa=("""	void *image = malloc(buffer_size);

	sprintf(path,""",
 """	void *image = GAME_STATE_IMAGE_NEW(buffer, buffer_size);

	sprintf(path,""")),
Either(chupa=("""	CloseHandle(file);
	free(image);

	return;""",
 """	CloseHandle(file);
	GAME_STATE_IMAGE_DELETE(image);

	return;""")),
# the saved game the main menu resumes
Either(chupa=("""		void *image = malloc(buffer_size);

		if (!image ||""",
 """		void *image = GAME_STATE_IMAGE_NEW(buffer, buffer_size);

		if (!image ||""")),
Either(chupa=("""		(void)buffer;
		free(image);
		CloseHandle(file);""",
 """		(void)buffer;
		GAME_STATE_IMAGE_DELETE(image);
		CloseHandle(file);""")),
]
FIXES_CHUPA["source/saved games/game_state.c#image"] = [
Either(chupa=("""	csmemcpy(game_state_globals.base_address, image, size);
	error(_error_silent, "the saved game is taken");""",
 """	/* (the PSP reads it in place: psp_fixups.py, FIXES_CHUPA) */
	if ((void *)image != (void *)game_state_globals.base_address)
		csmemcpy(game_state_globals.base_address, image, size);
	error(_error_silent, "the saved game is taken");""")),
]
# ChupathingyCE's loader checks a map's tags and BSPs against the tag
# window's size in physical_memory_map.h (cache_files.c, and the tag schema
# check port/linux/game/tag_validate.c)
FIXES_CHUPA["source/cache/physical_memory_map.h#window"] = [
Either(chupa=("""#define TAG_CACHE_SIZE 0x1600000""",
 """#ifdef HALO_PSP
/* the PSP's tag window (psp_capacity.h; psp_fixups.py) */
#define TAG_CACHE_SIZE PSP_TAG_CACHE_SIZE
#else
#define TAG_CACHE_SIZE 0x1600000
#endif""")),
]
# ChupathingyCE names a code address with the dynamic loader's dladdr
# (posix_describe_address, for the game's stack dump); the PSP has none
FIXES_CHUPA["port/linux/src/posix_files.c#dladdr"] = [
Either(chupa=("""#include <dlfcn.h>
#include <stdio.h>
""",
 """#include <stdio.h>
#ifdef HALO_PSP
/* psp_fixups.py (FIXES_CHUPA): no dynamic loader on the PSP, so the address
alone (psp_crash.c names the PSP's crashes from the link map) */
void posix_describe_address(void *address, char *buffer, posix_ulong size)
{
	snprintf(buffer, size, "%p", address);
}
#else
#include <dlfcn.h>
""")),
Either(chupa=("""		snprintf(buffer, size, "%p ?????", address);
	}
}""",
 """		snprintf(buffer, size, "%p ?????", address);
	}
}
#endif""")),
]
# ChupathingyCE's static memory, sized for a desktop, from what the PSP
# keeps: the program's static memory comes off the heap's end (psp_build.py
# sizes the heap to what is left), and ChupathingyCE took 298 KB more of it
# than halo-ce-universal. Each is the PSP's size of the same thing:
# - the script check's walk: its 76 KB stack only while it walks, which it
#   does only for a map whose scripts are damaged (never a shipped map's);
# - the empty tag data an index that isn't one reads: 8 KB (the largest
#   tag root is the scenario's, 1,456 bytes), not 64, and 8 to zero again
#   each time, not 64;
# - a multiplayer gametype's vehicle homes: the Xbox maps' 80, not 1,024;
# - the clusters' lights kept for lighting objects: 448, not the light
#   partition's 2,048 references;
# - the game state's allocations kept for checking a saved game: 128, not 256;
# - desktop OpenGL's sampler objects and the texture lookups remembered:
#   the null renderer has no samplers; the lookups halo-ce-universal's 64
FIXES_CHUPA["source/hs/hs.c#walk"] = [
Either(chupa=("""static short hs_expression_refusal(
	long root_expression_index,
	char const **name)
{
	static long stack[MAXIMUM_HS_SYNTAX_NODES_PER_SCENARIO+1];
	long stack_count = 0;
""",
 """#ifdef HALO_PSP
/* psp_fixups.py (FIXES_CHUPA): the walk's stack (76 KB) from the heap, only
while it walks (only a map whose scripts are damaged is walked). With no
room the expression is run unchecked, as halo-ce-universal runs every one */
#include <stdlib.h>

static short hs_expression_refusal_walk(
	long root_expression_index,
	char const **name,
	long *stack);

static short hs_expression_refusal(
	long root_expression_index,
	char const **name)
{
	long *stack = malloc((MAXIMUM_HS_SYNTAX_NODES_PER_SCENARIO+1) * sizeof(long));
	short refusal = _hs_node_refusal_none;

	*name = NULL;
	if (stack)
	{
		refusal = hs_expression_refusal_walk(root_expression_index, name, stack);
		free(stack);
	}
	else
	{
		error(_error_silent, "no memory to check a script's expression: it runs unchecked");
	}

	return refusal;
}

static short hs_expression_refusal_walk(
	long root_expression_index,
	char const **name,
	long *stack)
{
#else
static short hs_expression_refusal(
	long root_expression_index,
	char const **name)
{
	static long stack[MAXIMUM_HS_SYNTAX_NODES_PER_SCENARIO+1];
#endif
	long stack_count = 0;
""")),
]
FIXES_CHUPA["source/tag_files/tag_groups.c#empty"] = [
Either(chupa=("""	TAG_EMPTY_DATA_SIZE = 0x10000,""",
 """#ifdef HALO_PSP
	/* (psp_fixups.py, FIXES_CHUPA: the largest tag root is the scenario's,
	1,456 bytes) */
	TAG_EMPTY_DATA_SIZE = 0x2000,
#else
	TAG_EMPTY_DATA_SIZE = 0x10000,
#endif""")),
]
FIXES_CHUPA["source/game/game_engine.c#homes"] = [
Either(chupa=("""	MAXIMUM_VEHICLE_HOMES = 1024""",
 """#ifdef HALO_PSP
	/* (psp_fixups.py, FIXES_CHUPA: the Xbox maps' most) */
	MAXIMUM_VEHICLE_HOMES = 80
#else
	MAXIMUM_VEHICLE_HOMES = 1024
#endif""")),
]
FIXES_CHUPA["source/objects/object_lights.c#clusters"] = [
Either(chupa=("""	LIGHTS_PORT_MAXIMUM_ENTRIES = HALO_PORT_MAXIMUM_CLUSTER_REFERENCES,""",
 """#ifdef HALO_PSP
	/* (psp_fixups.py, FIXES_CHUPA: b30 peaks at 294 of the light
	partition's references [E]; past this many a cluster's lights are
	walked as before) */
	LIGHTS_PORT_MAXIMUM_ENTRIES = 448,
#else
	LIGHTS_PORT_MAXIMUM_ENTRIES = HALO_PORT_MAXIMUM_CLUSTER_REFERENCES,
#endif""")),
]
FIXES_CHUPA["source/saved games/game_state.c#allocations"] = [
Either(chupa=("""	MAXIMUM_GAME_STATE_ALLOCATIONS = 256,""",
 """#ifdef HALO_PSP
	/* (psp_fixups.py, FIXES_CHUPA: the game state's allocations a map
	makes are 81 or fewer, the pools among them fewer still [E]) */
	MAXIMUM_GAME_STATE_ALLOCATIONS = 128,
#else
	MAXIMUM_GAME_STATE_ALLOCATIONS = 256,
#endif""")),
]
FIXES_CHUPA["port/linux/src/d3d8_gl.c#samplers"] = [
Either(chupa=("""#define SAMPLER_CACHE_SIZE 512""",
 """#ifdef HALO_PSP
/* (psp_fixups.py, FIXES_CHUPA: the null renderer makes none) */
#define SAMPLER_CACHE_SIZE 16
#else
#define SAMPLER_CACHE_SIZE 512
#endif""")),
]
FIXES_CHUPA["port/linux/src/xbox_textures.c#recent"] = [
Either(chupa=("""#define RECENT_TEXTURE_COUNT 512""",
 """#ifdef HALO_PSP
/* (psp_fixups.py, FIXES_CHUPA: halo-ce-universal's count) */
#define RECENT_TEXTURE_COUNT 64
#else
#define RECENT_TEXTURE_COUNT 512
#endif""")),
]
# ChupathingyCE widens the overshield's and the camouflage's render spheres
# to their meshes as a map loads, reading the meshes through the tags'
# vertex buffers. The PSP's bake keeps model geometry in the geometry pack
# (psp_geo.c), so those buffers point at nothing: b30 crashed reading
# outside RAM as it loaded [E]. Their spheres stay the tags', as on the Xbox
FIXES_CHUPA["source/cache/cache_files.c#powerups"] = [
Either(chupa=("""			/* The two powerups' authored render spheres can be smaller than
			 * their rigid meshes. Derive their bounds while tags are writable. */
			models_fix_powerup_render_bounds();""",
 """#ifndef HALO_PSP
			/* The two powerups' authored render spheres can be smaller than
			 * their rigid meshes. Derive their bounds while tags are writable. */
			models_fix_powerup_render_bounds();
#else
			/* psp_fixups.py (FIXES_CHUPA): the powerups' meshes are in the
			geometry pack, not behind the tags' vertex buffers this reads:
			their render spheres stay the tags', as on the Xbox */
#endif""")),
]
# ChupathingyCE's heap use at run time, which the static memory above doesn't
# show: b30's beach took about 900 KB more heap than halo-ce-universal's [E].
# The two largest are for desktop features the PSP leaves out:
# - the widgets' memory pool: 256 KB for the PC menus' many widgets; the
#   Xbox's menus need the Xbox's 16 KB;
# - the high-resolution text's atlas: a 256 x 256 bitmap and its texture
#   (256 KB) standing for text drawn by the desktop's renderer, which the
#   PSP has none of (psp_chupa_platform.c: no high-resolution font)
FIXES_CHUPA["source/interface/ui_widget.c#pool"] = [
Either(chupa=("""	WIDGET_MEMORY_POOL_SIZE = 0x40000,""",
 """#ifdef HALO_PSP
	/* (psp_fixups.py, FIXES_CHUPA: the Xbox's, for the Xbox's menus) */
	WIDGET_MEMORY_POOL_SIZE = 0x4000,
#else
	WIDGET_MEMORY_POOL_SIZE = 0x40000,
#endif""")),
]
FIXES_CHUPA["source/rasterizer/rasterizer_text.c#atlas"] = [
Either(chupa=("""			/* port: the high-res text atlas's placeholder */
			hires_text_atlas = bitmap_2d_new(""",
 """#ifndef HALO_PSP
			/* port: the high-res text atlas's placeholder */
			hires_text_atlas = bitmap_2d_new(""")),
Either(chupa=("""					HIRES_TEXT_ATLAS_BITMAP_SIZE,
					HIRES_TEXT_ATLAS_BITMAP_SIZE);
			}""",
 """					HIRES_TEXT_ATLAS_BITMAP_SIZE,
					HIRES_TEXT_ATLAS_BITMAP_SIZE);
			}
#else
			/* (psp_fixups.py, FIXES_CHUPA: no high-resolution text on the
			PSP, so no atlas: 256 KB of heap) */
#endif""")),
]
# Trusted maps. ChupathingyCE checks a map's data each time the tick reads it
# (each animation keyframe against its data's size, each node a collision
# walk enters against the tree's size and depth), so that a damaged or
# hand-made map can't send the game outside the data. On b30's beach they
# were 1-2% of the tick's instructions [E], and they make the hot code
# bigger (docs/chupa_rebase.md). The PSP's maps are
# its own bake of Sheepdog01's disc, and the retail BSPs and animations are sound
# (ChupathingyCE's own notes say so), so the PSP reads them as
# halo-ce-universal does: these checks answer TRUE and the compiler drops
# them. PSP_MAP_CHECKS=1 (psp_build.py) builds them in, for a map that isn't
# the bake's or a comparison
FIXES_CHUPA["source/physics/collision_bsp.c#trusted"] = [
Either(chupa=("""	long *nodes_left)
{
	if (node_index < bsp->bsp3d.nodes.count &&""",
 """	long *nodes_left)
{
#if defined(HALO_PSP) && !defined(PSP_MAP_CHECKS)
	/* psp_fixups.py (FIXES_CHUPA): a trusted map (see #trusted) */
	return TRUE;
#endif
	if (node_index < bsp->bsp3d.nodes.count &&""")),
Either(chupa=("""	long *nodes_left)
{
	if (node_index < bsp->bsp2d.nodes.count &&""",
 """	long *nodes_left)
{
#if defined(HALO_PSP) && !defined(PSP_MAP_CHECKS)
	/* psp_fixups.py (FIXES_CHUPA): a trusted map (see #trusted) */
	return TRUE;
#endif
	if (node_index < bsp->bsp2d.nodes.count &&""")),
Either(chupa=("""	struct collision_leaf const *leaf;

	if (leaf_index >= 0 && leaf_index < bsp->leaves.count)""",
 """	struct collision_leaf const *leaf;

#if defined(HALO_PSP) && !defined(PSP_MAP_CHECKS)
	/* psp_fixups.py (FIXES_CHUPA): a trusted map (see #trusted) */
	return TRUE;
#endif
	if (leaf_index >= 0 && leaf_index < bsp->leaves.count)""")),
]
# (and a surface's ring of edges: checked at every edge of every surface a
# walk tests, from several files, so on the PSP the header makes it TRUE where
# it is called; the definition's name is in parentheses so the macro leaves
# it alone)
FIXES_CHUPA["source/physics/collision_bsp.h#trusted"] = [
Either(chupa=("""boolean collision_surface_edge_ring_continues(
	struct collision_bsp const *bsp,
	long edge_index,
	short edge_count);
""",
 """boolean collision_surface_edge_ring_continues(
	struct collision_bsp const *bsp,
	long edge_index,
	short edge_count);
#if defined(HALO_PSP) && !defined(PSP_MAP_CHECKS)
/* psp_fixups.py (FIXES_CHUPA): a trusted map's rings close (collision_bsp.c#trusted) */
#define collision_surface_edge_ring_continues(bsp, edge_index, edge_count) \\
	((void)(bsp), (void)(edge_index), (void)(edge_count), TRUE)
#endif
""")),
]
FIXES_CHUPA["source/physics/collision_bsp.c#ring"] = [
Either(chupa=("""boolean collision_surface_edge_ring_continues(
	struct collision_bsp const *bsp,
	long edge_index,
	short edge_count)
{""",
 """boolean (collision_surface_edge_ring_continues)(
	struct collision_bsp const *bsp,
	long edge_index,
	short edge_count)
{""")),
]
FIXES_CHUPA["source/models/model_animations.c#trusted"] = [
Either(chupa=("""	long size = animation->data.size;

	if (offset<0 || offset>size)""",
 """	long size = animation->data.size;

#if defined(HALO_PSP) && !defined(PSP_MAP_CHECKS)
	/* psp_fixups.py (FIXES_CHUPA): a trusted map (see #trusted) */
	return TRUE;
#endif
	if (offset<0 || offset>size)""")),
Either(chupa=("""	long frame_offset = animation_frame_data_offset(animation, frame_index);
	short node_count = animation->node_count;
	char const *problem = NULL;
""",
 """	long frame_offset = animation_frame_data_offset(animation, frame_index);
	short node_count = animation->node_count;
	char const *problem = NULL;

#if defined(HALO_PSP) && !defined(PSP_MAP_CHECKS)
	/* psp_fixups.py (FIXES_CHUPA): a trusted map (see #trusted) */
	return TRUE;
#endif
""")),
]
# ChupathingyCE's checks of a sound's map data name the pitch range or the
# permutation; on the PSP they carry no names (FIXES_108
# sound_definitions.h#layouts)
FIXES_CHUPA["source/sound/sound_definitions.c#names"] = [
Either(chupa=("""			"pitch range %s picks from %d of its %d permutations",
			range->name,""",
 """			"pitch range %s picks from %d of its %d permutations",
#ifdef HALO_PSP
			"", /* psp_fixups.py (FIXES_CHUPA): pitch ranges carry no names */
#else
			range->name,
#endif""")),
Either(chupa=("""			"pitch range %s forces permutation %d of %d",
			range->name,""",
 """			"pitch range %s forces permutation %d of %d",
#ifdef HALO_PSP
			"", /* psp_fixups.py (FIXES_CHUPA): pitch ranges carry no names */
#else
			range->name,
#endif""")),
Either(chupa=("""				"permutation %s is followed by permutation %d of %d",
				permutation->name,""",
 """				"permutation %s is followed by permutation %d of %d",
#ifdef HALO_PSP
				"", /* psp_fixups.py (FIXES_CHUPA): permutations carry no names */
#else
				permutation->name,
#endif""")),
]
FIXES_CHUPA["source/cache/xbox_sound_cache.c#names"] = [
Either(chupa=("""					"sound permutation %.32s names cache block %08x (not its own)",
					sound->name,""",
 """					"sound permutation %.32s names cache block %08x (not its own)",
#ifdef HALO_PSP
					"", /* psp_fixups.py (FIXES_CHUPA): permutations carry no names */
#else
					sound->name,
#endif""")),
Either(chupa=("""				"sound permutation %.32s of %d bytes not loaded",
				sound->name,""",
 """#ifdef HALO_PSP
				/* psp_fixups.py (FIXES_CHUPA): permutations carry no names; the
				sound's tag is named, as in the cache's other messages */
				"a permutation of %s of %d bytes not loaded",
				tag_get_name(sound->runtime_tag_index),
#else
				"sound permutation %.32s of %d bytes not loaded",
				sound->name,
#endif""")),
]
# Build 112: halo-ce-universal's upstream patch 0002 (cold or compiled-out
# asserts, float rounding) never reached ChupathingyCE. psp_build.py passes
# -DPSP_ASSERTS_COLD=1 and -freorder-blocks-and-partition, but cseries.h had
# no branch for them: b110's and b111's link maps hold 3 .text.unlikely
# sections against 2,981 on the old base, so every failed assert's call sat
# in the middle of the hot code it guards, in the 16 KB instruction cache
# that two thirds of the misses come from (optimization_ranking.md, E1). And
# fast_ftol rounded through a double: on the PSP, whose FPU has no double
# operations, three library calls (to double, rint, to long) for every
# animation frame index, perception test and HUD number (libm's rint was
# linked for it). A float rounds to the nearest integer exactly in float, so
# rintf gives the same bits
FIXES_CHUPA["source/cseries/cseries.h#asserts"] = [
Either(chupa=("""#define match_assert(file, line, expr) if (!(expr)) { release_assert_failed(#expr, MATCH_FILE(file), MATCH_LINE(line), TRUE); }
#define match_vassert(file, line, expr, string) if (!(expr)) { release_assert_failed(RELEASE_ASSERT_MESSAGE(string), MATCH_FILE(file), MATCH_LINE(line), TRUE); }
#define match_warn(file, line, expr) if (!(expr)) { release_assert_failed(#expr, MATCH_FILE(file), MATCH_LINE(line), FALSE); }""",
 """/* psp_fixups.py (FIXES_CHUPA, build 112): PSP_ASSERTS_OFF compiles the
fatal asserts out; PSP_ASSERTS_COLD marks them unlikely, so their calls go to
.text.unlikely, away from the code they guard */
#if defined(PSP_ASSERTS_OFF)
#define match_assert(file, line, expr) if (0 && (expr)) { }
#define match_vassert(file, line, expr, string) if (0 && (expr)) { }
#elif defined(PSP_ASSERTS_COLD)
#define match_assert(file, line, expr) if (__builtin_expect(!(expr), 0)) { release_assert_failed(#expr, MATCH_FILE(file), MATCH_LINE(line), TRUE); }
#define match_vassert(file, line, expr, string) if (__builtin_expect(!(expr), 0)) { release_assert_failed(RELEASE_ASSERT_MESSAGE(string), MATCH_FILE(file), MATCH_LINE(line), TRUE); }
#else
#define match_assert(file, line, expr) if (!(expr)) { release_assert_failed(#expr, MATCH_FILE(file), MATCH_LINE(line), TRUE); }
#define match_vassert(file, line, expr, string) if (!(expr)) { release_assert_failed(RELEASE_ASSERT_MESSAGE(string), MATCH_FILE(file), MATCH_LINE(line), TRUE); }
#endif
#define match_warn(file, line, expr) if (!(expr)) { release_assert_failed(#expr, MATCH_FILE(file), MATCH_LINE(line), FALSE); }""")),
Either(chupa=("""#ifdef HALO_RELEASE
void release_assert_failed(char const *information, char const *file, long line, boolean fatal);
#endif""",
 """#ifdef HALO_RELEASE
void release_assert_failed(char const *information, char const *file, long line, boolean fatal)
#if defined(PSP_ASSERTS_COLD)
	__attribute__((cold)) /* psp_fixups.py (FIXES_CHUPA, build 112) */
#endif
	;
#endif""")),
Either(chupa=("""	/* FISTP: round to nearest under the default control word */
	result = (long)__builtin_rint((double)value);""",
 """	/* FISTP: round to nearest under the default control word.
	psp_fixups.py (FIXES_CHUPA, build 112): rounded in float. A float is
	exact in a double and its nearest integer is exact in a float, so the
	result is the same, without a software double round trip on the PSP */
	result = (long)__builtin_rintf(value);""")),
]

# ---------- build 113: the flip prompt watched, and a fixture for it
# (psp_watch.c; b113_candidates.md #10). "flipwatch" logs the prompt's four
# conditions as they change for each vehicle the player's search looks at,
# the player's action and button, the flip and the vehicle physics' upending.
# "flipfixture" (debug tools only: it moves a vehicle) puts the vehicle nearest
# the player upside down just in front of him, two seconds into the map, so
# the emulator can reproduce Sheepdog01's sequence with no hands.
FIXES_113 = {}
FIXES_113["source/game/players.c#flipwatch"] = [
("""static void player_examine_nearby_vehicle(
	long player_index,
	long vehicle_index)
{""",
 """#ifdef HALO_PSP
/* psp_watch.c (build 113) */
int psp_flipwatch_on(void);
void psp_flipwatch_examine(long vehicle_index, const char *name, int dead, float up_k, float tipped_below,
	int upending, int driver, int at_rest);
void psp_flipwatch_player(long action_result, long action_object, int action_bit);
void psp_flipwatch_flip(long vehicle_index, int approach_type);
char *tag_get_name(long tag_index);
#if PSP_DEBUG_TOOLS
int psp_keep(const char *name);
void platform_psp_log(const char *format, ...);

/* "flipfixture": a Warthog within 30 units of the player (else the nearest
vehicle) put upside down just in front of him, touching his search's sphere,
once, at game time 60; at game time 150, once it has fallen and settled,
where it lies against the player */
static void psp_flip_fixture(long unit_index)
{
	static int fixture = -1;
	static long placed = NONE;
	struct object_iterator iterator;
	struct object_datum *object, *unit;
	long best = NONE;
	real best_distance = 30.0f * 30.0f;

	if (fixture < 0)
		fixture = psp_keep("flipfixture");
	if (placed != NONE && game_time_get() >= 150)
	{
		struct object_datum *vehicle = object_try_and_get_and_verify_type(placed, _object_mask_vehicle);

		unit = object_get(unit_index);
		if (vehicle)
			platform_psp_log("flipfixture: t=%ld the vehicle lies at %.2f %.2f %.2f, up.k %.2f, at rest %d; its "
				"sphere's centre %.2f from the player's (radii %.2f and %.2f)", game_time_get(),
				vehicle->object.position.x, vehicle->object.position.y, vehicle->object.position.z, vehicle->object.up.k,
				TEST_FLAG(vehicle->object.flags, _object_at_rest_bit) ? 1 : 0,
				(real)sqrt((vehicle->object.bounding_sphere_center.x - unit->object.bounding_sphere_center.x) *
					(vehicle->object.bounding_sphere_center.x - unit->object.bounding_sphere_center.x) +
					(vehicle->object.bounding_sphere_center.y - unit->object.bounding_sphere_center.y) *
					(vehicle->object.bounding_sphere_center.y - unit->object.bounding_sphere_center.y) +
					(vehicle->object.bounding_sphere_center.z - unit->object.bounding_sphere_center.z) *
					(vehicle->object.bounding_sphere_center.z - unit->object.bounding_sphere_center.z)),
				vehicle->object.bounding_sphere_radius, unit->object.bounding_sphere_radius);
		placed = NONE;
	}
	if (!fixture || game_time_get() < 60)
		return;
	fixture = 0;
	unit = object_get(unit_index);
	object_iterator_new(&iterator, _object_mask_vehicle, 0);
	while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
	{
		real dx = object->object.position.x - unit->object.position.x;
		real dy = object->object.position.y - unit->object.position.y;
		real dz = object->object.position.z - unit->object.position.z;
		real distance = dx * dx + dy * dy + dz * dz;

		/* a Warthog first (Sheepdog01's sightings), else the nearest */
		if (object->object.parent_object_index == NONE && distance < 30.0f * 30.0f &&
			strstr(tag_get_name(object->definition_index), "warthog") != NULL)
			distance -= 30.0f * 30.0f;
		if (distance < best_distance && object->object.parent_object_index == NONE)
		{
			best_distance = distance;
			best = iterator.index;
		}
	}
	if (best == NONE)
	{
		platform_psp_log("flipfixture: no vehicle within 30 units of the player");
		return;
	}
	{
		struct object_datum *vehicle = object_get(best);
		real_point3d place;
		real_vector3d forward, up = { 0.0f, 0.0f, -1.0f }, ahead;
		real length, offset;

		ahead.i = unit->object.forward.i;
		ahead.j = unit->object.forward.j;
		ahead.k = 0.0f;
		length = (real)sqrt(ahead.i * ahead.i + ahead.j * ahead.j);
		if (length < 0.01f)
		{
			ahead.i = 1.0f;
			ahead.j = 0.0f;
			length = 1.0f;
		}
		ahead.i /= length;
		ahead.j /= length;
		/* near enough that the two bounding spheres overlap (the player's
		search finds what touches his sphere), the vehicle's own radius plus
		a little: b113's first runs put a Ghost 1.5 units out, its sphere
		0.3 units short of the player's */
		offset = vehicle->object.bounding_sphere_radius + 0.2f;
		place.x = unit->object.position.x + offset * ahead.i;
		place.y = unit->object.position.y + offset * ahead.j;
		place.z = unit->object.position.z + 0.5f;
		/* side on to the player, roof down */
		forward.i = -ahead.j;
		forward.j = ahead.i;
		forward.k = 0.0f;
		object_set_position(best, &place, &forward, &up);
		placed = best;
		vehicle->object.translational_velocity.i = vehicle->object.translational_velocity.j =
			vehicle->object.translational_velocity.k = 0.0f;
		vehicle->object.angular_velocity.i = vehicle->object.angular_velocity.j =
			vehicle->object.angular_velocity.k = 0.0f;
		vehicle->object.flags &= ~FLAG(_object_at_rest_bit);
		platform_psp_log("flipfixture: %s #%08lx put upside down %.2f units in front of the player (%.2f %.2f %.2f)",
			tag_get_name(vehicle->definition_index), (unsigned long)best, offset, place.x, place.y, place.z);
	}
}
#endif
#endif
static void player_examine_nearby_vehicle(
	long player_index,
	long vehicle_index)
{"""),
("""	player = player_get(player_index);
	vehicle = (struct players_vehicle_datum *)vehicle_get(vehicle_index);
	if (!TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit))
""",
 """	player = player_get(player_index);
	vehicle = (struct players_vehicle_datum *)vehicle_get(vehicle_index);
#ifdef HALO_PSP
	if (psp_flipwatch_on())
	{
		struct game_globals_player_control *psp_control = TAG_BLOCK_GET_ELEMENT(
			&scenario_get_game_globals()->player_control, 0, struct game_globals_player_control);

		psp_flipwatch_examine(vehicle_index, tag_get_name(vehicle->definition_index),
			TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit) != 0, vehicle->object.up.k,
			(real)cos((_pi * 0.5f) - psp_control->minimum_vehicle_flipping_angle),
			TEST_FLAG(vehicle->vehicle.flags, _vehicle_upending_bit) != 0,
			vehicle->unit.driver_object_index != NONE, TEST_FLAG(vehicle->object.flags, _object_at_rest_bit) != 0);
	}
#endif
	if (!TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit))
"""),
("""		SET_FLAG(target_vehicle->vehicle.flags, _vehicle_upending_bit, TRUE);
""",
 """		SET_FLAG(target_vehicle->vehicle.flags, _vehicle_upending_bit, TRUE);
#ifdef HALO_PSP
		psp_flipwatch_flip(player->action_object_index, approach_type);
#endif
"""),
("""					if (TEST_FLAG(action->control_flags, _unit_control_action_bit) &&
						unit->object.parent_object_index == NONE &&
						!player_handle_action(iterator.datum_index) &&""",
 """#ifdef HALO_PSP
#if PSP_DEBUG_TOOLS
					psp_flip_fixture(player->unit_index);
#endif
					if (psp_flipwatch_on())
						psp_flipwatch_player(player->action_result, player->action_object_index,
							TEST_FLAG(action->control_flags, _unit_control_action_bit) != 0);
#endif
					if (TEST_FLAG(action->control_flags, _unit_control_action_bit) &&
						unit->object.parent_object_index == NONE &&
						!player_handle_action(iterator.datum_index) &&"""),
]
FIXES_113["source/units/vehicles.c#flipwatch"] = [
("""			vehicle->vehicle.upending_ticks++;
		}
		else
		{
			vehicle->vehicle.upending_ticks = 0;""",
 """#ifdef HALO_PSP
			{
				/* psp_watch.c (build 113) */
				void psp_flipwatch_upending_start(long vehicle_index, float up_k);

				if (!vehicle->vehicle.upending_ticks)
					psp_flipwatch_upending_start(vehicle_index, vehicle->object.up.k);
			}
#endif
			vehicle->vehicle.upending_ticks++;
		}
		else
		{
#ifdef HALO_PSP
			if (TEST_FLAG(vehicle->vehicle.flags, 4))
			{
				void psp_flipwatch_upending_end(long vehicle_index, int ticks, int type, float up_k);

				psp_flipwatch_upending_end(vehicle_index, vehicle->vehicle.upending_ticks,
					vehicle->vehicle.upending_type, vehicle->object.up.k);
			}
#endif
			vehicle->vehicle.upending_ticks = 0;"""),
]
FIXES_113["source/objects/objects.c#flipwatch"] = [
("""		before = psp_family_checksum(header);
	if (!before && !unchecked)
""",
 """		before = psp_family_checksum(header);
	{
		/* build 113: flipwatch, the sleep's own numbers for a vehicle just
		flipped (psp_watch.c): why b113's first fixture runs saw it stay
		asleep after its upending flag was set */
		extern long psp_flipwatch_watched(void);

		if (object_index == psp_flipwatch_watched())
			platform_psp_log("flipwatch: t=%ld sleep check #%08lx: may sleep %d, checksum now %08lx, kept %08lx, quiet %d, "
				"same object %d -> %s", game_time_get(), (unsigned long)object_index, may_sleep, before,
				psp_sleep[absolute_index].checksum, psp_sleep[absolute_index].quiet,
				psp_sleep[absolute_index].identifier == header->identifier ? 1 : 0,
				unchecked || (psp_sleep[absolute_index].identifier == header->identifier &&
					psp_sleep[absolute_index].quiet >= 2 && psp_sleep[absolute_index].checksum == before) ? "ASLEEP" : "updates");
	}
	if (!before && !unchecked)
"""),
("""		if (header->type == _object_type_vehicle)
			psp_vehicles_asleep++;
""",
 """		if (header->type == _object_type_vehicle)
		{
			void psp_flipwatch_asleep(long object_index); /* psp_watch.c (build 113) */

			psp_vehicles_asleep++;
			psp_flipwatch_asleep(object_index);
		}
"""),
]


# build 113: "fixedseed" (tests, beside "fixedticks" and "statecrc"). Two
# b112 runs with the same input parted at tick 806, in the effects
# (docs/big_levers.md §5). The local random seed, which effects, particles
# and the sound manager all draw from, starts from the clock
# (random_math_initialize), and the sound manager draws from it as sounds
# start and loop: when depends on the audio and the stick's timing, not on
# game time. With the word, the local seed starts at a constant when a map
# starts, and the sound manager draws from a seed of its own, so the game's
# own draws come out the same in every run. Off, nothing changes.
FIXES_113["source/math/random_math.c#fixedseed"] = [
("""unsigned long *get_global_local_random_seed_address(
	void)
{
	return &random_math_globals.global_local_random_seed;
}
""",
 """unsigned long *get_global_local_random_seed_address(
	void)
{
	return &random_math_globals.global_local_random_seed;
}

#ifdef HALO_PSP
/* build 113: "fixedseed" (psp_fixups.py): the sound manager's own seed, and
the constant starts. sound_manager.c and sound_definitions.c draw through
psp_sound_seed */
int psp_keep(const char *name);
static unsigned long psp_sound_random_seed = 0x50554E44UL;
static int psp_fixed_seed = -1;

static int psp_fixed_seed_on(void)
{
	if (psp_fixed_seed < 0)
		psp_fixed_seed = psp_keep("fixedseed");
	return psp_fixed_seed;
}

unsigned long *psp_sound_seed(void)
{
	return psp_fixed_seed_on() ? &psp_sound_random_seed : &random_math_globals.global_local_random_seed;
}

/* game.c, game_initialize_for_new_map, beside the global seed's start */
void psp_fixed_seed_new_map(void)
{
	if (psp_fixed_seed_on())
	{
		random_math_globals.global_local_random_seed = 0x0B5E55EDUL;
		psp_sound_random_seed = 0x50554E44UL;
	}
}
#endif
"""),
]
FIXES_113["source/game/game.c#fixedseed"] = [
("""	set_random_seed(game_globals->options.random_seed);
""",
 """	set_random_seed(game_globals->options.random_seed);
#ifdef HALO_PSP
	{
		extern void psp_fixed_seed_new_map(void); /* random_math.c (psp_fixups.py, build 113) */

		psp_fixed_seed_new_map();
	}
#endif
"""),
]
FIXES_113["source/sound/sound_manager.c#fixedseed"] = [
("""#include <math.h>
#include <stdio.h>
""",
 """#include <math.h>
#include <stdio.h>

#ifdef HALO_PSP
/* build 113: "fixedseed": the sound manager draws from its own seed
(random_math.c, psp_fixups.py); without the word it is the local seed, as
before */
unsigned long *psp_sound_seed(void);
#define get_global_local_random_seed_address psp_sound_seed
#define real_local_random_range(lower, upper) real_seed_random_range(psp_sound_seed(), (lower), (upper))
#endif
"""),
]
FIXES_113["source/sound/sound_definitions.c#fixedseed"] = [
("""#include "sound_definitions.h"
""",
 """#include "sound_definitions.h"

#ifdef HALO_PSP
/* build 113: "fixedseed": permutations drawn from the sound manager's seed
(random_math.c, psp_fixups.py) */
unsigned long *psp_sound_seed(void);
#define get_global_local_random_seed_address psp_sound_seed
#endif
"""),
]


# build 113: the render-state cache (b113_candidates.md #21): 512 entries,
# 256 with "renderstates256"; and counts of what it and the rendered-object
# list do each frame, reported with the objects (psp_platform.c)
FIXES_113["source/render/render_objects.c#renderstates"] = [
("""	cached_object_render_states = game_state_data_new(
		"cached object render states",
		MAXIMUM_CACHED_OBJECT_RENDER_STATES,
		sizeof(struct object_render_state));""",
 """#ifdef HALO_PSP
	{
		extern int psp_keep(const char *name);

		/* build 113: the Xbox's 256 with "renderstates256" (psp_capacity.h) */
		cached_object_render_states = game_state_data_new(
			"cached object render states",
			psp_keep("renderstates256") ? 256 : MAXIMUM_CACHED_OBJECT_RENDER_STATES,
			sizeof(struct object_render_state));
	}
#else
	cached_object_render_states = game_state_data_new(
		"cached object render states",
		MAXIMUM_CACHED_OBJECT_RENDER_STATES,
		sizeof(struct object_render_state));
#endif"""),
("""	object_marker_end();

	if (render_object_globals.rendered_object_count == MAXIMUM_RENDERED_OBJECTS &&""",
 """	object_marker_end();
#ifdef HALO_PSP
	{
		/* build 113: how full the rendered-object list gets (psp_platform.c's
		report): objects past it aren't drawn that frame */
		extern long psp_rendered_views, psp_rendered_full, psp_rendered_peak, psp_rendered_capacity;

		psp_rendered_capacity = MAXIMUM_RENDERED_OBJECTS;
		psp_rendered_views++;
		if (render_object_globals.rendered_object_count > psp_rendered_peak)
			psp_rendered_peak = render_object_globals.rendered_object_count;
		if (render_object_globals.rendered_object_count == MAXIMUM_RENDERED_OBJECTS)
			psp_rendered_full++;
	}
#endif

	if (render_object_globals.rendered_object_count == MAXIMUM_RENDERED_OBJECTS &&"""),
("""		render_state_index = datum_new(cached_object_render_states);
		if (render_state_index == NONE)
		{""",
 """		render_state_index = datum_new(cached_object_render_states);
#ifdef HALO_PSP
		{
			/* build 113: lighting worked out whole, and how many of those
			first had to find and push out the oldest entry */
			extern long psp_render_states_new, psp_render_states_evicted;

			psp_render_states_new++;
			if (render_state_index == NONE)
				psp_render_states_evicted++;
		}
#endif
		if (render_state_index == NONE)
		{"""),
]


# build 113: contrails, the Covenant weapons' tracers (b113_candidates.md #1;
# keep word "contrails"): the strip render_contrail builds goes to the GE's
# see-through draws (psp_ge.c, psp_ge_contrail) instead of the
# transparent-geometry groups the PSP never draws. Without the word, as before
FIXES_113["source/render/render_contrails.c#contrails"] = [
("""static void render_contrail(
	struct contrail_datum *contrail,
	struct contrail_definition *definition,
	short instance_index)
{
	struct bitmap_data *bitmap;""",
 """#ifdef HALO_PSP
int psp_keep(const char *name);
int psp_ge_active(void);
int psp_ge_contrail(void const *vertices, int vertex_count, unsigned int bitmap_key, short blend); /* psp_ge.c */
#endif

static void render_contrail(
	struct contrail_datum *contrail,
	struct contrail_definition *definition,
	short instance_index)
{
	struct bitmap_data *bitmap;"""),
("""			rasterizer_dynamic_triangles_unlock(triangle_buffer_index);
			rasterizer_dynamic_vertices_unlock(vertex_buffer_index);
			rasterizer_dynamic_unlit_geometry_draw(""",
 """#ifdef HALO_PSP
			{
				static int psp_contrails = -1;

				if (psp_contrails < 0)
					psp_contrails = psp_keep("contrails");
				if (psp_contrails && psp_ge_active())
				{
					/* the shader's blend: contrail shaders lay out their first
					words as effect shaders do (flags, then the blend function) */
					psp_ge_contrail(first_vertex, vertex_count, (unsigned int)bitmap,
						((struct shader_effect_definition const *)shader)->framebuffer_blend_function);
					rasterizer_dynamic_triangles_unlock(triangle_buffer_index);
					rasterizer_dynamic_vertices_unlock(vertex_buffer_index);
					rasterizer_dynamic_triangles_delete(triangle_buffer_index);
					rasterizer_dynamic_vertices_delete(vertex_buffer_index);
					rasterizer_globals.current_lock_operation = _rasterizer_lock_none;
					return;
				}
			}
#endif
			rasterizer_dynamic_triangles_unlock(triangle_buffer_index);
			rasterizer_dynamic_vertices_unlock(vertex_buffer_index);
			rasterizer_dynamic_unlit_geometry_draw("""),
]


# build 113: a zoomed weapon's mask (b113_candidates.md #20; keep word
# "zoommask"): the screen effect's mask goes to the GE (psp_ge.c,
# psp_ge_zoom_mask). The rest of the effect (blur, night vision) as before
FIXES_113["source/rasterizer/rasterizer.c#zoommask"] = [
("""void rasterizer_screen_effect(
	struct rasterizer_cinematic_screen_effect_parameters *parameters)
{
	_rasterizer_screen_effect(parameters);""",
 """#ifdef HALO_PSP
#include "rasterizer/rasterizer_cinematics.h"
int psp_keep(const char *name);
int psp_ge_active(void);
void psp_ge_zoom_mask(unsigned int key, unsigned int strength); /* psp_ge.c */
#endif

void rasterizer_screen_effect(
	struct rasterizer_cinematic_screen_effect_parameters *parameters)
{
#ifdef HALO_PSP
	{
		static int psp_zoom_mask = -1;

		/* "zoommaskfull": the whole alpha always, for Sheepdog01 to compare */
		if (psp_zoom_mask < 0)
			psp_zoom_mask = psp_keep("zoommaskfull") ? 2 : psp_keep("zoommask");
		/* the whole alpha with no blur; a third with one, the blur's
		stand-in (convolution type 0: none) */
		if (psp_zoom_mask && psp_ge_active() && parameters && parameters->convolution_mask)
			psp_ge_zoom_mask((unsigned int)parameters->convolution_mask,
				psp_zoom_mask == 2 || !parameters->convolution_type ? 255 : 85);
	}
#endif
	_rasterizer_screen_effect(parameters);"""),
]


# build 113: the sound cache's placement (b113_candidates.md #24; keep word
# "soundplace", Sheepdog01's call). b112 run 6 dropped sounds while only 650-1,100 KB
# of its 1,792 were playing: a sound needs one run of the cache's pages, and
# sounds still playing (or loading) can't be pushed out, so where they sit
# splits the space. Big sounds (32 KB and up: dialogue, engine and ambient
# loops, which also play longest) now take the highest place they fit, so
# they gather at the top and the small, short ones churn below, in the space
# left whole. Small sounds keep the cache's own choice (the oldest place).
# Only the sound cache; without the word, as before
FIXES_113["source/memory/lruv_cache.c#soundplace"] = [
("""long lruv_block_new(
	struct lruv_cache *cache,
	long size)
{""",
 """#ifdef HALO_PSP
/* build 113: "soundplace" (psp_fixups.py): the cache whose big blocks take
the highest place they fit (xbox_sound_cache.c sets it), and the size that
counts as big */
struct lruv_cache *psp_lruv_high_cache = NULL;
#define PSP_LRUV_HIGH_PAGES 8

/* build 113: the longest run of pages a new block could take now (free pages
and blocks that may be pushed out, side by side), and the pages held by
blocks that can't (playing, loading, or used this tick): the sound cache's
log line when a sound finds no room (xbox_sound_cache.c) */
long psp_lruv_largest_clearable(struct lruv_cache *cache, long *locked_pages)
{
	long page = 0, run = 0, best = 0, locked = 0, block_index = cache->first_block_index;

	while (page < cache->page_count)
	{
		struct lruv_cache_block *block;

		if (block_index == NONE)
		{
			run += cache->page_count - page;
			break;
		}
		block = datum_get(cache->blocks, block_index);
		if (page < block->first_page_index)
		{
			run += block->first_page_index - page;
			page = block->first_page_index;
			continue;
		}
		if ((cache->locked_block_proc && cache->locked_block_proc(block_index)) ||
			(unsigned long)block->last_used_tick == (unsigned long)cache->tick)
		{
			if (run > best)
				best = run;
			run = 0;
			locked += block->page_count;
		}
		else
			run += block->page_count;
		page = block->first_page_index + block->page_count;
		block_index = block->next_block_index;
	}
	if (run > best)
		best = run;
	if (locked_pages)
		*locked_pages = locked;
	return best;
}
#endif

long lruv_block_new(
	struct lruv_cache *cache,
	long size)
{"""),
("""				if (hole->page_count >= desired_page_count)
				{
					if (!found_hole ||
						hole->last_used_tick < best_hole.last_used_tick ||
						(hole->last_used_tick == best_hole.last_used_tick &&
						 hole->page_count < best_hole.page_count))
					{""",
 """				if (hole->page_count >= desired_page_count)
				{
#ifdef HALO_PSP
					/* build 113: a big sound takes the highest place it fits
					(holes come in rising page order, so the last that fits) */
					int psp_high = cache == psp_lruv_high_cache && desired_page_count >= PSP_LRUV_HIGH_PAGES;
#else
					int psp_high = 0;
#endif

					if (!found_hole || psp_high ||
						(hole->last_used_tick < best_hole.last_used_tick ||
						(hole->last_used_tick == best_hole.last_used_tick &&
						 hole->page_count < best_hole.page_count)))
					{"""),
]
FIXES_113["source/cache/xbox_sound_cache.c#soundplace"] = [
("""	xbox_sound_cache_globals.cache = lruv_new(
		"xbox sound cache",
		SOUND_CACHE_PAGE_COUNT,
		SOUND_CACHE_PAGE_SIZE_BITS,
		512,
		sound_cache_delete_block_proc,
		(lruv_locked_block_proc)sound_cache_locked_block_proc);""",
 """	xbox_sound_cache_globals.cache = lruv_new(
		"xbox sound cache",
		SOUND_CACHE_PAGE_COUNT,
		SOUND_CACHE_PAGE_SIZE_BITS,
		512,
		sound_cache_delete_block_proc,
		(lruv_locked_block_proc)sound_cache_locked_block_proc);
#ifdef HALO_PSP
	{
		/* build 113: "soundplace": big sounds at the top (lruv_cache.c).
		build 114: on by default, "nosoundplace" for b113's; b113's Test 2
		on the PSP with sound1792: no sound dropped by game time 4,000 */
		extern struct lruv_cache *psp_lruv_high_cache;
		extern int psp_keep(const char *name);

		if (!psp_keep("nosoundplace"))
			psp_lruv_high_cache = xbox_sound_cache_globals.cache;
	}
#endif"""),
]


# build 113: the sound cache's failures named (b113_candidates.md #24): the
# first 24 each get a line with the sound's size, the longest run the cache
# could clear for it, and what playing or loading sounds held
FIXES_113["source/cache/xbox_sound_cache.c#noroom"] = [
("""	else
		psp_sound_cache_failures++;
#endif
""",
 """	else
	{
		psp_sound_cache_failures++;
		if (psp_sound_cache_failures <= 24)
		{
			extern long psp_lruv_largest_clearable(struct lruv_cache *cache, long *locked_pages); /* lruv_cache.c */
			extern long psp_game_time_peek(void);
			long locked_pages = 0, clearable = psp_lruv_largest_clearable(xbox_sound_cache_globals.cache, &locked_pages);

			platform_psp_log("sound cache: no room for %ld KB at game time %ld: the longest run it could clear %ld KB; "
				"%ld KB held by sounds playing or loading", (long)(sound->samples.size / 1024), psp_game_time_peek(),
				clearable * 4, locked_pages * 4);
		}
	}
#endif
"""),
]

# ---------- build 114
FIXES_114 = {}
# build 114: "drawdistN" (B9, optimization_ranking.md; Sheepdog01 approved it as a
# switch to try): the far clip at N world units outside cutscenes. The
# engine already brings each window's far clip in to its fog when the fog is
# fully dense there (just above); the cap goes beside it, for the render and
# rasterizer cameras both, so the engine's own culling leaves out the objects
# and the level's clusters past it (the engine's work and the GE's). Where the
# level has atmospheric fog, the fog reaches full density at the cap and
# starts by 60% of it, in the level's own colour, so the far edge fades; a
# level without it just ends at the cap. Off unless keep.txt has the word
FIXES_114["source/render/render.c#drawdist"] = [
("""	if (window->render_camera.z_far <= window->render_camera.z_near)
	{
		if (!render_invalid_fog_warning_displayed)""",
 """#ifdef HALO_PSP
	{
		/* build 114: "drawdistN" (psp_ge.c, psp_draw_distance; psp_fixups.py) */
		extern float psp_draw_distance(void);
		extern boolean cinematic_in_progress(void);
		real limit = psp_draw_distance();

		if (limit > 0.0f && !cinematic_in_progress())
		{
			if (window->render_camera.z_far > limit)
				window->render_camera.z_far = limit;
			if (window->rasterizer_camera.z_far > limit)
				window->rasterizer_camera.z_far = limit;
			if (render.fog.atmospheric_maximum_distance > 0.0f)
			{
				if (render.fog.atmospheric_maximum_distance > limit)
					render.fog.atmospheric_maximum_distance = limit;
				if (render.fog.atmospheric_minimum_distance > limit * 0.6f)
					render.fog.atmospheric_minimum_distance = limit * 0.6f;
				render.fog.atmospheric_maximum_density = 1.0f;
			}
		}
	}
#endif
	if (window->render_camera.z_far <= window->render_camera.z_near)
	{
		if (!render_invalid_fog_warning_displayed)"""),
]

# ---------- build 115
# E16 (the animations' fmod and floor in single precision) was tried here and
# held back: with it, the emulator's state hashes differed from b115 without
# it from tick 0 (docs/build_b115.md, "What the emulator showed"). The text
# pairs are in commit 2de694d if it comes back with the cause found.
FIXES_115 = {}

# ---------- build 116
FIXES_116 = {}
# build 116: the planar fog (c10's swamp), as it reaches the eye: how deep
# the eye is below the fog's plane, over the fog's maximum depth, as the
# Xbox's fog pass works it out (rasterizer_xbox_environment_fog.c,
# _rasterizer_environment_fog_begin). A cluster that is all fog ("fully
# fogged") has its plane put at the far clip, facing the eye, by the Xbox's
# own settling (rasterizer_xbox.c), so the eye is all the way in. The GE
# fogs with the thicker of this and the atmospheric fog (psp_ge.c,
# psp_ge_fog_planar); keep word "planarfogoff": as before
# build 116: a see-through shader's tint, for the one map the GE draws of it:
# texpack.py works it out from the shader's stages with every map at its
# mean colour, and the pack carries it by the shader's tag address
# (psp_tex.c, psp_tex_shader_tint), halved and marked when the shader is
# brighter than its map, for the GE's colour doubling. Looked up just before
# each queue call, which uses it for that call's draws (psp_ge.c,
# psp_ge_seethrough_shader). Keep word "notint": as before (b30's ocean white)
FIXES_116["source/rasterizer/rasterizer.c#tint"] = [
("""			if (key && dynamic_triangle_buffer_index == NONE && dynamic_vertex_buffer_index == NONE)
				psp_ge_transparent(triangle_buffer, triangle_count, vertex_buffer, key, u_scale, v_scale, u_offset,
					v_offset, blend, &centroid->x);""",
 """			if (key && dynamic_triangle_buffer_index == NONE && dynamic_vertex_buffer_index == NONE)
			{
				extern void psp_ge_seethrough_shader(unsigned int shader);

				psp_ge_seethrough_shader((unsigned int)shader);
				psp_ge_transparent(triangle_buffer, triangle_count, vertex_buffer, key, u_scale, v_scale, u_offset,
					v_offset, blend, &centroid->x);
			}"""),
("""			if (key)
				psp_environment_transparent(dynamic_triangle_buffer_index, first_triangle_index, triangle_count,
					vertex_buffers, key, u_scale, v_scale, u_offset, v_offset, blend, centroid ? &centroid->x : NULL);""",
 """			if (key)
			{
				extern void psp_ge_seethrough_shader(unsigned int shader);

				psp_ge_seethrough_shader((unsigned int)shader);
				psp_environment_transparent(dynamic_triangle_buffer_index, first_triangle_index, triangle_count,
					vertex_buffers, key, u_scale, v_scale, u_offset, v_offset, blend, centroid ? &centroid->x : NULL);
			}"""),
]
# build 116: water (c10's swamp) draws its base map with the blend the
# Xbox's passes come down to (rasterizer_xbox_water.c): the background times
# the base map's colour when the shader says so, then the reflection added
# through the base map's alpha. One GE draw: an alpha blend when the colour
# modulates the background, an added draw otherwise; texpack.py's tint folds
# the reflection cube and the view tints in (seethrough.py). Builds 21-115
# drew the base map plainly, alpha-blended: the white swamp. Keep word
# "waterwhite": as before. The shader types are the Xbox's, as Invader
# writes them for it (no chicago extended): generic 5, chicago 6, water 7,
# glass 8, meter 9, plasma 10
FIXES_116["source/rasterizer/rasterizer.c#water"] = [
("""	else if (type == 7)
		key = psp_tex_bitmap_key(*(long const *)(definition + 88), 0);""",
 """	else if (type == 7 && PSP_KEEP_ONCE("waterwhite"))
		key = psp_tex_bitmap_key(*(long const *)(definition + 88), 0);
	else if (type == 7)
	{
		/* build 116: water's base map (+76, its tag at +88), blended by the
		shader's flags (+40: bit 0 the base map's alpha modulates the
		reflection, bit 1 its colour modulates the background) */
		key = psp_tex_bitmap_key(*(long const *)(definition + 88), 0);
		*blend = (*(unsigned short const *)(definition + 40) & 2) ? 0 : 3;
	}"""),
]
# build 116 (b113 #16): meter shaders, drawn (the plasma weapons' heat
# gauges, the Warthog's speedometer and engine meters). The Xbox works a
# meter's colour out per pixel in four combiner stages from five of the
# object's values (rasterizer_xbox_transparent_geometry.c,
# _shader_type_transparent_meter); the stages are the HUD's meters' with two
# constants in other slots, so the engine side fills the HUD's parameters
# (psp_seethrough.h) and the GE works a palette out from them as it does for
# the HUD (psp_ge.c, meter_palette; the bake gives meter maps the HUD's grid
# palette, texpack.py). Builds 21-115 left meters out. Keep word "nometers":
# as before. Level meters (no object, no values) stay left out
FIXES_116["source/rasterizer/rasterizer.c#meters"] = [
("""/* a see-through shader as the GE draws it: one texture (the first map of a
numeric shader""",
 """/* build 116: the meter the last psp_transparent_map call worked out, for
the queue call after it (NULL: not a meter) */
static struct psp_seethrough_meter const *psp_transparent_meter = NULL;

/* a colour (0-1) and an alpha as the Xbox's pixel32, ARGB, rounded as
real_a_rgb_color_to_pixel32 does (build 116, the meters) */
static unsigned int psp_meter_pixel(float alpha, float const *rgb)
{
	float v[4] = { alpha, rgb[0], rgb[1], rgb[2] };
	unsigned int out = 0;
	int k;

	for (k = 0; k < 4; k++)
		out = (out << 8) | (unsigned int)(int)(PIN(v[k], 0.0f, 1.0f) * 255.0f + 0.5f);
	return out;
}

/* a see-through shader as the GE draws it: one texture (the first map of a
numeric shader"""),
("""	*u_scale = *v_scale = 1.0f;
	*u_offset = *v_offset = 0.0f;
	*blend = 0;
	/* build 107: a first map that is a cube map""",
 """	*u_scale = *v_scale = 1.0f;
	*u_offset = *v_offset = 0.0f;
	*blend = 0;
	psp_transparent_meter = NULL; /* build 116 */
	/* build 107: a first map that is a cube map"""),
("""	else if (type == 8)
	{
		key = psp_tex_bitmap_key(*(long const *)(definition + 356), 0);
		*u_scale = *(float const *)(definition + 340);
		*v_scale = *u_scale;
	}""",
 """	else if (type == 8)
	{
		key = psp_tex_bitmap_key(*(long const *)(definition + 356), 0);
		*u_scale = *(float const *)(definition + 340);
		*v_scale = *u_scale;
	}
	else if (type == 9 && object && !PSP_KEEP_ONCE("nometers"))
	{
		/* build 116: a meter. Its map (+76, the tag at +88; the bitmap the
		permutation picks, as rasterizer_set_texture does) holds the position
		along the meter in its blue and the shape in its alpha. Flags +40
		(bit 2 the flash colour negative, bit 3 tint mode 2); colours +124
		(gradient minimum, maximum, background, flash, tint: three floats
		each); meter and background transparency +184, +188; the sources
		+216 (meter brightness, flash brightness, value, gradient, flash
		extension: 1-4 the object's A-D outputs, otherwise 1). Into the HUD's
		slots (psp_seethrough.h): stage 0's flash extension where the HUD
		has its fill, stage 2's value where the HUD has its flash's end */
		static struct psp_seethrough_meter meter;
		unsigned short flags = *(unsigned short const *)(definition + 40);
		float const *color = (float const *)(definition + 124);
		float value[5] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f }, flash[3], tint[3];
		long map = *(long const *)(definition + 88);
		int bitmaps = psp_tex_bitmap_count(map), tint_mode_2 = (flags >> 3) & 1, k;

		for (k = 0; k < 5; k++)
		{
			short source = *(short const *)(definition + 216 + 2 * k);

			if (source >= 1 && source <= 4 && psp_animation_valid)
				value[k] = PIN(psp_animation_values[source - 1], 0.0f, 1.0f);
		}
		for (k = 0; k < 3; k++)
		{
			flash[k] = value[1] * color[9 + k];
			tint[k] = tint_mode_2 ? value[0] * color[12 + k] : color[12 + k];
		}
		meter.gradient_min_color = psp_meter_pixel(value[4], color);
		meter.gradient_max_color = psp_meter_pixel(1.0f / (value[3] * 8.0f > 1.0f ? value[3] * 8.0f : 1.0f), color + 3);
		meter.background_color = psp_meter_pixel(tint_mode_2 ? *(float const *)(definition + 188) : 0.0f, color + 6);
		meter.flash_color = psp_meter_pixel(value[2], flash);
		meter.tint_color = psp_meter_pixel(tint_mode_2 ? *(float const *)(definition + 184) : value[0], tint);
		meter.flash_color_is_negative = (unsigned char)((flags >> 2) & 1);
		meter.tint_mode_2 = (unsigned char)tint_mode_2;
		meter.pad[0] = meter.pad[1] = 0;
		meter.gradient = value[3];
		key = psp_tex_bitmap_key(map, bitmaps > 0 && permutation > 0 ? permutation % bitmaps : 0);
		if (key)
			psp_transparent_meter = &meter;
	}"""),
("""				extern void psp_ge_seethrough_shader(unsigned int shader);

				psp_ge_seethrough_shader((unsigned int)shader);
				psp_ge_transparent(triangle_buffer,""",
 """				extern void psp_ge_seethrough_shader(unsigned int shader);
				extern void psp_ge_seethrough_meter(const void *meter);

				psp_ge_seethrough_shader((unsigned int)shader);
				psp_ge_seethrough_meter(psp_transparent_meter); /* build 116: NULL unless a meter */
				psp_ge_transparent(triangle_buffer,"""),
]
# build 116 (b113 #2): a flare's ray meets characters too, as the Xbox's
# depth-tested visibility query hid a plasma glow behind anyone standing in
# front of it; b31-b115's ray met the level, vehicles, scenery and machines
# only, so glows shone through bodies. The players' own bipeds are left out
# (the eye is inside the player's), and the ray stops 0.15 units short of
# the flare, so a character's capsule doesn't hide the glow of the weapon in
# its own hands. Keep word "flaresnobipeds": as before
FIXES_116["source/rasterizer/xbox/rasterizer_xbox_widgets.c#bipeds"] = [
("""				vector_from_points3d(&global_window_parameters.camera.position, point, &ray);
				/* the level's sky surfaces (no material) are where the
				sky is drawn: a ray that meets one has reached the sky */
				if (!collision_test_vector(
					FLAG(_collision_test_front_facing_surfaces_bit) |
						FLAG(_collision_test_ignore_invisible_surfaces_bit) |
						FLAG(_collision_test_structure_bit) |
						FLAG(_collision_test_objects_bit) |
						_collision_test_objects_sight_blocking_flags,""",
 """				vector_from_points3d(&global_window_parameters.camera.position, point, &ray);
				{
					static int no_bipeds = -1;
					real length = magnitude3d(&ray);

					if (no_bipeds < 0)
						no_bipeds = psp_keep("flaresnobipeds");
					if (!no_bipeds && length > 0.3f)
						scale_vector3d(&ray, (length - 0.15f) / length, &ray);
					psp_flare_bipeds = no_bipeds ? 0 :
						FLAG(_collision_test_objects_bipeds_bit) | FLAG(_collision_test_skip_player_bipeds_bit);
				}
				/* the level's sky surfaces (no material) are where the
				sky is drawn: a ray that meets one has reached the sky */
				if (!collision_test_vector(
					FLAG(_collision_test_front_facing_surfaces_bit) |
						FLAG(_collision_test_ignore_invisible_surfaces_bit) |
						FLAG(_collision_test_structure_bit) |
						FLAG(_collision_test_objects_bit) |
						_collision_test_objects_sight_blocking_flags | psp_flare_bipeds,"""),
("""static unsigned long psp_occlusion_frame = 1;
static int psp_occlusion_exact = -1;""",
 """static unsigned long psp_occlusion_frame = 1;
static int psp_occlusion_exact = -1;
/* build 116: characters in a flare's way (b113 #2) */
static unsigned long psp_flare_bipeds = 0;"""),
]
FIXES_116["source/rasterizer/rasterizer.c#planarfog"] = [
("""		extern void psp_ge_fog(const float *color, float minimum_distance, float maximum_distance, float density);

		psp_ge_fog(&global_window_parameters.fog.atmospheric_color.red,""",
 """		extern void psp_ge_fog(const float *color, float minimum_distance, float maximum_distance, float density);
		extern void psp_ge_fog_planar(float red, float green, float blue, float maximum_distance, float density);
		struct render_fog const *settled = &global_window_parameters.fog;
		real eye = 0.0f;

		if (settled->planar_mode && settled->planar_maximum_depth > 0.0f)
		{
			eye = -(settled->plane.n.i * global_window_parameters.camera.position.x +
				settled->plane.n.j * global_window_parameters.camera.position.y +
				settled->plane.n.k * global_window_parameters.camera.position.z -
				settled->plane.d) / settled->planar_maximum_depth;
			eye = eye < 0.0f ? 0.0f : eye > 1.0f ? 1.0f : eye;
		}
		/* build 116: first the planar fog, which psp_ge_fog weighs against the atmospheric */
		psp_ge_fog_planar(settled->planar_color.red, settled->planar_color.green, settled->planar_color.blue,
			settled->planar_maximum_distance, settled->planar_maximum_density * eye);
		psp_ge_fog(&global_window_parameters.fog.atmospheric_color.red,"""),
]
# build 116: the compressed animations' float tables and node headers read
# with unaligned-safe loads (PSP_UNALIGNED_REAL, PSP_UNALIGNED_REAL3,
# PSP_UNALIGNED_ULONG: psp_limits.h). Our own keyframed animations pad every
# table to 4 bytes (the fork's psp_keyframe_compress), but Bungie's compressed
# ones don't always: b115 faulted on a10's opening cinematic at a translation
# keyframe 2 past a multiple of 4 (badvaddr 09af9e36, in the animation cache;
# the emulator's JIT forgives it, the PSP's CPU doesn't). A restoration: the
# same values, read safely. The rotations are 6-byte tables of shorts and the
# frame indices words, both fine at 2-byte alignment. The last pair is the
# default translation animation_get_node_orientations copies itself for a
# node without translation keyframes: the strict interpreter stopped b115.1
# there on a10, a30 and c40 at the same game times b115 stopped (lab batch
# campaign-strict-b1151)
FIXES_116["source/models/model_animations.c#unaligned"] = [
("""	node_header = ((unsigned long const *)(data+header->translation_node_headers_offset))[adjusted_node_index];""",
 """	node_header = PSP_UNALIGNED_ULONG(data+header->translation_node_headers_offset+sizeof(unsigned long)*adjusted_node_index);"""),
("""		*translation = default_translations[node_index];""",
 """		PSP_UNALIGNED_REAL3(translation, &default_translations[node_index]);"""),
("""			*translation = keyframe_translations[keyframe_count-1];""",
 """			PSP_UNALIGNED_REAL3(translation, &keyframe_translations[keyframe_count-1]);"""),
("""		if (real_frame_index==(real)this_keyframe_frame_index)
		{
			*translation = *this_keyframe;""",
 """#ifdef HALO_PSP
		/* build 116 (psp_fixups.py): both keyframes copied out with
		unaligned-safe loads, so the copy and the interpolation below read
		aligned floats */
		real_point3d psp_this_keyframe, psp_next_keyframe;

		PSP_UNALIGNED_REAL3(&psp_this_keyframe, this_keyframe);
		PSP_UNALIGNED_REAL3(&psp_next_keyframe, next_keyframe);
		this_keyframe = &psp_this_keyframe;
		next_keyframe = &psp_next_keyframe;
#endif
		if (real_frame_index==(real)this_keyframe_frame_index)
		{
			*translation = *this_keyframe;"""),
("""	node_header = ((unsigned long const *)(data+header->scale_node_headers_offset))[adjusted_node_index];""",
 """	node_header = PSP_UNALIGNED_ULONG(data+header->scale_node_headers_offset+sizeof(unsigned long)*adjusted_node_index);"""),
("""		*scale = default_scales[adjusted_node_index];""",
 """		*scale = PSP_UNALIGNED_REAL(&default_scales[adjusted_node_index]);"""),
("""			*scale = keyframe_scales[keyframe_count-1];""",
 """			*scale = PSP_UNALIGNED_REAL(&keyframe_scales[keyframe_count-1]);"""),
("""			this_keyframe_scale = default_scales[adjusted_node_index];""",
 """			this_keyframe_scale = PSP_UNALIGNED_REAL(&default_scales[adjusted_node_index]);"""),
("""			next_keyframe_scale = keyframe_scales[0];""",
 """			next_keyframe_scale = PSP_UNALIGNED_REAL(&keyframe_scales[0]);"""),
("""			this_keyframe_scale = keyframe_scales[keyframe_count-1];""",
 """			this_keyframe_scale = PSP_UNALIGNED_REAL(&keyframe_scales[keyframe_count-1]);"""),
("""			next_keyframe_scale = default_scales[adjusted_node_index];""",
 """			next_keyframe_scale = PSP_UNALIGNED_REAL(&default_scales[adjusted_node_index]);"""),
("""			this_keyframe_scale = keyframe_scales[keyframe_index];""",
 """			this_keyframe_scale = PSP_UNALIGNED_REAL(&keyframe_scales[keyframe_index]);"""),
("""			next_keyframe_scale = keyframe_scales[keyframe_index+1];""",
 """			next_keyframe_scale = PSP_UNALIGNED_REAL(&keyframe_scales[keyframe_index+1]);"""),
("""					orientation->translation = *((real_point3d *)(data+header->default_translations_offset)+node_index);""",
 """					PSP_UNALIGNED_REAL3(&orientation->translation, (real_point3d const *)(data+header->default_translations_offset)+node_index);"""),
]

# after build 116: "hdump" (psp_ge.c) names each HUD quad's bitmap by its
# tag: the tag whose bitmaps hold the quad's bitmap_data. A walk over every
# bitmap tag, so only for the frames a screenshot is asked for
FIXES_118 = {}
# build 118 (X10, optimization_ranking.md): the units' and bipeds' vector
# checks skipped. unit_verify_vectors runs a dozen times a unit a tick and
# biped_verify_object_vectors around a biped's every pose; each only tests
# that the facing vectors are unit length and at right angles, and does
# something (a log line and an assert) only when they aren't. In play they
# always are, so skipping them changes nothing but the time: 6% of the
# emulator's tick (b114, big_levers.md section 8: 7 us x 165 a tick).
# "verifyvectors" runs them as before
FIXES_118["source/units/units.c#verify"] = [
("""static void unit_verify_vectors(
	long unit_index,
	char const *debugstring)
{
	char buffer[512];

	if (!unit_vectors_are_valid(unit_index))""",
 """static void unit_verify_vectors(
	long unit_index,
	char const *debugstring)
{
	char buffer[512];

#ifdef HALO_PSP
	{
		extern int psp_keep(const char *name);
		static int psp_verify = -1;

		if (psp_verify < 0)
			psp_verify = psp_keep("verifyvectors");
		if (!psp_verify)
			return;
	}
#endif
	if (!unit_vectors_are_valid(unit_index))"""),
]
FIXES_118["source/units/bipeds.c#verify"] = [
("""static void biped_verify_object_vectors(
	long biped_index,
	char const *state)
{
	struct biped_datum *biped = biped_get(biped_index);
""",
 """static void biped_verify_object_vectors(
	long biped_index,
	char const *state)
{
	struct biped_datum *biped = biped_get(biped_index);
#ifdef HALO_PSP
	{
		/* (units.c's #verify, psp_fixups.py) */
		extern int psp_keep(const char *name);
		static int psp_verify = -1;

		if (psp_verify < 0)
			psp_verify = psp_keep("verifyvectors");
		if (!psp_verify)
			return;
	}
#endif
"""),
]
FIXES_118["source/interface/hud_draw.c#hdump"] = [
("""
void hud_draw_bitmap(
	struct bitmap_data const *bitmap,""",
 """
#ifdef HALO_PSP
#include "cache/cache_files.h"
char *tag_get_name(long tag_index);

/* the tag a bitmap_data belongs to, or NULL (psp_fixups.py; "hdump") */
char const *psp_bitmap_tag_name(void const *bitmap)
{
	struct tag_iterator iterator;
	long tag_index;

	tag_iterator_new(&iterator, BITMAP_GROUP_TAG);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		struct bitmap_group const *group = bitmap_group_get(tag_index);
		char const *first = (char const *)group->bitmaps.address;

		if (first && (char const *)bitmap >= first &&
			(char const *)bitmap < first + group->bitmaps.count * (long)sizeof(struct bitmap_data))
			return tag_get_name(tag_index);
	}
	return NULL;
}

/* the shader tag whose definition this is, or NULL ("tdump"; every shader
group has 'shdr' as a parent) */
char const *psp_shader_tag_name(void const *shader)
{
	struct tag_iterator iterator;
	long tag_index;

	tag_iterator_new(&iterator, 0x73686472 /* shdr */);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		if (tag_get(0x73686472, tag_index) == shader)
			return tag_get_name(tag_index);
	}
	return NULL;
}
#endif

void hud_draw_bitmap(
	struct bitmap_data const *bitmap,"""),
]

# after build 116: "hdump" also lists a multitexture overlay's other maps,
# how they combine, and their offsets and scales (the sniper rifle's angle
# ticks: the GE draws only the first map, b116)
FIXES_118["source/rasterizer/rasterizer.c#hdump"] = [
("""		if (psp_ge_active())
		{
			/* one map: the first, except under the menus' moving "plasma",""",
 """		if (parameters->map[1] && psp_ge_active())
		{
			extern volatile int psp_screenshot_requested; /* psp_ge.c */
			extern char const *psp_bitmap_tag_name(void const *bitmap); /* hud_draw.c (psp_fixups.py) */
			extern void platform_log(const char *format, ...);
			extern int psp_keep(const char *name);
			static int psp_hdump = -1;

			if (psp_hdump < 0)
				psp_hdump = psp_keep("hdump");
			if (psp_hdump && psp_screenshot_requested)
			{
				int map;

				platform_log("hdump: multitexture, map 0 to 1 %d, map 1 to 2 %d, framebuffer %d, plasma %d",
					parameters->map0_to_1_blend_function, parameters->map1_to_2_blend_function,
					parameters->framebuffer_blend_function, parameters->doing_plasma_effect);
				for (map = 0; map < 3 && parameters->map[map]; map++)
				{
					char const *name = psp_bitmap_tag_name(parameters->map[map]);

					platform_log("hdump:   map %d %s: offset %.3f %.3f, scale %.3f %.3f, texture scale %.4f %.4f, "
						"%s, %s, tint %.2f %.2f %.2f, fade %.2f", map, name ? name : "(no tag)",
						parameters->map_offset[map] ? parameters->map_offset[map]->x : 0.0f,
						parameters->map_offset[map] ? parameters->map_offset[map]->y : 0.0f,
						parameters->map_scale[map].i, parameters->map_scale[map].j,
						parameters->map_texture_scale[map].i, parameters->map_texture_scale[map].j,
						parameters->map_wrapped[map] ? "wrapped" : "clamped",
						parameters->map_anchor_screen[map] ? "anchored to the screen" : "to the quad",
						parameters->map_tint[map] ? parameters->map_tint[map]->red : 1.0f,
						parameters->map_tint[map] ? parameters->map_tint[map]->green : 1.0f,
						parameters->map_tint[map] ? parameters->map_tint[map]->blue : 1.0f,
						parameters->map_fade[map] ? *parameters->map_fade[map] : 1.0f);
				}
			}
		}
		if (psp_ge_active())
		{
			/* one map: the first, except under the menus' moving "plasma","""),
]

# after build 116: a HUD multitexture overlay whose second map multiplies the
# first, as the Xbox's combiners take them (keep word "hudonemap": the first
# map alone, as before). The sniper rifle's angle ticks are two: a mask times
# the ticks, which scroll with the aim's pitch and fade in when zoomed. The GE
# drew the mask alone, at full strength: a50's two white bars (b116)
FIXES_118["source/rasterizer/rasterizer.c#multiply"] = [
("""
void rasterizer_psuedo_dynamic_screen_quad_draw(
	struct rasterizer_dynamic_screen_geometry_parameters *parameters,""",
 """
#ifdef HALO_PSP
#include "bitmaps/bitmap_group.h"

/* The GE has one texture a draw, so the second map is drawn and the first
(the mask) is read at the edges of strips across the quad: its texels
multiply the strips' vertex colours, and the GE's colour interpolation does
the rest. Each map's coordinates as the Xbox's screen vertex program works
them out: the quad's (or the screen's, when anchored to it) times the map's
scale, plus its offset, times its texture scale. Nothing is drawn when
either map's fade is 0 (the ticks, unzoomed). Returns 0 for any other
combination, which is drawn as before (the first map) and named once */
#define PSP_HUD_STRIPS_MAX 32

static int psp_hud_multiply(
	struct rasterizer_dynamic_screen_geometry_parameters const *parameters,
	struct dynamic_screen_vertex const *vertices)
{
	extern int psp_tex_resident(unsigned int key);
	extern int psp_tex_texel(unsigned int key, float u, float v, unsigned int *argb);
	extern void psp_ge_screen_quad(void const *vertices, unsigned int key, short blend, float red, float green,
		float blue, float fade, float offset_x, float offset_y, void const *meter, int wrapped, float u_scale,
		float v_scale);
	extern void platform_log(const char *format, ...);
	extern int psp_keep(const char *name);
	static int off = -1, named = 0, told = 0;
	struct dynamic_screen_vertex strip[4];
	real_rgb_color const *tint0 = parameters->map_tint[0], *tint1 = parameters->map_tint[1];
	real fade0 = parameters->map_fade[0] ? *parameters->map_fade[0] : 1.0f;
	real fade1 = parameters->map_fade[1] ? *parameters->map_fade[1] : 1.0f;
	real edge_u[PSP_HUD_STRIPS_MAX + 1][2], edge_v[PSP_HUD_STRIPS_MAX + 1][2];
	real edge_x[PSP_HUD_STRIPS_MAX + 1][2], edge_y[PSP_HUD_STRIPS_MAX + 1][2];
	pixel32 edge_color[PSP_HUD_STRIPS_MAX + 1][2];
	real across, along, inset = 0.0f;
	int strips, line, side, along_s;

	if (off < 0)
		off = psp_keep("hudonemap");
	if (off)
		return 0;
	if (parameters->map[2] || parameters->meter_parameters ||
		parameters->map0_to_1_blend_function != 1 /* _shader_framebuffer_blend_function_multiply */)
	{
		if (named < 4)
		{
			named++;
			platform_log("HUD: a multitexture overlay the GE draws as its first map only (map 0 to 1 %d, three maps %d)",
				parameters->map0_to_1_blend_function, parameters->map[2] != NULL);
		}
		return 0;
	}
	/* a map's fade scales only alpha, which only the alpha blend (0) takes
	the source by; the other blends still write colour (review) */
	if ((fade0 <= 0.0f || fade1 <= 0.0f) && parameters->framebuffer_blend_function == 0)
		return 1;
	/* the mask's texels are read from memory: a frame it isn't loaded
	(the stick's budget spent) draws nothing, as a quad whose bind fails */
	if (!psp_tex_resident((unsigned int)parameters->map[0]))
		return 1;
	/* strips across the quad's longer side, about 8 of the game's pixels
	each: the 0-1 and 3-2 edges (s) or the 0-3 and 1-2 edges (t) */
	across = (vertices[1].position.x - vertices[0].position.x) * (vertices[1].position.x - vertices[0].position.x) +
		(vertices[1].position.y - vertices[0].position.y) * (vertices[1].position.y - vertices[0].position.y);
	along = (vertices[3].position.x - vertices[0].position.x) * (vertices[3].position.x - vertices[0].position.x) +
		(vertices[3].position.y - vertices[0].position.y) * (vertices[3].position.y - vertices[0].position.y);
	along_s = across > along;
	strips = (int)(sqrtf(along_s ? across : along) / 8.0f);
	{
		/* no more strips than the mask has texels along them (nearest
		sampling gains nothing past that) */
		real du = (vertices[along_s ? 1 : 3].texture_coordinates.x - vertices[0].texture_coordinates.x) *
			parameters->map_scale[0].i * parameters->map_texture_scale[0].i * (real)parameters->map[0]->width;
		real dv = (vertices[along_s ? 1 : 3].texture_coordinates.y - vertices[0].texture_coordinates.y) *
			parameters->map_scale[0].j * parameters->map_texture_scale[0].j * (real)parameters->map[0]->height;
		int texels = (int)(sqrtf(du * du + dv * dv) + 0.5f);

		if (!parameters->map_anchor_screen[0] && texels >= 1 && strips > texels)
			strips = texels;
	}
	strips = strips < 1 ? 1 : strips > PSP_HUD_STRIPS_MAX ? PSP_HUD_STRIPS_MAX : strips;
	{
		/* the mask is read half a texel inside the quad's two long edges,
		not on them: clamped HUD art often has a clear border texel */
		real du = (vertices[along_s ? 3 : 1].texture_coordinates.x - vertices[0].texture_coordinates.x) *
			parameters->map_scale[0].i * parameters->map_texture_scale[0].i * (real)parameters->map[0]->width;
		real dv = (vertices[along_s ? 3 : 1].texture_coordinates.y - vertices[0].texture_coordinates.y) *
			parameters->map_scale[0].j * parameters->map_texture_scale[0].j * (real)parameters->map[0]->height;
		real texels = sqrtf(du * du + dv * dv);

		inset = texels > 1.0f ? 0.5f / texels : 0.5f;
	}
	for (line = 0; line <= strips; line++)
	{
		real f = (real)line / (real)strips;

		for (side = 0; side < 2; side++)
		{
			/* the two corners this line runs between, and the point on it */
			int a = along_s ? (side ? 3 : 0) : (side ? 1 : 0);
			int b = along_s ? (side ? 2 : 1) : (side ? 2 : 3);
			real x = vertices[a].position.x + (vertices[b].position.x - vertices[a].position.x) * f;
			real y = vertices[a].position.y + (vertices[b].position.y - vertices[a].position.y) * f;
			real u = vertices[a].texture_coordinates.x + (vertices[b].texture_coordinates.x - vertices[a].texture_coordinates.x) * f;
			real v = vertices[a].texture_coordinates.y + (vertices[b].texture_coordinates.y - vertices[a].texture_coordinates.y) * f;
			/* the point the mask is read at: inset from this edge toward the other */
			int c = along_s ? (side ? 0 : 3) : (side ? 0 : 1), d = along_s ? (side ? 1 : 2) : (side ? 3 : 2);
			real xs = x + (vertices[c].position.x + (vertices[d].position.x - vertices[c].position.x) * f - x) * inset;
			real ys = y + (vertices[c].position.y + (vertices[d].position.y - vertices[c].position.y) * f - y) * inset;
			real us = u + (vertices[c].texture_coordinates.x +
				(vertices[d].texture_coordinates.x - vertices[c].texture_coordinates.x) * f - u) * inset;
			real vs = v + (vertices[c].texture_coordinates.y +
				(vertices[d].texture_coordinates.y - vertices[c].texture_coordinates.y) * f - v) * inset;
			real u0 = (parameters->map_anchor_screen[0] ? xs : us) * parameters->map_scale[0].i;
			real v0 = (parameters->map_anchor_screen[0] ? ys : vs) * parameters->map_scale[0].j;
			unsigned int texel = 0xFFFFFFFFu, out = 0;
			int channel;

			if (parameters->map_offset[0])
			{
				u0 += parameters->map_offset[0]->x;
				v0 += parameters->map_offset[0]->y;
			}
			u0 *= parameters->map_texture_scale[0].i;
			v0 *= parameters->map_texture_scale[0].j;
			if (!parameters->map_wrapped[0])
			{
				u0 = PIN(u0, 0.0f, 0.999f);
				v0 = PIN(v0, 0.0f, 0.999f);
			}
			psp_tex_texel((unsigned int)parameters->map[0], u0, v0, &texel);
			/* the vertex colour (the corners', along the line) times the
			mask's texel times the first map's tint and fade; ARGB, so
			channel 0 is blue and 3 alpha */
			for (channel = 0; channel < 4; channel++)
			{
				int shift = 8 * channel;
				real scale = channel == 3 ? fade0 :
					!tint0 ? 1.0f : channel == 2 ? tint0->red : channel == 1 ? tint0->green : tint0->blue;
				real from = (real)((vertices[a].color >> shift) & 0xFF), to = (real)((vertices[b].color >> shift) & 0xFF);
				real value = (from + (to - from) * f) * (real)((texel >> shift) & 0xFF) / 255.0f * scale;

				out |= (unsigned int)PIN(value + 0.5f, 0.0f, 255.0f) << shift;
			}
			edge_x[line][side] = x;
			edge_y[line][side] = y;
			/* the second map's coordinates, before its texture scale (the
			GE's quad applies that, as for every HUD quad) */
			edge_u[line][side] = (parameters->map_anchor_screen[1] ? x : u) * parameters->map_scale[1].i +
				(parameters->map_offset[1] ? parameters->map_offset[1]->x : 0.0f);
			edge_v[line][side] = (parameters->map_anchor_screen[1] ? y : v) * parameters->map_scale[1].j +
				(parameters->map_offset[1] ? parameters->map_offset[1]->y : 0.0f);
			edge_color[line][side] = out;
		}
	}
	for (line = 0; line < strips; line++)
	{
		/* the strip's corners in the quad's order: 0 and 1 on one line, 2
		and 3 on the next (or across, for strips along t) */
		int k;

		for (k = 0; k < 4; k++)
		{
			int l = along_s ? (k == 0 || k == 3 ? line : line + 1) : (k < 2 ? line : line + 1);
			int s = along_s ? (k < 2 ? 0 : 1) : (k == 0 || k == 3 ? 0 : 1);

			strip[k].position.x = edge_x[l][s];
			strip[k].position.y = edge_y[l][s];
			strip[k].texture_coordinates.x = edge_u[l][s];
			strip[k].texture_coordinates.y = edge_v[l][s];
			strip[k].color = edge_color[l][s];
		}
		psp_ge_screen_quad(strip, (unsigned int)parameters->map[1], parameters->framebuffer_blend_function,
			tint1 ? tint1->red : 1.0f, tint1 ? tint1->green : 1.0f, tint1 ? tint1->blue : 1.0f, fade1,
			parameters->offset ? parameters->offset->i : 0.0f, parameters->offset ? parameters->offset->j : 0.0f,
			NULL, parameters->map_wrapped[1], parameters->map_texture_scale[1].i, parameters->map_texture_scale[1].j);
	}
	if (!told)
	{
		told = 1;
		platform_log("HUD: a multitexture overlay's second map multiplied by its first, in %d strips", strips);
	}
	return 1;
}
#endif

void rasterizer_psuedo_dynamic_screen_quad_draw(
	struct rasterizer_dynamic_screen_geometry_parameters *parameters,"""),
("""			psp_ge_screen_quad(vertices, (unsigned int)parameters->map[map], parameters->framebuffer_blend_function,""",
 """			if (parameters->map[1] && !parameters->doing_plasma_effect && psp_hud_multiply(parameters, vertices))
				return;
			psp_ge_screen_quad(vertices, (unsigned int)parameters->map[map], parameters->framebuffer_blend_function,"""),
]

# after build 116: a glass shader's diffuse map on a model lit by the model's
# light, as the Xbox's glass diffuse pass draws it (keep word "glassflat":
# at full brightness, as before). a10's bridge monitor ("monitor huge", a
# glass shader) drew a bright green pattern in a dark room (b116)
FIXES_118["source/rasterizer/rasterizer.c#glasslit"] = [
("""	else if (type == 8)
	{
		key = psp_tex_bitmap_key(*(long const *)(definition + 356), 0);
		*u_scale = *(float const *)(definition + 340);
		*v_scale = *u_scale;
	}""",
 """	else if (type == 8)
	{
		key = psp_tex_bitmap_key(*(long const *)(definition + 356), 0);
		*u_scale = *(float const *)(definition + 340);
		*v_scale = *u_scale;
		/* alpha-blended, lit (fade mode 3: psp_ge.c's SEETHROUGH_LIT) */
		if (object && !PSP_KEEP_ONCE("glassflat"))
			*blend = PSP_SEETHROUGH_PACK(0, 3, 1.0f, 0);
	}"""),
]

for rel, pairs in list(FIXES_19.items()) + list(FIXES_19_SKY.items()) + list(FIXES_22.items()) + list(FIXES_23.items()) + list(FIXES_31.items()) + list(FIXES_36.items()) + list(FIXES_105.items()) + list(FIXES_106.items()) + list(FIXES_107.items()) + list(FIXES_108.items()) + list(FIXES_109.items()) + list(FIXES_110.items()) + list(FIXES_112.items()) + list(FIXES_113.items()) + list(FIXES_114.items()) + list(FIXES_115.items()) + list(FIXES_116.items()) + list(FIXES_118.items()) + list(FIXES_CHUPA.items()):
    FIXES.setdefault(rel.split('#')[0], []).extend(pairs)


failures = []
for rel, changes in FIXES.items():
    path = os.path.join(TREE, rel)
    text = open(path, errors="surrogateescape").read()
    if "psp_fixups.py" in text:
        continue
    applied = 0
    for change in changes:
        if isinstance(change, Either):
            if change.pair is None:
                continue
            change = change.pair
        applied += 1
        old, new = change
        count = text.count(old)
        if count != 1:
            failures.append("psp_fixups: %s (%s): expected 1 match, found %d:\n%s" % (rel, BASE, count, old[:200]))
            if not CHECK_ONLY:
                sys.exit(failures[-1])
            continue
        text = text.replace(old, new)
    if CHECK_ONLY or not applied:
        # (a file whose changes are all the other base's is left as it is)
        continue
    # the sizes go after the file's first #include, so its headers come first
    if rel.endswith(".h"):
        open(path, "w", errors="surrogateescape").write(text); continue
    first = text.index("#include")
    line_end = text.index("\n", first) + 1
    text = text[:line_end] + PRELUDE + text[line_end:]
    open(path, "w", errors="surrogateescape").write(text)
if CHECK_ONLY:
    print("\n\n".join(failures))
    sys.exit("psp_fixups: %d anchors don't match once" % len(failures) if failures else 0)
print("psp_fixups: %d files changed, AI debug records shared in %d, unaligned accesses fixed in %d, breadcrumbs in %d, code appended to %d, passes skipped in %d, packed designators %d"
      % (len(FIXES), ai_debug_records(TREE), unaligned_accesses(TREE), breadcrumbs(TREE) + step_labels(TREE), appended(TREE), skipped_passes(TREE), packed_designators(TREE)))
# build 102: after ai_debug_records, which skips a file once PSP_DEBUG_INDEX is in it
print("psp_fixups: AI debugger state on demand in %d files" % ai_debug_shrink(TREE))
print("psp_fixups: colour conversions in single precision in %d files" % float_colours(TREE))
print("psp_fixups: render interpolation for one local player in %d files" % interpolation_players(TREE))
