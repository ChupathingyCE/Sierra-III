/*
PSP_SDL.C

The few SDL3 functions the Linux platform layer calls (sdl_platform.c,
xinput_sdl.c, dsound_sdl.c, gl_functions.c), implemented directly on the
PSP's system calls. The PSP toolchain does ship SDL3, but linking it costs
2.4 MB (1.35 MB of code and a 1 MB display list for its renderer) out of
the PSP-1000's 24 MB, for a window, an OpenGL context and audio the
bring-up build doesn't use.

- No window or OpenGL: SDL_CreateWindow fails, so the renderer runs as
  the null renderer.
- One gamepad, read from the PSP's buttons and analog stick
  (SDL_GetGamepadButton/Axis), mapped one way in menus and another in play.
- No audio device: sound goes out through psp_dsound.c.
*/

#include <SDL3/SDL.h>
#include <pspctrl.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <psprtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(const char *format, ...);

#define PSP_GAMEPAD_ID 1
static int controller_ready = 0;
static int walk_test = 0;

/* scripted input, for tests with no hands (and in the emulator, which has
none): ms0:/halo/input.txt holds lines of "seconds buttons stick_x stick_y",
the buttons in hex as the PSP reports them (start 0008, select 0001,
triangle 1000, R 0200...), each held from its time until the next line's,
the times counted from the first time the game reads the controls */
#define MAXIMUM_SCRIPT_LINES 64
static struct { float seconds; unsigned int buttons; int x, y; } input_script[MAXIMUM_SCRIPT_LINES];
static int input_script_count = 0;
static SceInt64 input_script_start = 0;

static void input_script_load(void)
{
	FILE *file = fopen("ms0:/halo/input.txt", "r");
	char line[128];

	if (!file)
		return;
	while (input_script_count < MAXIMUM_SCRIPT_LINES && fgets(line, sizeof(line), file))
	{
		float seconds;
		unsigned int buttons;
		int x = 128, y = 128;

		if (sscanf(line, "%f %x %d %d", &seconds, &buttons, &x, &y) >= 2)
		{
			input_script[input_script_count].seconds = seconds;
			input_script[input_script_count].buttons = buttons;
			input_script[input_script_count].x = x;
			input_script[input_script_count].y = y;
			input_script_count++;
		}
	}
	fclose(file);
	platform_log("input script: %d lines from input.txt", input_script_count);
}

static void input_script_apply(SceCtrlData *pad)
{
	float seconds;
	int index, current = -1;

	if (!input_script_count)
		return;
	if (!input_script_start)
		input_script_start = sceKernelGetSystemTimeWide();
	seconds = (sceKernelGetSystemTimeWide() - input_script_start) / 1e6f;
	for (index = 0; index < input_script_count && input_script[index].seconds <= seconds; index++)
		current = index;
	if (current >= 0)
	{
		pad->Buttons |= input_script[current].buttons;
		pad->Lx = (unsigned char)input_script[current].x;
		pad->Ly = (unsigned char)input_script[current].y;
	}
}

static void controller_initialize(void)
{
	if (!controller_ready)
	{
		sceCtrlSetSamplingCycle(0);
		sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
		/* a test with no hands: with ms0:/halo/walktest.txt present, forward
		(triangle) is held and the stick turns slowly right, so the
		heartbeat's player position shows the input reaching the game */
		{
			SceIoStat status;

			walk_test = sceIoGetstat("ms0:/halo/walktest.txt", &status) >= 0;
			if (walk_test)
				platform_log("walk test: holding forward, turning right (delete walktest.txt to stop)");
		}
		input_script_load();
		controller_ready = 1;
	}
}

/* ---------- core */

