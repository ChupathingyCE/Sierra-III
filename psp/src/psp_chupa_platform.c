/* psp_chupa_platform.c - ChupathingyCE's desktop platform features, answered for the PSP (build 110)

The platform layer files the PSP build compiles (psp_build.py's
PLATFORM_FILES) call into ChupathingyCE's newer ones, which the PSP leaves
out: they draw with desktop OpenGL, read a touchscreen, or talk to the
internet. Each answer is the feature switched off, as the real function
answers with nothing set up, so the caller takes its plain path. Built as
sdl_platform.c is (its flags carry HALO_VERSION), with the real headers, so
a signature that differs doesn't compile. psp_chupa.c answers the game's
calls. */

#include "platform.h"
#include "sdl_platform.h"
#include "build_identity.h"
#include "hud_hires.h"
#include "menu_files.h"
#include "text_hires.h"
#include "touch_input.h"
#include "xgpu.h"
#include "delta.h"

#include <stdio.h>

#ifndef HALO_VERSION
#define HALO_VERSION "dev"
#endif

/* ---------- the build's identity (build_identity.c, updater.c) */

/* ChupathingyCE's version, as its own builds name themselves, and the PSP */
const char *build_identity(void)
{
	return "ChupathingyCE " HALO_VERSION " (PSP)";
}

/* the real one also logs the netcode's tables, which the PSP has none of */
void build_identity_log(void)
{
	static int logged;

	if (logged)
		return;
	logged = 1;
	platform_log("build: %s", build_identity());
}

const char *updater_version(void)
{
	return HALO_VERSION;
}

/* ---------- the netcode's version tables (delta.c): no internet play */

void delta_legacy_start(void) {}

/* ---------- the touchscreen (touch_input.c): the PSP has buttons */

void touch_input_event(unsigned int type, const SDL_TouchFingerEvent *finger) { (void)type; (void)finger; }
void touch_input_cancel(void) {}
void touch_input_menu_set_active(int active) { (void)active; }
/* (the caller reads the pointer only on a platform with a touchscreen) */
void touch_input_menu_read(struct platform_ui_pointer *pointer) { (void)pointer; }
void touch_input_gamepad(XINPUT_GAMEPAD *pad) { (void)pad; }
void touch_input_controls(XINPUT_GAMEPAD *pad, int menus) { (void)pad; (void)menus; }
/* no finger moved: the look is left as it is */
void touch_input_look(float scale, float *yaw, float *pitch) { (void)scale; (void)yaw; (void)pitch; }
void touch_input_rumble(unsigned int left, unsigned int right) { (void)left; (void)right; }

/* ---------- desktop OpenGL's extras (xgpu_post.c, xgpu_shader_cache.c)

The PSP runs d3d8_gl.c as the null renderer (the GE draws, psp_ge.c): no
antialiasing pass, and no shader cache on the Memory Stick */

BOOL xgpu_post_prepare(BOOL smaa) { (void)smaa; return FALSE; }
BOOL xgpu_post_anti_alias(BOOL smaa, GLuint framebuffer, unsigned long width, unsigned long height,
	const GLint corners[4])
{
	(void)smaa; (void)framebuffer; (void)width; (void)height; (void)corners;
	return FALSE;
}
/* the real one's answer when it can't keep a shader: compiled as it is */
GLuint xgpu_shader_cache_compile(GLenum type, const char *source, const char *what)
{
	return xgpu_compile_shader(type, source, what);
}
void xgpu_shader_cache_linked(GLuint vertex_shader, GLuint fragment_shader) { (void)vertex_shader; (void)fragment_shader; }
/* nothing cached to warm */
void xgpu_shader_cache_warm(void (*linked)(GLuint vertex_shader, GLuint fragment_shader, GLuint program))
{
	(void)linked;
}

/* ---------- high-resolution HUD and text (hud_hires.c, text_hires.c)

A desktop's larger screen gets sharper pictures and fonts; the PSP's is
smaller than the Xbox's, and the GE draws the game's own: no override, no
font (-1, the real ones' "none") */

long hud_hires_override_find(unsigned long address, unsigned long width, unsigned long height,
	unsigned long level0_size)
{
	(void)address; (void)width; (void)height; (void)level0_size;
	return -1;
}
unsigned int hud_hires_override_texture(long asset, unsigned long *levels) { (void)asset; (void)levels; return 0; }
int hud_hires_override_coverage(long asset) { (void)asset; return 0; }
int hud_hires_override_point_threshold(long asset) { (void)asset; return 0; }

long text_hires_font(char const *tag_name, float cap_height, float oversample)
{
	(void)tag_name; (void)cap_height; (void)oversample;
	return -1;
}
int text_hires_covers(long font, unsigned long code) { (void)font; (void)code; return 0; }
int text_hires_glyph(long font, unsigned long code, struct text_hires_glyph *glyph)
{
	(void)font; (void)code; (void)glyph;
	return 0;
}
void text_hires_register_atlas(const unsigned long *texture, unsigned long width, unsigned long height)
{
	(void)texture; (void)width; (void)height;
}
unsigned int text_hires_atlas_texture(unsigned long data) { (void)data; return 0; }

/* ---------- the PC menus' pictures (menu_files.c): no PC menus (psp_chupa.c) */

unsigned int menu_art_texture(unsigned long data, unsigned long *levels) { (void)data; (void)levels; return 0; }
