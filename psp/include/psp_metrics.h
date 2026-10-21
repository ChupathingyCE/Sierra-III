/* psp_metrics.h: the test builds' metrics (psp/src/psp_metrics.c).

Every log point is one of these macros. Built with PSP_METRICS=0 (the final
release: psp_build.py) each compiles to nothing, so the build carries none of
the metrics' code or strings. Each is a plain call with no test around it,
so a function that gains one keeps its branches and its profile counts. */
#ifndef PSP_METRICS_H
#define PSP_METRICS_H

enum
{
	PSP_METRICS_TEXTURES,
	PSP_METRICS_GEOMETRY,
	PSP_METRICS_ANIMATIONS,
	PSP_METRICS_STICK_KINDS
};

enum
{
	PSP_METRICS_CHECKPOINT = 1,
	PSP_METRICS_REVERT = 2
};

#if defined(PSP_METRICS) && PSP_METRICS
void psp_metrics_tick(unsigned long microseconds);
void psp_metrics_frame(unsigned long draw_us, unsigned long ticks, unsigned long end_us);
void psp_metrics_event(int event);
void psp_metrics_map_play(const char *map_name);
void psp_metrics_map_end(int on_game_thread);
/* each reader's blocking Memory Stick time, so a slow frame can name the
cache that read in it */
extern unsigned long psp_metrics_stick_us[PSP_METRICS_STICK_KINDS];
extern unsigned long psp_metrics_stick_reads[PSP_METRICS_STICK_KINDS];
extern unsigned long psp_metrics_stick_bytes[PSP_METRICS_STICK_KINDS];
#define PSP_METRICS_TICK(us) psp_metrics_tick(us)
#define PSP_METRICS_FRAME(draw_us, ticks, end_us) psp_metrics_frame(draw_us, ticks, end_us)
#define PSP_METRICS_EVENT(event) psp_metrics_event(event)
#define PSP_METRICS_MAP_PLAY(name) psp_metrics_map_play(name)
#define PSP_METRICS_MAP_END(on_game_thread) psp_metrics_map_end(on_game_thread)
#define PSP_METRICS_STICK(kind, bytes, us) (psp_metrics_stick_reads[kind]++, psp_metrics_stick_bytes[kind] += (bytes), psp_metrics_stick_us[kind] += (us))
#else
#define PSP_METRICS_TICK(us) ((void)0)
#define PSP_METRICS_FRAME(draw_us, ticks, end_us) ((void)0)
#define PSP_METRICS_EVENT(event) ((void)0)
#define PSP_METRICS_MAP_PLAY(name) ((void)0)
#define PSP_METRICS_MAP_END(on_game_thread) ((void)0)
#define PSP_METRICS_STICK(kind, bytes, us) ((void)0)
#endif

#endif
