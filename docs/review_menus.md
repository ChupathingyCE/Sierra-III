# Review: the menus as the PSP's (for b139)

For Sheepdog01 to read and question before testing (CLAUDE.md, "How AI is used here"). Sheepdog01's direction (2026-10-19): the PSP's button glyphs wherever an Xbox button shows in the menus; each build's main menu without the other mode; the PSP named where the menus' text names the Xbox. All of it is in `psp_fixups.py`, `FIXES_MENUS`, plus the map choice in `psp/src/psp_sdl.c` (`psp_button_glyph`). Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

| Change | What | Kind | Default | Switch |
|---|---|---|---|---|
| §1 menu keys | Each screen's button key ("(B)=BACK (A)=SELECT", "(Y)=CREATE NEW") drawn as the PSP's glyph | look (Sheepdog01's direction) | on | `xboxglyphs` |
| §2 keyboard legend | The name entry's "=BACK" and "=ENTER" buttons covered and drawn as the PSP's | look (Sheepdog01's direction) | on | `xboxglyphs` |
| §3 the map by context | A glyph follows the pad's mode (menus or play), not the drawing site | fix | on | `glyphmapfixed` |
| §4 the other mode hidden | The campaign build's main menu without MULTIPLAYER; the multiplayer build's without CAMPAIGN | look (Sheepdog01's direction) | on | `menumultiplayer`, `menucampaign` |
| §5 the red warning | No red "event handler 'new game if no plyr profiles' failed" on the name entry | fix (the Xbox's own behaviour) | on | `menuwarnall` |
| §6 the wording | "Xbox" in the menus' string lists made the PSP's, at load | look (Sheepdog01's direction) | on | `xboxwording` |

## §1 The menus' button keys (`ui_widget.c`: `psp_menu_key_glyph`, in `widget_instance_render_recursive`)

- **Why the glyphs didn't show** [D]: the footers aren't text with button tokens, so the text's icon hook (`FIXES_CONTROLS[...ui_widget.c#glyphs]`) never ran. Each key is a widget of its own whose background picture is the Xbox's button; ChupathingyCE already tells them apart by their tag's name (`ui_mouse_key_button`: `a_butn`, `b_butn`, `y_butn`, `a_butn_sm`, …), and its label ("=SELECT") is the text box just right of it.
- **How:** when such a widget's picture is drawn, its quad is marked with the glyph (`psp_ge_glyph_next`), as the text's icons are, and the mark is cleared after the draw so an undrawn quad can't hand it on. The button's index is the engine's icon index (A, B, X, Y, black, white, … start, back), mapped with the menus' map: A ✕, B ○, X □, Y △, start START, back SELECT.
- **The colour:** the label's text colour, as the text's icons take the text's (§6 of `review_controls.md`).

## §2 The keyboard's legend (`virtual_keyboard.c` → `ui_widget.c`: `psp_keyboard_legend_glyphs`)

- **Why:** the name entry's "=BACK" and "=ENTER" are part of its full-screen background picture (ChupathingyCE's own comment in `virtual_keyboard.c`), so there is no widget to swap.
- **How:** after the background, each Xbox button's box (B at x 366–394, A at 468–496, y 412–442 in the menus' 640 × 480) is covered with the same picture's texels from x 336 on the same rows, where the band is empty, then the glyph is drawn in the button's own box (x 368–392 / 470–494, y 416–440) in the legend's blue. The places were measured on the walk's screenshots (menuwalk1, lab 39274068322), so they're good to about a texel [E]. `draw_bitmap_in_rect` gains a column to start from (`draw_bitmap_texel_x`), the twin of the port's `draw_bitmap_texel_y`. The picture itself is unchanged.

## §3 The glyph map by context (`psp_sdl.c`: `psp_button_glyph`)

- **Why:** the HUD's prompts asked for the play map and the menus' text for the menus' map, whichever mode the pad was in.
- **How:** the map is `in_menus()`'s (the pad's mode). `glyphmapfixed` gives the callers' choice back.
- **Icons with no PSP button in the menus** (black, white, the triggers, the stick clicks, the right stick): they keep the play map's glyph (Select + L, Select + ↑, L, R, Select + ↓, Select, the nub). The menus show those only when they describe play's controls, where the play button is the one the player needs.
- **Open:** the profile's button-settings screens describe play's A, B, X and Y. Where they do it in text, the menus' map now shows ✕ ○ □ △, not play's D-pad. Where they do it in a picture (the controller diagram), it stays the Xbox's.

## §4 The other mode's entry hidden (`ui_widget.c`: `psp_main_menu_without_other_mode`, from `cache_files.c` as each map's tags load)

- **Why:** the campaign build compiles the multiplayer menus' units out, yet MULTIPLAYER still opened them; the multiplayer build has no campaign.
- **How:** every widget list is checked for one entry of each mode, told by where it leads, not by its place:
  - **multiplayer:** a handler that opens a screen under `ui\shell\main_menu\multiplayer_type_select\`;
  - **campaign:** the handler function "new game if no plyr profiles".
  
  Both are searched in the entry and in its children. The list that has exactly one of each is the main menu. The build's other mode is taken out of its `child_widgets`, and each entry after it takes the place of the one before, so the list closes up and up and down move only between what's left. Once one is cut the list no longer matches, so tags loaded again aren't cut twice. The log says "menus: the main menu without the … entry".
- **Off:** `menumultiplayer` (campaign build) or `menucampaign` (multiplayer build).

## §5 The red warning on the name entry (`ui_widget_event_handler_functions.c`)

- **Cause** [D]: with no profiles, "new game if no plyr profiles" opens the name entry itself (`new_campaign_chosen`) and answers FALSE. The FALSE stops the handler's own next screen, the profile list, from opening over it. The Xbox logged that silently (`error(_error_silent, …)` in `event_handler_dispatch`, still in `debug.txt`). ChupathingyCE's `console_warning` for any FALSE drew it in red. So it isn't a PSP stub or a missing unit: the screen it leads to is the right one.
- **Fix:** no console warning for that one function. `menuwarnall` warns as before.

## §6 The wording (`ui_widget.c`: `psp_menu_strings_name_the_psp`)

- **How:** at load, in code (not at bake). Every `ustr` string list's strings are changed in place, the longer phrases first:

  | From | To |
  |---|---|
  | "Xbox video game system" | "PSP system" |
  | "Xbox console" | "PSP" |
  | "Xbox Hard Disk" | "Memory Stick" |
  | "Xbox" | "PSP" |

  Each also matches in the same capitals as the original. A swap only ever shortens a string, so each keeps its own room. The log gives the count by phrase, never the text.
- **Exact:** only those words change; nothing on the disc or in the bake changes. `xboxwording` leaves the strings as they are.
- **Open:** wording that doesn't say "Xbox" stays as it is ("controller", "hard disk" on its own). Sheepdog01 can read the screens and say what else should change.

## The checks

On the emulator [E]: lab batch `menuglyphs1` (see the lab's findings): the menus with and without the keep words, a strict run, the pause menu in a10, the multiplayer build's main menu.
