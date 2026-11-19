/* The pill sweep's box as the game builds it (PSP_SWEEPME=1): its feature
   lists sized to what the ME's records hold (psp_sweepme.c), a list that
   reaches the size counted as cut short, and its world read from the world
   store's one-tick-old copy (box_ws_old.h) by datum index, for slice 2's
   whole gather. box_sweep.c is the code */
#define BOX_SWEEP_FEATURES 32
#define BOX_SWEEP_STORE 1
#include "box_sweep.c"