void SDL_SetMainReady(void) {}
bool SDL_Init(SDL_InitFlags flags) { (void)flags; controller_initialize(); return true; }
Uint64 SDL_GetTicks(void) { return (Uint64)(sceKernelGetSystemTimeWide() / 1000); }
void SDL_free(void *memory) { free(memory); }
const char *SDL_GetError(void) { return "not available on the PSP"; }
bool SDL_SetHint(const char *name, const char *value) { (void)name; (void)value; return true; }
SDL_ThreadID SDL_GetCurrentThreadID(void) { return (SDL_ThreadID)sceKernelGetThreadId(); }
char *SDL_GetClipboardText(void) { char *text = malloc(1); if (text) text[0] = 0; return text; }
bool SDL_SetClipboardText(const char *text) { (void)text; return false; }
bool SDL_PollEvent(SDL_Event *event) { (void)event; return false; }
bool SDL_ShowSimpleMessageBox(SDL_MessageBoxFlags flags, const char *title, const char *message, SDL_Window *window)
{
	(void)flags; (void)window;
	platform_log("message box: %s: %s", title ? title : "", message ? message : "");
	return true;
}
bool SDL_ShowAndroidToast(const char *message, int duration, int gravity, int x, int y)
{
	(void)duration; (void)gravity; (void)x; (void)y;
	platform_log("toast: %s", message ? message : "");
	return true;
}

/* ---------- no window, no OpenGL */

SDL_Window *SDL_CreateWindow(const char *title, int w, int h, SDL_WindowFlags flags)
{ (void)title; (void)w; (void)h; (void)flags; return NULL; }
SDL_GLContext SDL_GL_CreateContext(SDL_Window *window) { (void)window; return NULL; }
bool SDL_GL_MakeCurrent(SDL_Window *window, SDL_GLContext context) { (void)window; (void)context; return false; }
bool SDL_GL_SetAttribute(SDL_GLAttr attribute, int value) { (void)attribute; (void)value; return false; }
bool SDL_GL_SetSwapInterval(int interval) { (void)interval; return false; }
bool SDL_GL_SwapWindow(SDL_Window *window) { (void)window; return false; }
SDL_FunctionPointer SDL_GL_GetProcAddress(const char *name) { (void)name; return NULL; }
bool SDL_GetWindowSizeInPixels(SDL_Window *window, int *w, int *h)
{ (void)window; if (w) *w = 480; if (h) *h = 272; return true; }
bool SDL_SetWindowRelativeMouseMode(SDL_Window *window, bool enabled) { (void)window; (void)enabled; return true; }

/* ---------- no audio device */

SDL_AudioStream *SDL_OpenAudioDeviceStream(SDL_AudioDeviceID device, const SDL_AudioSpec *spec,
	SDL_AudioStreamCallback callback, void *userdata)
{ (void)device; (void)spec; (void)callback; (void)userdata; return NULL; }
bool SDL_PutAudioStreamData(SDL_AudioStream *stream, const void *buffer, int length)
{ (void)stream; (void)buffer; (void)length; return false; }
bool SDL_ResumeAudioStreamDevice(SDL_AudioStream *stream) { (void)stream; return false; }

/* ---------- the PSP's controls as one gamepad

Placeholder mapping (the PSP has one stick; a real control scheme is a
design decision for later):
  analog stick        -> right stick (look)
  triangle / cross    -> left stick up / down (move forward / back)
  square / circle     -> left stick left / right (strafe)
  R / L               -> right / left trigger (fire / grenade)
  d-pad up            -> A (jump)       d-pad down  -> B (melee)
  d-pad left          -> X (reload)     d-pad right -> Y (switch weapon)
  start / select      -> start / back */

/* the last controls the game read, for the heartbeat (psp_platform.c) */
volatile unsigned int psp_pad_buttons, psp_pad_reads;
volatile int psp_pad_lx = 128, psp_pad_ly = 128;

static int pad_swallowed = 0; /* build 77: the debug menu has the pad */

