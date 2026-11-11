/* Every adapter built in: a run names its box with the keep word box<name>. */
#include "box.h"

const struct box_adapter *const box_adapters[] = { &box_null, &box_selftest, 0 };
