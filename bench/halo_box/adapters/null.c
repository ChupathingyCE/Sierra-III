/* The null box: an entity that does nothing. Its cost in each mode is the
harness's own (the loop, the clock, and in the emptied and crowded modes the
refetch of the harness's own lines), which every other box's figure has
subtracted. */
#include "../box.h"

static int null_setup(const struct box_static *statics, unsigned int count) { (void)statics; (void)count; return 0; }
static void null_tick(const struct box_tick *tick) { (void)tick; }
static __attribute__((noinline)) void null_run(int i) { (void)i; __asm__ __volatile__(""); }
static void null_output(int i, const void **bytes, unsigned int *length) { (void)i; *bytes = 0; *length = 0; }

const struct box_adapter box_null = {
	"null", 0, 0, BOX_EXACT, 0, 0, 0, 0, null_setup, null_tick, null_run, null_output, 0,
};
