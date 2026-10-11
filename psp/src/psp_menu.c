/* psp_menu.c - the launch menu (build 66)

Shown before the game starts, on the PSP's debug screen (libpspdebug: a
plain text console on the frame buffer; the GE isn't up yet). Two pages:

- maps: every .map in ms0:/halo/maps (ui.map aside), with whether its
  texture pack (.ptex) is beside it. The chosen map goes into init.txt as
  the game's map_name command (a multiplayer map's scenario is
  levels\test\<name>\<name>, a campaign map's levels\<name>\<name>: the map
  header's type says which).
- options: the keep words that matter day to day, as toggles, written to
  keep.txt. Words the menu doesn't list (anything typed by hand) are kept.

Up/down move, left/right or cross toggle, L/R or triangle switch pages,
start saves both files and starts the game. Not shown when
ms0:/halo/input.txt (a scripted run: the emulator) or ms0:/halo/nomenu.txt
exists; the files are then read as they are, as before. */
#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <string.h>

#define DATA_ROOT "ms0:/halo"
#define MAXIMUM_MAPS 32
#define NAME_LENGTH 40

struct map_entry
{
	char name[NAME_LENGTH];
	int type; /* 0 campaign, 1 multiplayer, 2 ui, -1 unknown */
	int has_pack;
};

struct option
{
	const char *word;
	const char *label;
	/* a group: one of the group on at a time (0: on its own) */
	int group;
	int on;
};

/* build 93: trimmed (Sheepdog01): near2, near4, overlayslope1, overlayslope4,
decalall, flatshade, tdump, visdump, noboxtest, detailallfar, levelcompact and
interpolation left the menu. They still work typed into keep.txt, and the
menu keeps them there as "other words" */
static struct option options[] = {
	{ "nearauto", "near plane: by what is near the eye", 1, 0 },
	{ "nooverlay", "performance overlay OFF at start", 0, 0 },
	{ "nomeaudio", "sound mixed on the main CPU, not the ME", 0, 0 },
	/* build 114's switches (docs/build_b114.md): b113's defaults back */
	{ "list512", "display list 512 KB (b113's; 768 now)", 0, 0 },
	{ "sound1536", "sound cache 1536 KB (b113's; 1792 now)", 0, 0 },
	{ "nosoundplace", "sound cache: big sounds not kept at the top", 0, 0 },
	{ "seethrough160", "see-through draws: 160 a frame (b113's; 320 now)", 0, 0 },
	{ "listcachedall", "GE command words written through the cache", 0, 0 },
	{ "lean", "lean play: timers and overlay off (notimers nooverlay)", 0, 0 },
	/* build 113's switches (docs/build_b113.md) */
	{ "contrails", "Covenant tracers (contrails) drawn", 0, 0 },
	{ "zoommask", "zoom: the mask as a soft edge (blur stand-in)", 6, 0 },
	{ "zoommaskfull", "zoom: the mask at full strength", 6, 0 },
	{ "gebusy", "GE busy time in the log", 0, 0 },
	{ "gedefer", "GE calls written down, carried out at frame end", 0, 0 },
	{ "gemeon", "GE calls carried out on the ME (with gedefer)", 0, 0 },
	{ "weaksum", "b112's sleep checksum (the flip bug)", 0, 0 },
	{ "renderstates256", "render-state cache 256 (b112's)", 0, 0 },
	{ "benchrun", "benchmark: 4 views x 9 switches, ~6 min", 0, 0 },
	{ "assettrack", "asset track: the maps' _track packs", 5, 0 },
	{ "assetdetail", "asset track: the maps' _detail packs", 5, 0 },
#if PSP_DEBUG_TOOLS
	{ "meclassify", "ME speed test: 2 render jobs, measure only", 0, 0 },
#endif
	{ "hwprofile", "CPU counters in the log (benchrun: always)", 0, 0 },
	{ "handletest", "file handles: how many are free 15 s in", 0, 0 },
	{ "blackbox", "crash hunt: I/O traced in play, log saved each frame", 0, 0 },
	{ "crashplay", "crash test: a deliberate crash 20 s into play", 0, 0 },
	{ "lightwhite", "objects lit as before b102 (full light)", 0, 0 },
	{ "crumbs", "crash hunt: last steps drawn on the screen (slow)", 0, 0 },
	{ "detailauto", "detail maps: the GE's own level choice", 0, 0 },
	{ "noangle", "no slant buckets for texture levels", 0, 0 },
	{ "mipauto", "all textures: the GE's own level choice", 0, 0 },
	{ "nodetail", "no detail maps", 0, 0 },
	{ "noshadows", "no shadows", 0, 0 },
	{ "nodecals", "no decals", 0, 0 },
	{ "nohud", "no HUD", 0, 0 },
	{ "novblankthrottle", "Halo's frame pacing off", 0, 0 },
	{ "novfpu", "no VFPU", 0, 0 },
	{ "texsqueeze", "texture RAM cache capped at 1 MB: stream", 3, 0 },
	{ "texvramonly", "no texture RAM cache: VRAM only", 3, 0 },
	{ "geoasync", "geometry reads asynchronous", 0, 0 },
	{ "geo15", "campaign geometry cache 1.5 MB (less for textures)", 4, 0 },
	{ "geo2", "campaign geometry cache 2 MB (less for textures)", 4, 0 },
};
#define OPTION_COUNT (int)(sizeof(options) / sizeof(options[0]))
/* rows for options: 34 on the screen, less the 3-line header and 3 lines
around the list */
#define OPTION_ROWS 26
/* the debug screen is 68 columns: a line that fills all 68 wraps by itself
and its newline then adds a blank row (b91: every option took two rows, so
the list ran past the bottom and wrapped over the top). Lines stay at 67 */
/* build 92: the menu drawn into two buffers in turn (VRAM, uncached view) */
#define MENU_VRAM 0x44000000u
#define MENU_BUFFER_BYTES (512u * 272u * 4u)

