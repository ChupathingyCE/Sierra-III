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

static struct option options[] = {
	{ "nearauto", "near plane: by what is near the eye (nearauto)", 1, 0 },
	{ "near4", "near plane: x4 (near4)", 1, 0 },
	{ "near2", "near plane: x2 (near2)", 1, 0 },
	{ "nooverlay", "performance overlay OFF at start (nooverlay)", 0, 0 },
	{ "nomeaudio", "sound mixed on the main CPU, not the ME (nomeaudio)", 0, 0 },
	{ "benchrun", "benchmark: 4 views x 5 switches, ~4 min (benchrun)", 0, 0 },
	{ "levelcompact", "level vertices at 16 bits (levelcompact)", 0, 0 },
	{ "meclassify", "ME speed test: 2 render jobs, measure only (meclassify)", 0, 0 },
	{ "interpolation", "smooth motion between game ticks (interpolation)", 0, 0 },
	{ "hwprofile", "hardware profile in the log (hwprofile)", 0, 0 },
	{ "detailauto", "detail maps: the GE's own level choice (detailauto)", 0, 0 },
	{ "noangle", "no slant buckets for texture levels (noangle)", 0, 0 },
	{ "mipauto", "all textures: the GE's own level choice (mipauto)", 0, 0 },
	{ "overlayslope1", "wall symbols: one pixel of slant (overlayslope1)", 2, 0 },
	{ "overlayslope4", "wall symbols: four pixels of slant (overlayslope4)", 2, 0 },
	{ "decalall", "decals: draw even under 2 pixels (decalall)", 0, 0 },
	{ "flatshade", "flat shading, as builds 14-61 (flatshade)", 0, 0 },
	{ "nodetail", "no detail maps (nodetail)", 0, 0 },
	{ "noshadows", "no shadows (noshadows)", 0, 0 },
	{ "nodecals", "no decals (nodecals)", 0, 0 },
	{ "nohud", "no HUD (nohud)", 0, 0 },
	{ "novblankthrottle", "Halo's frame pacing off (novblankthrottle)", 0, 0 },
	{ "noboxtest", "classify every level draw (noboxtest)", 0, 0 },
	{ "detailallfar", "detail passes past the fade too (detailallfar)", 0, 0 },
	{ "novfpu", "no VFPU (novfpu)", 0, 0 },
	{ "texsqueeze", "texture RAM cache capped at 1 MB: stream (texsqueeze)", 3, 0 },
	{ "texvramonly", "no texture RAM cache: VRAM only (texvramonly)", 3, 0 },
	{ "geoasync", "geometry reads asynchronous (geoasync)", 0, 0 },
	{ "tdump", "log see-through draws on screenshots (tdump)", 0, 0 },
	{ "visdump", "log the visibility walk (visdump)", 0, 0 },
};
#define OPTION_COUNT (int)(sizeof(options) / sizeof(options[0]))

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
		for (i = 0; i < OPTION_COUNT; i++)
		{
			pspDebugScreenSetTextColor(i == cursor ? 0xFF00FFFF : 0xFFFFFFFF);
			pspDebugScreenPrintf(" [%c] %s\n", options[i].on ? 'x' : ' ', options[i].label);
		}
		pspDebugScreenSetTextColor(0xFFAAAAAA);
		pspDebugScreenPrintf("\n other words kept: %s\n", extra_words);
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
	scan_maps();
	load_options();
	map = current_map();
	cursor = map;
	pspDebugScreenInit();
	pspDebugScreenSetBackColor(0xFF201008);
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
			sceDisplayWaitVblankStart();
			if (page != drawn_page)
				pspDebugScreenClear();
			draw(page, cursor, map, build);
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
		sceKernelDelayThread(60000);
	}
	save_files(map);
	pspDebugScreenClear();
	pspDebugScreenSetXY(0, 0);
	pspDebugScreenPrintf("Loading %s...\n", map_count ? maps[map].name : "the game");
}
