# Reports for ChupathingyCE

Things found while porting that belong upstream: what, where, and a draft to file. Sheepdog01 files them (the upstreaming rule in `CLAUDE.md`: he understands and can explain each, and the report says it was found with Claude). Labels: [D] read in code, [M] measured on a PSP, [E] emulator, [U] inference.

| # | What | State |
|---|---|---|
| 1 | Unarmed melee fires in the campaign | **to report** (Sheepdog01, 2026-10-19) |
| 2 | The loading chant's bytes in `progress_bar.c` | candidate: Sheepdog01's call |

## 1. Unarmed melee fires in the campaign

- **What:** with no weapon, pressing melee in the campaign (a10, before the first weapon) plays a melee: the third-person animation, then at its hit tick the melee rays from the head marker, and when one touches a wall or the floor, its material's impact sound (`units.c` `unit_cause_player_melee_damage` → `unit_melee_sound`) [D]. Sheepdog01 heard it in his PSP hand test; the PSP port runs the same code.
- **Where:** `source/units/bipeds.c`, the melee gate (~4343): `weapon_index == NONE && biped->unit.parent_seat_index == NONE` lets a player with no weapon melee. Added by `c9ee319a` ("Add the PC menus") for multiplayer gametypes whose loadout is none [D].
- **The original:** the Xbox and PC games block it: the gate was `!weapon_prevents_melee_attack(weapon_index)`, which is TRUE for no weapon (`weapons.c` ~821–836) [D].
- **Suggested fix:** keep the unarmed case to multiplayer (a game engine running, or the gametype's loadout), so the campaign plays as the original.
- **The port's own fix:** Sierra-III `4b97b14` blocks it in the campaign build only (`FIXES_SOUND` `bipeds.c#unarmed`, keep word `unarmedmelee`).

**Draft issue** (title: *Unarmed melee fires in the campaign*):

> In the campaign, before the player picks up a weapon (a10's opening), pressing melee plays a melee: the animation, and an impact sound when the melee rays touch a nearby wall. The Xbox and PC games don't allow melee with no weapon (`weapon_prevents_melee_attack` returns TRUE for none). The unarmed case in the melee gate (`source/units/bipeds.c`, ~line 4343, from c9ee319a) was added for multiplayer loadouts of none, but nothing limits it to multiplayer. Suggest gating it to multiplayer games so the campaign matches the original. Found while porting to the PSP (Sierra-III), with Claude's help; tested on the PSP port, not yet on the desktop build.

Before filing: confirm on ChupathingyCE's desktop build (a10, melee facing a wall before the first weapon), as the rule asks.

## 2. The loading chant's bytes in `progress_bar.c`

`compressed_monk_tone` (18,396 bytes of the game executable's audio) is in the source, against ChupathingyCE's own "Game files" rule. Details and a draft in `docs/research/chant_provenance.md`. Waiting on Sheepdog01's call.
