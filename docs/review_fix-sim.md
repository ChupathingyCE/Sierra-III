# Review: fix-sim (H31, H25/H26, H27)

Pass 1 of b139's hand-test fixes: the Warthog flip (H31), vehicles clipping and wobbling (H25, H26), and what each vehicle type costs a tick (H27). Branch `fix-sim`, off `w139`.

**Profile:** `players.c` changes in the release build (the flip's wake), and `objects.c` and `units.c` carry new debug-only hooks. b139's PGO profile must be re-recorded after this merges; a `PSP_PGO_STRICT=1` build refuses to link until it is.

## 1. The flip wakes the vehicle it rights (H31), a fix

`psp_fixups.py`, `FIXES_WAKE["source/game/players.c#wake_flip"]`:

```c
		target_vehicle->vehicle.unknown42A = 0;
		if (!PSP_KEEP_ONCE("flipwakeoff"))
		{
			void psp_wake(long object_index); /* psp_fixups.py (FIXES_WAKE) */

			psp_wake(player->action_object_index);
		}
```

**Why:** wake-on-write (on by default since b136) checksums a sleeping object only when a known writer marked it. The player's flip writes the vehicle's upending flag and approach side into a vehicle that has usually been asleep since it came to rest upside down. Nothing marked it, so the sleeper kept its old checksum, its update never ran, the torque never started and the flag never cleared. The prompt never came back. wakecheck's hands-off ladder has no player to press the button, so it couldn't see this writer. [D]

**Evidence [E]:** the flip fixture (`flipfixture`, debug tools) puts an upside-down Warthog on b30's beach; the action button is held from game time 3000.
- Default: the flip sets upending, the sleep check sees the mark and updates the vehicle, the torque starts that tick, and up.k goes from -0.99 to 0.95 in 29 ticks. The flag clears and the vehicle is upright.
- `flipwakeoff`: upending is set, but the vehicle stays asleep with its kept checksum. No torque, up.k stays -0.99: b139's regression, reproduced.

**Cost:** one `psp_wake` per flip, nothing in steady play. [D]

**Keep word:** `flipwakeoff` turns the wake off (for A/Bs only).

## 2. Clipping and wobble (H25, H26): no speed-up is the cause

**What was built:** `drivetest` (debug tools on the PSP; always on the PC trace build), `FIXES_DRIVE["source/units/units.c#drivetest"]`. At game time `drivetestatN` (2700) it makes a Warthog on b30's beach, seats the player and plays one fixed drive through the unit's control data: 150 ticks of throttle into a boulder (`boulder_crouch`), 60 of reverse, 300 of full-lock left, 300 of full-lock right. Each tick it logs the position, velocity, facing, up vector and spin. `tools/drivecmp.py` reads the logs and gives, per leg, speed, turn, wobble (the up vector's rate), lowest up.k, the closest approach to the target, the bounce, and the largest gap in place between two runs.

**Evidence [E]** (emulator, fixed ticks and seed, against the PC build at the same engine pin):

| Leg | PC | PSP default | PSP, every speed-up off |
|---|---|---|---|
| ram 150 ticks | 1.69 wu/s, wobble 35.6 deg/s, up.k 0.624, closest 0.465, bounce 0.69 | identical | identical |
| reverse 60 | 0.62 wu/s, wobble 20.1 | identical | identical |
| left 300 | 1.86 wu/s, turn 183.6 deg/s, wobble 13.5 | identical (gap 0.11 wu) | identical to default |
| right 300 | 2.49 wu/s, turn -209.6, wobble 15.1 | 2.51, -208.0, wobble 13.4 (gap 1.66 wu) | identical to default |

- The PSP build matches the PC to four decimals through the ram, the clip and the bounce. In this fixture the clip-then-bounce happens on the PC too: it is the engine's own behaviour.
- The gap in the circles is float rounding: a last-digit difference from step 2, grown over 750 ticks of full lock. The PSP wobbles no more than the PC (13.4 deg/s against 15.1).
- The drive is bit-identical with every collision, sleep and pose speed-up on, all off, and each family alone off (surface test, sphere walk, sleep, poses). Their checks (`spherememocheck surfacetestcheck posingcheck poseaheadcheck wakecheck`) found 0 differences over the drive.

**Checked against the engine pin, and one open question** (results in the lab's findings):
- **The engine pin: settled.** b135 moved the pin from v0.7.4e to v0.7.5d, close to when H25 started. The same drive on the PC at v0.7.4e is identical to v0.7.5d to every printed digit [E], so the pin move didn't change how the vehicle collides.
- **The hardware.** The same drive on a PSP says whether the PSP's own FPU drives the emulator's drive.

If the hardware matches too, what the hand test saw is drawn rather than simulated (the camera, or how a pose is shown at the PSP's frame rate). That needs a new recon; nothing is changed for it on this branch. [U]

## 3. What each vehicle type costs a tick (H27), an instrument

**What was built:** `vehiclecost` (debug tools). Each vehicle's update is timed by its definition, and each profile window logs `vehiclecost: ms per tick (updates per tick, us each)` per type. The time includes the update's children (the seated riders) [D]. The "objects_update by type" line's vehicle part splits the same time into the type update, damage and node matrices.

`psp_fixups.py` (the object loop in `objects.c`, debug tools only):

```c
						if (object_header->type == _object_type_vehicle)
							psp_vehicle_account(object_header->datum->definition_index, (psp_microseconds() - psp_object_start) * psp_object_scale);
```

**Evidence [E]** (emulator clock: compare within a run, never against the PSP's milliseconds):

| Map, stretch | Type | us an update | ms a tick (all of that type) |
|---|---|---|---|
| b30, two Pelicans arriving | pelican | 1,576 | 3.14 (two) |
| b30, Pelicans landed, unloading | pelican | 491-596 | 1.0-1.2 (two) |
| b30, Pelicans leaving | pelican | 834-887 | 1.67-1.77 (two) |
| b30, Warthog at rest | warthog | 3 (asleep) | 0.00 |
| b30, driven: ram and reverse | warthog | | 3.05 (the driven one) |
| b30, driven: full-lock circles | warthog | | 2.50 |
| c40, hands-off, parked | wraith | 77-85 (awake) | 0.08 |
| c40, hands-off, parked | banshee | 3 (asleep) | 0.00 |

- **The type update is most of it.** In the ram window, vehicles take 4.81 ms a tick: the type update 4.28 (89%), node matrices 0.18, damage 0.02, and the rest (riders and sleep checks) about 0.33.
- **A flier costs about three times more in flight than parked.** The Pelican: 1,576 us an update arriving, 491-596 landed.
- **Parked Pelicans stay awake** at about half a millisecond each.

Not measured yet: a Banshee or a Scorpion in motion (nothing drives or flies them hands-off). The hardware run of the same drive gives these numbers on the PSP.

**Cost:** none in a release build: debug tools only.

## Tools and fixtures

- `flipfixture` gains `flipfixtureatN` and `flippressN` (the fixture's time and a held action button), so a batch can play the flip hands-off. Debug tools only.
- `tools/drivecmp.py`: `label=log` arguments; the first is the reference.
