/*
psp_nointerp.c: render_interpolation.c's answers with interpolation off

The PSP draws at most one frame a tick and never blends between ticks, so
render_interpolation.c (23 KB of code and statics, ChupathingyCE's blend of
the objects, the camera and the first-person weapon between ticks) stays out
of the campaign build (psp_build.py, PSP_INTERPOLATION=1 builds it back in).
Each answer here is the one that unit gives with interpolation off, so the
picture and play are the same.

The one part that works with interpolation off is the direct camera
(display.direct_camera, on by default): in first person on foot the view
points where the player aims, from player_control's facing, not the
observer's. It is kept as the unit has it.
*/

#include "cseries.h"
#include "math/real_math.h"
#include "objects/objects.h"
#include "camera/director.h"
#include "camera/observer.h"
#include "cutscene/cinematics.h"
#include "game/players.h"
#include "effects/particles.h"
#include "render/render_cameras.h"
#include "units/units.h"

/* port/linux/src/port_config.c */
int config_boolean(const char *name);
unsigned long config_changes(void);

#define TICKS_TO_SECONDS (1.0f / TICKS_PER_SECOND)

void render_interpolation_tick(void)
{
}

void render_interpolation_reset(void)
{
}

void render_interpolation_frame_begin(void)
{
}

void render_interpolation_frame_end(void)
{
}

boolean render_interpolation_drawing(void)
{
	return FALSE;
}

real render_interpolation_fraction(void)
{
	return 1.0f;
}

/* (NULL: the object's own node matrices, as the tick left them) */
real_matrix4x3 *render_interpolation_object_node_matrices(long object_index)
{
	(void)object_index;
	return NULL;
}

real *render_interpolation_object_function_values(long object_index)
{
	(void)object_index;
	return NULL;
}

real_rgb_color *render_interpolation_object_change_colors(long object_index)
{
	(void)object_index;
	return NULL;
}

boolean render_interpolation_unit_camouflage(long unit_index, real *amount, real *super_amount)
{
	(void)unit_index;
	(void)amount;
	(void)super_amount;
	return FALSE;
}

/* (FALSE: the particle is made now) */
boolean render_interpolation_particle_later(
	struct new_particle_data const *data,
	long object_index,
	short node_index,
	real fraction)
{
	(void)data;
	(void)object_index;
	(void)node_index;
	(void)fraction;
	return FALSE;
}

void render_interpolation_particles_due(void)
{
}

void render_interpolation_object_point(long object_index, real_point3d *point)
{
	(void)object_index;
	(void)point;
}

void render_interpolation_correct_object(long object_index, real_vector3d const *offset)
{
	(void)object_index;
	(void)offset;
}

void render_interpolation_flashlight(long light_index, long unit_index, real_vector3d *forward, real_vector3d *up)
{
	(void)light_index;
	(void)unit_index;
	(void)forward;
	(void)up;
}

void render_interpolation_view_turn(long unit_index, real_point3d *position, real_vector3d *forward, real_vector3d *up)
{
	(void)unit_index;
	(void)position;
	(void)forward;
	(void)up;
}

void render_interpolation_first_person(
	short local_player_index,
	real_matrix4x3 *node_matrices,
	short node_count,
	struct render_camera const *camera,
	long weapon_index,
	long graph_index)
{
	(void)local_player_index;
	(void)node_matrices;
	(void)node_count;
	(void)camera;
	(void)weapon_index;
	(void)graph_index;
}

/* The direct camera, as render_interpolation.c makes it: the PSP has one
local player */
struct observer_result const *render_interpolation_camera(
	short local_player_index,
	struct observer_result const *observer)
{
	static int enabled;
	static unsigned long read_at = (unsigned long)-1;
	static struct observer_result direct;
	long unit_index;

	if (local_player_index != 0)
		return observer;
	if (read_at != config_changes())
	{
		read_at = config_changes();
		enabled = config_boolean("display.direct_camera");
	}
	if (!enabled || !observer ||
		director_get_perspective(local_player_index) != _director_perspective_first_person ||
		director_inhibited_facing(local_player_index) ||
		cinematic_in_progress())
	{
		return observer;
	}
	unit_index = player_control_get_unit_index(local_player_index);
	if (unit_index == NONE || object_get(unit_index)->object.parent_object_index != NONE)
		return observer;
	direct = *observer;
	player_control_get_facing_direction(local_player_index, &direct.forward);
	observer_up_from_forward(&direct.forward, &direct.up);
	return &direct;
}

real render_interpolation_ticks_since(long now, long then)
{
	return (real)(now - then);
}

real render_interpolation_game_time_sec(long ticks)
{
	return (real)ticks * TICKS_TO_SECONDS;
}
