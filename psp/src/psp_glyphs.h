/* psp_glyphs.h - the button glyph sheet's layout (tools/psp_glyphs.py writes it, beside
psp_glyphs.png): each glyph's cell in pixels, x y width height */
#define PSP_GLYPH_SHEET_WIDTH 256
#define PSP_GLYPH_SHEET_HEIGHT 128
enum psp_glyph
{
	PSP_GLYPH_CROSS,
	PSP_GLYPH_CIRCLE,
	PSP_GLYPH_SQUARE,
	PSP_GLYPH_TRIANGLE,
	PSP_GLYPH_DPAD_UP,
	PSP_GLYPH_DPAD_DOWN,
	PSP_GLYPH_DPAD_LEFT,
	PSP_GLYPH_DPAD_RIGHT,
	PSP_GLYPH_DPAD,
	PSP_GLYPH_L,
	PSP_GLYPH_R,
	PSP_GLYPH_NUB,
	PSP_GLYPH_FACE,
	PSP_GLYPH_START,
	PSP_GLYPH_SELECT,
	PSP_GLYPH_SELECT_UP,
	PSP_GLYPH_SELECT_DOWN,
	PSP_GLYPH_SELECT_L,
	PSP_GLYPH_COUNT
};
static const unsigned short psp_glyph_cells[PSP_GLYPH_COUNT][4] =
{
	{ 0, 0, 32, 32 }, /* cross */
	{ 32, 0, 32, 32 }, /* circle */
	{ 64, 0, 32, 32 }, /* square */
	{ 96, 0, 32, 32 }, /* triangle */
	{ 128, 0, 32, 32 }, /* dpad_up */
	{ 160, 0, 32, 32 }, /* dpad_down */
	{ 192, 0, 32, 32 }, /* dpad_left */
	{ 224, 0, 32, 32 }, /* dpad_right */
	{ 0, 32, 32, 32 }, /* dpad */
	{ 32, 32, 32, 32 }, /* l */
	{ 64, 32, 32, 32 }, /* r */
	{ 96, 32, 32, 32 }, /* nub */
	{ 128, 32, 32, 32 }, /* face */
	{ 160, 32, 64, 32 }, /* start */
	{ 0, 64, 64, 32 }, /* select */
	{ 64, 64, 96, 32 }, /* select_up */
	{ 160, 64, 96, 32 }, /* select_down */
	{ 0, 96, 96, 32 }, /* select_l */
};