static struct map_entry maps[MAXIMUM_MAPS];
static int map_count = 0;
/* words in keep.txt the menu doesn't know: kept as typed */
static char extra_words[256] = "";

static int file_exists(const char *path)
{
	SceIoStat status;

	return sceIoGetstat(path, &status) >= 0;
}

static int map_type(const char *path)
{
	SceUID file = sceIoOpen(path, PSP_O_RDONLY, 0);
	unsigned char header[0x64];
	int type = -1;

	if (file < 0)
		return -1;
	if (sceIoRead(file, header, sizeof(header)) == (int)sizeof(header) && header[0] == 'd' && header[1] == 'a' &&
		header[2] == 'e' && header[3] == 'h')
		type = header[0x60] | (header[0x61] << 8);
	sceIoClose(file);
	return type;
}

static void scan_maps(void)
{
	SceUID directory = sceIoDopen(DATA_ROOT "/maps");
	SceIoDirent entry;

	map_count = 0;
	if (directory < 0)
		return;
	memset(&entry, 0, sizeof(entry));
	while (map_count < MAXIMUM_MAPS && sceIoDread(directory, &entry) > 0)
	{
		int length = strlen(entry.d_name);
		char path[128];

		if (length > 4 && length < NAME_LENGTH + 4 && strcmp(entry.d_name + length - 4, ".map") == 0 &&
			strcmp(entry.d_name, "ui.map") != 0)
		{
			struct map_entry *map = &maps[map_count];

			memcpy(map->name, entry.d_name, length - 4);
			map->name[length - 4] = '\0';
			snprintf(path, sizeof(path), DATA_ROOT "/maps/%s", entry.d_name);
			map->type = map_type(path);
			snprintf(path, sizeof(path), DATA_ROOT "/maps/%s.ptex", map->name);
			map->has_pack = file_exists(path);
			map_count++;
		}
		memset(&entry, 0, sizeof(entry));
	}
	sceIoDclose(directory);
	/* alphabetical */
	{
		int i, j;

		for (i = 1; i < map_count; i++)
			for (j = i; j > 0 && strcmp(maps[j - 1].name, maps[j].name) > 0; j--)
			{
				struct map_entry swap = maps[j - 1];

				maps[j - 1] = maps[j];
				maps[j] = swap;
			}
	}
}

/* the current map from init.txt's map_name line: the last path element */
static int current_map(void)
{
	SceUID file = sceIoOpen(DATA_ROOT "/init.txt", PSP_O_RDONLY, 0);
	char text[256];
	int length, i;

	if (file < 0)
		return 0;
	length = sceIoRead(file, text, sizeof(text) - 1);
	sceIoClose(file);
	if (length <= 0)
		return 0;
	text[length] = '\0';
	{
		char *line = strstr(text, "map_name");
		char *end, *last;

		if (!line)
			return 0;
		line += 8;
		while (*line == ' ' || *line == '\t')
			line++;
		end = line;
		while (*end && *end != '\r' && *end != '\n' && *end != ' ')
			end++;
		*end = '\0';
		last = line;
		for (i = 0; line[i]; i++)
			if (line[i] == '\\' || line[i] == '/')
				last = line + i + 1;
		for (i = 0; i < map_count; i++)
			if (strcmp(maps[i].name, last) == 0)
				return i;
	}
	return 0;
}

