/* Every adapter built in: a run names its box with the keep word box<name>. */
#include "box.h"

const struct box_adapter *const box_adapters[] = { &box_null, &box_selftest, &box_posing, &box_collbsp, &box_collbspa, &box_collbspw, &box_collbspr, &box_collbspb, &box_collbspm, &box_sweep0, &box_sweepa, &box_sweepf, &box_sweepl, &box_sweepb, &box_sweepb25, &box_sweepb100, &box_sweepb25f, &box_sweepb25l, 0 };
