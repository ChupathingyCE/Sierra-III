/* Every adapter built in: a run names its box with the keep word box<name>. */
#include "box.h"

const struct box_adapter *const box_adapters[] = { &box_null, &box_selftest, &box_posing,
	&box_price_imiss, &box_price_dclean, &box_price_ddirty, &box_price_uncached, 0 };
