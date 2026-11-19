/* The box's BSP walks as the game builds them (PSP_SWEEPME=1): the level's
   3D nodes read where the tags hold them, since the box's own copy of them
   (32 B a node) would take 1.3 MB of b30's heap. box_collbsp.c is the code */
#define BOX_COLLBSP_TAG_NODES 1
#include "box_collbsp.c"