static SceCtrlData read_controls(void)
{
	/* one read serves every button and axis query of the same poll: the
	game asks ~20 times per controller read (build 19) */
	static SceCtrlData cached;
	static unsigned int cached_time;
	static int cached_valid = 0;
	unsigned int now = sceKernelGetSystemTimeLow();
	SceCtrlData pad;

	if (cached_valid && now - cached_time < 2000)
		return cached;
	controller_initialize();
	memset(&pad, 0, sizeof(pad));
	pad.Lx = pad.Ly = 128;
	sceCtrlPeekBufferPositive(&pad, 1);
	if (walk_test)
	{
		pad.Buttons |= PSP_CTRL_TRIANGLE;
		pad.Lx = 170;
	}
	input_script_apply(&pad);
	{
		/* build 77: select + left opens the debug menu (psp_debug.c; the
		level pass modes, select + left with "passes" since build 53, are
		in it now). While it is open the game sees an idle pad */
		extern int psp_debug_input(unsigned int buttons);

		pad_swallowed = psp_debug_input(pad.Buttons);
		if (pad_swallowed)
		{
			pad.Buttons = 0;
			pad.Lx = pad.Ly = 128;
		}
	}
	if (!pad_swallowed)
	{
		/* select + right: a screenshot of the next frame (build 34) */
		extern volatile int psp_screenshot_requested;
		static int shot_held = 0;
		int shot = (pad.Buttons & (PSP_CTRL_SELECT | PSP_CTRL_RIGHT)) == (PSP_CTRL_SELECT | PSP_CTRL_RIGHT);

		if (shot && !shot_held)
			psp_screenshot_requested = 1;
		shot_held = shot;
	}
	psp_pad_buttons = pad.Buttons;
	psp_pad_lx = pad.Lx;
	psp_pad_ly = pad.Ly;
	psp_pad_reads++;
	cached = pad;
	cached_time = now;
	cached_valid = 1;
	return pad;
}

SDL_JoystickID *SDL_GetGamepads(int *count)
{
	SDL_JoystickID *ids = malloc(2 * sizeof(SDL_JoystickID));

	if (!ids)
	{
		if (count) *count = 0;
		return NULL;
	}
	ids[0] = PSP_GAMEPAD_ID;
	ids[1] = 0;
	if (count) *count = 1;
	return ids;
}

SDL_Gamepad *SDL_OpenGamepad(SDL_JoystickID id) { return id == PSP_GAMEPAD_ID ? (SDL_Gamepad *)&controller_ready : NULL; }
SDL_Gamepad *SDL_GetGamepadFromID(SDL_JoystickID id) { return SDL_OpenGamepad(id); }
SDL_GamepadType SDL_GetGamepadType(SDL_Gamepad *gamepad) { (void)gamepad; return SDL_GAMEPAD_TYPE_XBOXONE; }
bool SDL_RumbleGamepad(SDL_Gamepad *gamepad, Uint16 low, Uint16 high, Uint32 milliseconds)
{ (void)gamepad; (void)low; (void)high; (void)milliseconds; return false; }

/* menus and play want different controls (build 27): while the game's
menus are up (the main menu, the pause menu, dialogs) the buttons are the
pad's own (cross A, circle B, square X, triangle Y, the D-pad and the nub to
move); in play, the face buttons move, the nub looks, the D-pad is A, B, X
and Y (jump, melee, action/reload, switch weapon), R fires, L throws a
grenade, select tapped zooms, and select held shifts the D-pad and L (below) */
extern unsigned char ui_widgets_active(void);

static int in_menus(void)
{
	return ui_widgets_active() != 0;
}