static void load_options(void)
{
	SceUID file = sceIoOpen(DATA_ROOT "/keep.txt", PSP_O_RDONLY, 0);
	char text[256], *word, *save = NULL;
	int length, i;

	extra_words[0] = '\0';
	if (file < 0)
		return;
	length = sceIoRead(file, text, sizeof(text) - 1);
	sceIoClose(file);
	if (length <= 0)
		return;
	text[length] = '\0';
	for (i = 0; text[i]; i++)
		if (text[i] == '\n' || text[i] == '\r' || text[i] == '\t' || text[i] == ',')
			text[i] = ' ';
	word = strtok_r(text, " ", &save);
	while (word)
	{
		int known = 0;

		for (i = 0; i < OPTION_COUNT; i++)
			if (strcmp(options[i].word, word) == 0)
			{
				options[i].on = 1;
				known = 1;
			}
		if (!known && strlen(extra_words) + strlen(word) + 2 < sizeof(extra_words))
		{
			strcat(extra_words, word);
			strcat(extra_words, " ");
		}
		word = strtok_r(NULL, " ", &save);
	}
}

static void save_files(int map)
{
	SceUID file = sceIoOpen(DATA_ROOT "/keep.txt", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0666);
	char text[512];
	int length = 0, i;

	if (file >= 0)
	{
		for (i = 0; i < OPTION_COUNT; i++)
			if (options[i].on)
				length += snprintf(text + length, sizeof(text) - length, "%s ", options[i].word);
		length += snprintf(text + length, sizeof(text) - length, "%s\n", extra_words);
		sceIoWrite(file, text, length);
		sceIoClose(file);
	}
	if (map >= 0 && map < map_count)
	{
		file = sceIoOpen(DATA_ROOT "/init.txt", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0666);
		if (file >= 0)
		{
			if (maps[map].type == 0)
				length = snprintf(text, sizeof(text), "map_name levels\\%s\\%s\n", maps[map].name, maps[map].name);
			else
				length = snprintf(text, sizeof(text), "map_name levels\\test\\%s\\%s\n", maps[map].name, maps[map].name);
			sceIoWrite(file, text, length);
			sceIoClose(file);
		}
	}
}

static void draw(int page, int cursor, int map, int build)
{
	int i;

	pspDebugScreenSetXY(0, 0);
	pspDebugScreenSetTextColor(0xFFFFFFFF);
	pspDebugScreenPrintf("Halo PSP build %d        %s\n", build,
		page == 0 ? "MAPS  (R: options)" : "OPTIONS  (L: maps)");
	pspDebugScreenPrintf("up/down move, left/right/cross toggle, START plays\n\n");
	if (page == 0)
	{
		for (i = 0; i < map_count; i++)
		{
			pspDebugScreenSetTextColor(i == cursor ? 0xFF00FFFF : 0xFFFFFFFF);
			pspDebugScreenPrintf(" %c %-24s %-12s %s\n", i == map ? '*' : ' ', maps[i].name,
				maps[i].type == 1 ? "multiplayer" : maps[i].type == 0 ? "campaign" : "?",
				maps[i].has_pack ? "textures" : "NO TEXTURE PACK");
		}
		if (!map_count)
			pspDebugScreenPrintf(" no maps in ms0:/halo/maps\n");
	}
	else
	{
		/* build 91: the flag first, then what it does; and a scrolling
		window, since the list is longer than the 34-row screen (rows past
		the bottom wrapped to the top and wrote over the header and the
		controls line, which flickered as the cursor moved) */
		int first = cursor - OPTION_ROWS / 2;

		if (first > OPTION_COUNT - OPTION_ROWS)
			first = OPTION_COUNT - OPTION_ROWS;
		if (first < 0)
			first = 0;
		pspDebugScreenSetTextColor(0xFFAAAAAA);
		{
			char note[40] = "";

			if (first > 0)
				snprintf(note, sizeof(note), "... %d more above", first);
			pspDebugScreenPrintf("     %-62s\n", note);
		}
		for (i = first; i < OPTION_COUNT && i < first + OPTION_ROWS; i++)
		{
			pspDebugScreenSetTextColor(i == cursor ? 0xFF00FFFF : 0xFFFFFFFF);
			pspDebugScreenPrintf(" [%c] %-16s %-45.45s\n", options[i].on ? 'x' : ' ', options[i].word, options[i].label);
		}
		pspDebugScreenSetTextColor(0xFFAAAAAA);
		{
			char note[40] = "";

			if (i < OPTION_COUNT)
				snprintf(note, sizeof(note), "... %d more below", OPTION_COUNT - i);
			pspDebugScreenPrintf("     %-62s\n", note);
		}
		pspDebugScreenPrintf(" other words kept: %-49.49s", extra_words);
	}
	pspDebugScreenSetTextColor(0xFFFFFFFF);
}

static void toggle(int i)
{
	int j;

	if (options[i].group)
	{
		int was = options[i].on;

		for (j = 0; j < OPTION_COUNT; j++)
			if (options[j].group == options[i].group)
				options[j].on = 0;
		options[i].on = !was;
	}
	else
		options[i].on = !options[i].on;
}

