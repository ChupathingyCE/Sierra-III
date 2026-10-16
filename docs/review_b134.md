# Review: build 134

For Sheepdog01 to read and question before testing (CLAUDE.md, "How AI is used here"). b134 is a cleanup build, the pause point to pivot from (Sheepdog01, 2026-10-16): b133 (`review_b133.md`) with the shelved experiments taken out of the build, a profile check in the build, and its own profile. No new speed work. Labels: [M] measured on a PSP, [E] emulator, [D] read in code, [U] inference.

| Branch | What | Kind | Default | Words or switches | Review |
|---|---|---|---|---|---|
| `w134` | The shelf: nwbatch and its kin built only with `PSP_SHELF=1`; the other shelved experiments listed | cleanup | out of the build | `PSP_SHELF=1` | §1 |
| `w134` | The profile check: functions compiled without their counts listed, and a release refuses to link with any | build | on (the list); strict for releases | `PSP_PGO_STRICT=1` | §2 |
| `w134` | Presets and keep-word log lines to match | cleanup | on | none | §3 |
| `w134` | Its own profile; b132's kept code order | build | on | `PSP_TEXT_LAYOUT=b132` | §4 |

## Release check

Lab 38913248313, against b133 as built (`405ca12`): play and both seeds identical at every tick on d20, a50 and c20 (12,000 each) [E]; b30 and c40 shots right; the multiplayer build plays Blood Gulch. Checked at `3867f49`, which differs from the build commit `2b9b41d` only in the build number's constant.

## §1 The shelf (`psp_fixups.py`, `docs/shelf.md`)

**What.** Fixup groups tagged as shelved are applied only with `PSP_SHELF=1`:

```python
SHELVED_TAGS = {"nwbatch", "nwbatchwrites"}
SHELF = os.environ.get("PSP_SHELF", "") == "1"

def shelved(groups):
    return [(rel, pairs) for rel, pairs in groups if SHELF or rel.partition('#')[2] not in SHELVED_TAGS]
```

FIXES_136 (nwbatch) is the only shelved group: nothing else anchors on it, and no other code names it.

**Why.** Mothballed, not deleted (Sheepdog01): the experiments wait for review once the fundamental changes are under way. A shelved experiment has to cost nothing when off, and nwbatch didn't: switched off, its hooks made each object's update ~2.5 KB bigger, and b133 ran c10 6.9% slower on both PSPs [M]. The cache model put it on the instruction cache: the same instructions, 26% more tick instruction misses on c10 [E, lab 38903261679]. The other shelved experiments (the b123 ME hygiene words, `mewake`, `mewarmtest`, `mefinishslow`) are opt-in and outside the per-object path, so they stay built [U]; `docs/shelf.md` lists them and how to take one off the shelf.

## §2 The profile check (`psp_build.py`)

**What.** In a `PSP_PGO=use` build, the counted units compile with their warnings on (warnings change no code), so GCC's message that it dropped a function's counts reaches the build:

```python
checked = PGO == "use" and any(a.startswith("-fprofile-use=") for a in cmd)
plain = cmd
if checked:
    cmd = [a for a in cmd if a != "-w"]
```

`profile_mismatches()` collects every "does not match its profile data" line, except the two functions that differ between the counting build and the using one by design (`PGO_MODE_DEPENDENT`: `main` logs the profile's name, `psp_tick_account` dumps the counts); b134's first strict build listed exactly those two and refused to link. The build log counts them, `pgo_mismatch.txt` lists them, and `PSP_PGO_STRICT=1` refuses to link with any. A unit that only compiles with `-w` is compiled as before, unchecked.

**Why.** b133's `d9e7cf3` used b132's profile. GCC dropped the counts of five hot functions whose branches the batch had changed (`objects_update`'s hot code grew from 5.4 to 8.2 KB), silently, because `-w` hides the message. It ran 3–20% slower on both PSPs [M], and the ladders found it after an hour of PSP time. The check finds it in the build [D, tested locally: this code against b132's profile lists 4 functions, b133's keep-word and `statefullall` changes].

## §3 Presets and keep words

Preset 16 (nwbatch) is gone. The log's keep-word line for b133's words now names the shelf.

## §4 Its own profile, and b132's code order

The ten openings and b30's routes, counted at `5e9b9f5` (lab 38911172546, 38911179342); the code since differs in `psp_build.py` and the build number only. Linked in b132's kept code order, as b133 (`review_b133.md` §4).