bool SDL_GetGamepadButton(SDL_Gamepad *gamepad, SDL_GamepadButton button)
{
	unsigned int b = read_controls().Buttons;

	(void)gamepad;
	if (in_menus())
	{
		switch (button)
		{
		case SDL_GAMEPAD_BUTTON_SOUTH: return (b & PSP_CTRL_CROSS) != 0;
		case SDL_GAMEPAD_BUTTON_EAST: return (b & PSP_CTRL_CIRCLE) != 0;
		case SDL_GAMEPAD_BUTTON_WEST: return (b & PSP_CTRL_SQUARE) != 0;
		case SDL_GAMEPAD_BUTTON_NORTH: return (b & PSP_CTRL_TRIANGLE) != 0;
		case SDL_GAMEPAD_BUTTON_DPAD_UP: return (b & PSP_CTRL_UP) != 0;
		case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return (b & PSP_CTRL_DOWN) != 0;
		case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return (b & PSP_CTRL_LEFT) != 0;
		case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return (b & PSP_CTRL_RIGHT) != 0;
		case SDL_GAMEPAD_BUTTON_START: return (b & PSP_CTRL_START) != 0;
		case SDL_GAMEPAD_BUTTON_BACK: return (b & PSP_CTRL_SELECT) != 0;
		default: return false;
		}
	}
	{
		/* select is a shift: with it held, the D-pad gives the flashlight (up)
		and crouch (down), and L the grenade switch (build 77; was left); tapped
		alone, it zooms (build 29). Select + right takes a screenshot, select +
		left opens the debug menu (read_controls; builds 34, 77) */
		static int select_held = 0, select_used = 0;
		/* build 89: when the tap was, and whether one is live. b29-b88 kept
		only zoom_until (0 at start) and tested (int)(zoom_until - now) > 0,
		which turns true by itself once the 32-bit microsecond clock passes
		2^31 (35.8 minutes after the PSP booted) or 35.8 minutes after the
		last tap: a phantom zoom held until the next select tap (review) */
		static unsigned int zoom_at = 0;
		static int zoom_live = 0;
		unsigned int now = sceKernelGetSystemTimeLow();
		int shift = (b & PSP_CTRL_SELECT) != 0;

		/* build 77: select held for the debug menu isn't a zoom tap */
		if (pad_swallowed)
			select_used = 1;
		if (shift)
		{
			if (b & (PSP_CTRL_UP | PSP_CTRL_DOWN | PSP_CTRL_LEFT | PSP_CTRL_RIGHT | PSP_CTRL_LTRIGGER))
				select_used = 1;
			select_held = 1;
		}
		else if (select_held)
		{
			if (!select_used)
			{
				zoom_at = now;
				zoom_live = 1;
			}
			select_held = select_used = 0;
		}
		switch (button)
		{
		case SDL_GAMEPAD_BUTTON_SOUTH: return !shift && (b & PSP_CTRL_UP) != 0;
		case SDL_GAMEPAD_BUTTON_EAST: return !shift && (b & PSP_CTRL_DOWN) != 0;
		case SDL_GAMEPAD_BUTTON_WEST: return !shift && (b & PSP_CTRL_LEFT) != 0;
		case SDL_GAMEPAD_BUTTON_NORTH: return !shift && (b & PSP_CTRL_RIGHT) != 0;
		case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return shift && (b & PSP_CTRL_UP) != 0;
		case SDL_GAMEPAD_BUTTON_LEFT_STICK: return shift && (b & PSP_CTRL_DOWN) != 0;
		/* build 77: the grenade switch is select + L (select + left opens
		the debug menu) */
		case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return shift && (b & PSP_CTRL_LTRIGGER) != 0;
		case SDL_GAMEPAD_BUTTON_START: return (b & PSP_CTRL_START) != 0;
		case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
			if (zoom_live && now - zoom_at >= 150000)
				zoom_live = 0;
			return zoom_live;
		default: return false;
		}
	}
}

static Sint16 stick(unsigned char value)
{
	int centred = (int)value - 128;

	if (centred > -24 && centred < 24)
		return 0;
	return (Sint16)(centred * 256 > 32767 ? 32767 : centred * 256 < -32768 ? -32768 : centred * 256);
}

Sint16 SDL_GetGamepadAxis(SDL_Gamepad *gamepad, SDL_GamepadAxis axis)
{
	SceCtrlData pad = read_controls();

	(void)gamepad;
	if (in_menus())
	{
		/* the nub moves through the menus */
		switch (axis)
		{
		case SDL_GAMEPAD_AXIS_LEFTX: return stick(pad.Lx);
		case SDL_GAMEPAD_AXIS_LEFTY: return stick(pad.Ly);
		default: return 0;
		}
	}
	switch (axis)
	{
	case SDL_GAMEPAD_AXIS_LEFTX:
		return (pad.Buttons & PSP_CTRL_SQUARE) ? -32767 : (pad.Buttons & PSP_CTRL_CIRCLE) ? 32767 : 0;
	case SDL_GAMEPAD_AXIS_LEFTY:
		return (pad.Buttons & PSP_CTRL_TRIANGLE) ? -32767 : (pad.Buttons & PSP_CTRL_CROSS) ? 32767 : 0;
	case SDL_GAMEPAD_AXIS_RIGHTX: return stick(pad.Lx);
	case SDL_GAMEPAD_AXIS_RIGHTY: return stick(pad.Ly);
	/* select + L switches grenades, without throwing one (build 77) */
	case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: return (pad.Buttons & PSP_CTRL_LTRIGGER) && !(pad.Buttons & PSP_CTRL_SELECT) ? 32767 : 0;
	case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return (pad.Buttons & PSP_CTRL_RTRIGGER) ? 32767 : 0;
	default: return 0;
	}
}