void psp_menu(int build)
{
	SceCtrlData pad;
	unsigned int last = 0;
	int page = 0, cursor = 0, map;
	int done = 0, repeat = 0;

	if (file_exists(DATA_ROOT "/input.txt") || file_exists(DATA_ROOT "/nomenu.txt"))
		return;
	int back = 0, dump_menu = -1;

	scan_maps();
	load_options();
	map = current_map();
	cursor = map;
	pspDebugScreenInit();
	pspDebugScreenSetBackColor(0xFF201008);
	pspDebugScreenClear();
	sceCtrlSetSamplingCycle(0);
	sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
	while (!done)
	{
		int count = page == 0 ? map_count : OPTION_COUNT;
		unsigned int pressed;
		static int drawn_page = -1, drawn_cursor = -1, drawn_map = -1, drawn_options = -1;
		int options_now = 0, k;

		/* build 71: redrawn only when something changed, at the start of a
		vertical blank. The debug screen draws straight into the picture on
		show, so clearing and redrawing it 16 times a second flickered (Sheepdog01,
		b70) */
		for (k = 0; k < OPTION_COUNT; k++)
			options_now |= options[k].on << k;
		if (page != drawn_page || cursor != drawn_cursor || map != drawn_map || options_now != drawn_options)
		{
			/* build 92: drawn whole into the buffer not on show, then shown
			at the next vertical blank. b91's scrolling list redrew every
			line on each move straight into the picture on show, which takes
			longer than a blank: the D-pad flicker (Sheepdog01, b91) */
			back = !back;
			pspDebugScreenSetBase((u32 *)(MENU_VRAM + back * MENU_BUFFER_BYTES));
			pspDebugScreenClear();
			draw(page, cursor, map, build);
			sceKernelDcacheWritebackAll();
			sceDisplayWaitVblankStart();
			sceDisplaySetFrameBuf((void *)(MENU_VRAM + back * MENU_BUFFER_BYTES), 512, PSP_DISPLAY_PIXEL_FORMAT_8888,
				PSP_DISPLAY_SETBUF_NEXTFRAME);
			drawn_page = page;
			drawn_cursor = cursor;
			drawn_map = map;
			drawn_options = options_now;
		}
		sceCtrlReadBufferPositive(&pad, 1);
		pressed = pad.Buttons & ~last;
		/* held up/down repeats after a moment */
		if ((pad.Buttons & (PSP_CTRL_UP | PSP_CTRL_DOWN)) && (pad.Buttons & (PSP_CTRL_UP | PSP_CTRL_DOWN)) == (last & (PSP_CTRL_UP | PSP_CTRL_DOWN)))
		{
			if (++repeat > 8)
			{
				pressed |= pad.Buttons & (PSP_CTRL_UP | PSP_CTRL_DOWN);
				repeat = 6;
			}
		}
		else
			repeat = 0;
		last = pad.Buttons;
		if ((pressed & PSP_CTRL_UP) && count)
			cursor = (cursor + count - 1) % count;
		if ((pressed & PSP_CTRL_DOWN) && count)
			cursor = (cursor + 1) % count;
		if (pressed & (PSP_CTRL_RTRIGGER | PSP_CTRL_LTRIGGER | PSP_CTRL_TRIANGLE))
		{
			page = !page;
			cursor = page == 0 ? map : 0;
		}
		if (pressed & (PSP_CTRL_CROSS | PSP_CTRL_LEFT | PSP_CTRL_RIGHT))
		{
			if (page == 0 && map_count)
				map = cursor;
			else if (page == 1)
				toggle(cursor);
		}
		if (pressed & PSP_CTRL_START)
			done = 1;
		/* build 92: ms0:/halo/menudump.txt (emulator test): the options page
		with the cursor 15 down, the picture on show written raw (512 x 272,
		32-bit) to menudump.raw, and on */
		if (dump_menu < 0)
			dump_menu = file_exists(DATA_ROOT "/menudump.txt");
		if (dump_menu == 1)
		{
			page = 1;
			cursor = 15;
			dump_menu = 2;
			continue;
		}
		if (dump_menu == 2)
		{
			SceUID file = sceIoOpen(DATA_ROOT "/menudump.raw", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0666);

			if (file >= 0)
			{
				sceIoWrite(file, (void *)(MENU_VRAM + back * MENU_BUFFER_BYTES), MENU_BUFFER_BYTES);
				sceIoClose(file);
			}
			done = 1;
		}
		sceKernelDelayThread(60000);
	}
	save_files(map);
	pspDebugScreenSetBase((u32 *)(MENU_VRAM + back * MENU_BUFFER_BYTES));
	pspDebugScreenClear();
	pspDebugScreenSetXY(0, 0);
	pspDebugScreenPrintf("Loading %s...\n", map_count ? maps[map].name : "the game");
}
