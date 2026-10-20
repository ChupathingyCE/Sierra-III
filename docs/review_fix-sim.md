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

**Evidence [E] and [M]** (fixed ticks and seed; the emulator and the PSP-1000 against the PC build at the same engine pin):

| Leg | PC | Emulator, default | Emulator, every speed-up off | PSP-1000 [M] |
|---|---|---|---|---|
| ram 150 ticks | 1.69 wu/s, wobble 35.6 deg/s, up.k 0.624, closest 0.465, bounce 0.69 | identical | identical | identical (gap 0.000 wu) |
| reverse 60 | 0.62 wu/s, wobble 20.1 | identical | identical | identical (gap 0.000) |
| left 300 | 1.86 wu/s, turn 183.6 deg/s, wobble 13.5 | identical (gap 0.11 wu) | identical to default | gap to the emulator 0.001 wu |
| right 300 | 2.49 wu/s, turn -209.6, wobble 15.1 | 2.51, -208.0, wobble 13.4 (gap 1.66 wu) | identical to default | 2.51, -208.0, wobble 13.4; gap to the emulator 0.003 wu (to the PC 1.66) |

- The PSP build matches the PC to four decimals through the ram, the clip and the bounce. In this fixture the clip-then-bounce happens on the PC too: it is the engine's own behaviour.
- The gap in the circles is float rounding: a last-digit difference from step 2, grown over 750 ticks of full lock. The PSP wobbles no more than the PC (13.4 deg/s against 15.1).
- The drive is bit-identical with every collision, sleep and pose speed-up on, all off, and each family alone off (surface test, sphere walk, sleep, poses). Their checks (`spherememocheck surfacetestcheck posingcheck poseaheadcheck wakecheck`) found 0 differences over the drive.

**Checked against the engine pin and the hardware:**
- **The engine pin: settled [E].** b135 moved the pin from v0.7.4e to v0.7.5d, close to when H25 started. The same drive on the PC at v0.7.4e is identical to v0.7.5d to every printed digit, so the pin move didn't change how the vehicle collides.
- **The hardware: settled [M].** The same drive on a PSP-1000 (b139's bake, game time 2700-3600, ended on its own `exitat` at 3632) matches the emulator's default drive to 4 decimals through the ram and reverse (gap 0.000 wu) and to 0.001 wu (left) and 0.003 wu (right) after that. The first printed difference is a last digit at step 2 (z 2.0922 on the PSP, 2.0923 in the emulator), so it is not bit for bit; it is the same drive, with the float rounding the circles amplify. Against the PC the right circle's gap is 1.66 wu, as the emulator's. The PSP wobbles no more than the PC (13.4 deg/s against 15.1).

**Conclusion:** the PSP's simulation drives like the PC's. The clip-then-bounce into the boulder and the wobble in the circles are the engine's own behaviour in the simulation, and neither a collision, sleep or pose speed-up, the engine pin nor the PSP's FPU adds to them. What was seen in the hand test is therefore in how the vehicle is drawn. This branch changes nothing for it; the recon to do next, in order [U]:
1. **The camera.** The chase camera follows the vehicle's pose; a clip or a wobble that is a few hundredths of a world unit in the simulation is amplified by a camera that tracks the vehicle every frame. Log the camera's position against the vehicle's per frame through the same drive (`drivetest` plus the shot logs).
2. **The pose at the PSP's frame rate.** The simulation runs at 30 ticks a second and the PSP draws below that in heavy stretches: check whether the vehicle's drawn pose is the last tick's rather than interpolated (`interpolation` is among the keep words that are skipped in these runs), and whether a tick or two lands in one frame, which shows as a jump that then eases back, the "clip then bounce" as seen.
3. **Compare the same drive on the PC at the frame rate the PSP reaches** (the PC build run with its frame rate capped) to see whether the look comes from the rate alone.

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

**Evidence [M]** (PSP-1000, the same drive and map; the first window of each kind skipped, it counts from before):

| Map, stretch | Type | us an update | ms a tick (all of that type) |
|---|---|---|---|
| b30, Pelicans in flight | pelican | 4,808-6,331 | 9.5-12.7 (two) |
| b30, Pelicans landed, unloading | pelican | 1,721-1,904 | 3.4-3.8 (two) |
| b30, Warthog at rest | warthog | 13-20 (asleep) | 0.01-0.02 |
| b30, driven: ram and reverse | warthog | 3,857 | 7.78 |
| b30, driven: full-lock circles | warthog | 3,390-3,851 | 6.81-7.67 |

- **The driven Warthog costs 6.8-7.8 ms a tick on the PSP**, with two updates a tick. The type update is 85-87% of the vehicles' total (10.2 of 12.1 ms in the ram window); node matrices 0.36, damage 0.05.
- **A Pelican in flight costs about 2.7 times its landed self** (4.8-6.3 ms an update against 1.7-1.9), and the landed Pelicans stay awake at 3.4-3.8 ms a tick for two.
- The emulator's microseconds are about 3.4 times lower than the PSP's for the parked Pelican (0.5 against 1.7-1.9 ms) and 2.5-2.9 times for the driven Warthog; the ratio differs by type, so the emulator's figures give the shape of the cost, never the PSP's milliseconds.
- This is b139's ebf944f build with debug tools; the drive ran with the profile window's own overhead, so read the figures as the vehicles' part of a tick, not as a release build's frame time.

Not measured yet: a Banshee or a Scorpion in motion (nothing drives or flies them hands-off).

**Cost:** none in a release build: debug tools only.

## Tools and fixtures

- `flipfixture` gains `flipfixtureatN` and `flippressN` (the fixture's time and a held action button), so a batch can play the flip hands-off. Debug tools only.
- `tools/drivecmp.py`: `label=log` arguments; the first is the reference.
