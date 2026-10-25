# Build 143: the first playable tick of the w142 line (2026-10-25)

`review_b143.md` lists every change and why. Build 143 is build 141 with your hand-test fixes, the tock's exact speed levers on, and the B21 shore and horizon work. It has the same release shape as b141. [D]

`halo_psp_b143rel.zip`, EBOOT `<hash after the strict build>`, from Sierra-III `bea8aee` plus its profile (`pgo/campaign-b143.tgz`) and the 143 bump. It needs a bake made with the bake tool at this commit (the lab's **b143**): it carries the level glow maps, the gel faces and the HUD meters' own alphas. Older bakes draw as before for glow and gel faces. [D]

## What it restores and fixes

- **Restores:** the level's self-illumination (Autumn's engines, panels), the canopy's smoked glass, alert lights on the walls, dark scenes' level (`ditherlift`), the HUD bars' own alphas, the menu at full texture level, and the glow clamp.
- **Fixes:** the world showing before it loads; menu music restarting; doors sliding as one; marines' warped faces; the Pelican and level draws dropped for a frame; console text over scenes; half-drawn shore polygons; black cut pieces; and the far horizon's black areas.
- **Faster, exact:** the mixer's lock waits, the handle lookups, mechunk (a30 7.7→12.7 fps on the PSP-3000), and a layout from the TG score.

## On the PSP

Install as b141: unzip into `$psp`, then clear `init.txt`, `keep.txt` and `input.txt`. The log's first line should say `bring-up build 143`. Maps: a bake from this commit's bake tool (the lab's b143).

## What to try

- **B21, b30's flight** (the Pelican, shore, beach structure at t52, far horizon). Words: `farcutoff`, `flipwaitoff`, `nearcinefp`, `submergedcutold`.
- **Pelican at b30's start:** it should not vanish as it passes (`modelpretouchoff`). Marines in the cabin should not spike (`levelpretouchoff` for the level side).
- **a10:** the cryo monitors, the HUD bars filling at full health (`--no-meter-alphas` is bake-side), the tutorial lights, and the alert hallway's frame rate (`nogelfaces`).
- **The main menu:** no orange ring in the first frames, music running through its tracks (`attractmode`), the ring not too bright (`glownoclamp`).
- **a30 and d20 speed:** `mechunkoff` for comparison.
- **The Pelican canopy:** `glassnotint`.

## Known

- **B21:** a slight missing polygon at b30's far horizon, "in the usual place" (PSP-1000 40229418415). Farcut cleared the black areas; the far plane ×4 looks the same, so the cause is something else. [M by eye]
- gelfaces costs +8–15% in a10's hallway, provisionally on. [E]
- The canopy's side panes stay dim; the cryo monitors barely change.
- Open from your list: cutscene audio drift (B5), lights on world geometry (B7), the a10 t915 cutscene being slower (B9), cut-short animations (B10), the bridge floor shadow (B12), the tutorial lights' colour (B17), and Chief's white armour (B4). [U, `bugs.md`]

## Proof

`review_b143.md`, "Proof to collect": the profile is made; the release check rc143 is running.
