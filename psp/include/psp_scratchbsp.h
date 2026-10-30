/* psp_scratchbsp.h - the level collision BSP's top nodes in the scratchpad

keep.txt "scratchbsp" (psp_scratch.c): the level's bsp3d nodes nearest the
root, each with the plane it tests, copied breadth first into the
scratchpad's room when the level's collision BSP is set. Every walk of the
level starts at the root and passes these nodes first, and the tag data
spreads them over hundreds of cache lines (two arrays apart), so these are
the walks' most missed lines per byte (the scratchpad census). A copied
node's children that are copied too are stored as PSP_SB_TAG | slot, so the
walk that read the copy follows the copy; any other child keeps its own
index and the walk goes on in the tag data. Only the walk that read a copy
ever holds a tagged index, and only the level's own tree starts at slot 0.
The same bits as the tag data: the walks' answers don't change.

The Media Engine can't see the scratchpad, so its walks (its rays and
points) read the tag data. A build with PSP_MAP_CHECKS keeps the walks'
node index checks, which a tagged index would fail: there it is left out */
#ifndef PSP_SCRATCHBSP_H
#define PSP_SCRATCHBSP_H

struct psp_sb_slot
{
	float plane[4];
	long designator;
	long children[2];
	long node_index;
};

extern struct psp_sb_slot *psp_sb_slots;
extern void const *psp_sb_tree;

#define PSP_SB_TAG 0x40000000L

static inline int psp_sb_on_me(void)
{
	unsigned int sp;

	__asm__ ("move %0, $sp" : "=r"(sp));
	return (sp & 0x1FFFFFFFu) < 0x01000000u;
}

/* a tagged index came from a copy, which only the main CPU reads, in the
level's tree: it needs no other test. Deeper nodes (untagged, not the root)
pay two tests; the ME is told apart before it reads the main CPU's word */
#define PSP_SB_HERE(bsp, index) (((index) & PSP_SB_TAG) || \
	((index) == 0 && !psp_sb_on_me() && (void const *)(bsp) == psp_sb_tree))

#define PSP_SB_GET(index, node, plane, node_type, plane_type) do { \
	struct psp_sb_slot const *psp_sb_slot_ = &psp_sb_slots[(index) & 0xFFFF]; \
	(node) = (node_type)(void const *)&psp_sb_slot_->designator; \
	(plane) = (plane_type)(void const *)psp_sb_slot_->plane; \
} while (0)

#endif
