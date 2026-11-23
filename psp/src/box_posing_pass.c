/*
box_posing_pass.c: the world store's posing pass, slice 1 (skeleton, not wired)

The rebuild's posing pass poses every awake posed object once a tick, after
motion, from the store's sections (ROOT, ANIM, POSE), in place of the engine's
posing inside each object's update and its posing on demand. The lab's
tick_store_spec_v1.md sets the rules this skeleton follows:
- Order (section 13 item 1): parents before children, so a vehicle is posed
  before its riders and a unit before the weapon it holds. Today's one-tick
  lag on seat animation goes away; C4's bands on riders come from the
  calibration, not assumed.
- The IK after the pose (section 13 item 1b): held weapons follow hands, so
  an object's postprocess runs inside the pass, after its parent and its own
  pose.
- No posing outside the pass (section 15 item 2): a setter that lands after
  the pass marks a want; the settle at the tick's end poses the wanted objects
  in one batch. Readers never pose. Exceptional settles are counted.
- POSE's first form (section 35): the arena holds 52-byte matrices; the pass
  works in its own 16-byte-aligned scratch (48-byte quads plus the scale) and
  writes 52 bytes back.

What's here: the order, the want marks and the settle's shape, with the
per-object work left as declared steps. It's compiled only with
PSP_TICK2_POSEPASS (no build sets it yet), and nothing calls it.
*/
/* keeps the unit non-empty when the switch is off (ISO C wants a declaration) */
typedef int box_posing_pass_unit;

#ifdef PSP_TICK2_POSEPASS

#include <stdint.h>
#include <string.h>

void platform_psp_log(const char *format, ...);

/* the store's slot count is per map (ROOT's table); the pass's own arrays are
   sized for the most a map can have */
#define BOX_POSE_SLOTS 1536 /* TICK2_STORE_MOST */
#define BOX_POSE_NONE 0xFFFF

/* one object's place in the tick's order: its slot and its parent's (NONE for
   a root of the scene) */
struct box_pose_entry
{
	uint16_t slot;
	uint16_t parent;
};

static struct box_pose_entry box_pose_order[BOX_POSE_SLOTS];
static uint16_t box_pose_count;
/* a want: posed again at the tick's end because a setter landed after the pass */
static uint8_t box_pose_wanted[BOX_POSE_SLOTS / 8];
static struct { uint32_t posed, wants, settled, exceptional; } box_pose_stats;

/* The per-object steps, to be supplied when the pass is wired:
   - gather: the object's slot, its parent's slot, and whether it's awake
     and posed this tick (from ROOT);
   - pose: the node matrices from ANIM's state and the parent's matrices,
     in the pass's scratch (48-byte quads plus the scale), written back to
     POSE's 52-byte form;
   - postprocess: the type's IK (held weapons to hands), after the pose. */
int box_pose_gather(uint16_t *slots, uint16_t *parents, int most);
void box_pose_object(uint16_t slot);
void box_pose_postprocess(uint16_t slot);

/* The order: parents before children. The gather lists awake posed objects
   in slot order; a child whose parent comes later is moved after it. Families
   are shallow (a vehicle, its riders, their weapons), so a few passes settle
   the order. Each pass is linear in the count. */
static void box_pose_sort(void)
{
	static uint16_t place[BOX_POSE_SLOTS];
	int moved = 1, rounds = 0, k;

	while (moved && rounds++ < 8)
	{
		moved = 0;
		for (k = 0; k < box_pose_count; k++)
			place[box_pose_order[k].slot] = (uint16_t)k;
		for (k = 0; k < box_pose_count; k++)
		{
			uint16_t parent = box_pose_order[k].parent;

			if (parent != BOX_POSE_NONE && place[parent] > k)
			{
				/* the parent is posed later: move this child just after it */
				struct box_pose_entry child = box_pose_order[k];
				int at = place[parent];

				memmove(&box_pose_order[k], &box_pose_order[k + 1], (size_t)(at - k) * sizeof(box_pose_order[0]));
				box_pose_order[at] = child;
				moved = 1;
				break;
			}
		}
	}
	if (moved)
		platform_psp_log("posing pass: the order didn't settle in 8 rounds (%d objects)", box_pose_count);
}

/* the pass: after motion, every awake posed object once, parents first */
void box_pose_pass(void)
{
	static uint16_t slots[BOX_POSE_SLOTS], parents[BOX_POSE_SLOTS];
	int k;

	box_pose_count = (uint16_t)box_pose_gather(slots, parents, BOX_POSE_SLOTS);
	for (k = 0; k < box_pose_count; k++)
	{
		box_pose_order[k].slot = slots[k];
		box_pose_order[k].parent = parents[k];
	}
	box_pose_sort();
	for (k = 0; k < box_pose_count; k++)
	{
		box_pose_object(box_pose_order[k].slot);
		box_pose_postprocess(box_pose_order[k].slot);
	}
	box_pose_stats.posed += box_pose_count;
	memset(box_pose_wanted, 0, sizeof(box_pose_wanted));
}

/* a root or animation setter after the pass: the object is posed again at the settle */
void box_pose_want(uint16_t slot)
{
	if (slot < BOX_POSE_SLOTS && !(box_pose_wanted[slot >> 3] & (1u << (slot & 7))))
	{
		box_pose_wanted[slot >> 3] |= (uint8_t)(1u << (slot & 7));
		box_pose_stats.wants++;
	}
}

/* the tick's end: the wanted objects posed in one batch, in the pass's order
   (a wanted child after its parent). A want for an object the pass didn't
   list (it woke after the pass) is posed last and counted as exceptional. */
void box_pose_settle(void)
{
	int k;

	for (k = 0; k < box_pose_count; k++)
	{
		uint16_t slot = box_pose_order[k].slot;

		if (box_pose_wanted[slot >> 3] & (1u << (slot & 7)))
		{
			box_pose_wanted[slot >> 3] &= (uint8_t)~(1u << (slot & 7));
			box_pose_object(slot);
			box_pose_postprocess(slot);
			box_pose_stats.settled++;
		}
	}
	for (k = 0; k < BOX_POSE_SLOTS; k++)
		if (box_pose_wanted[k >> 3] & (1u << (k & 7)))
		{
			box_pose_object((uint16_t)k);
			box_pose_postprocess((uint16_t)k);
			box_pose_stats.exceptional++;
		}
	memset(box_pose_wanted, 0, sizeof(box_pose_wanted));
}

/* each minute with the store's other counters */
void box_pose_report(void)
{
	platform_psp_log("posing pass: posed %u, wants %u, settled at the tick's end %u, exceptional %u",
		(unsigned)box_pose_stats.posed, (unsigned)box_pose_stats.wants, (unsigned)box_pose_stats.settled,
		(unsigned)box_pose_stats.exceptional);
}

#endif /* PSP_TICK2_POSEPASS */
