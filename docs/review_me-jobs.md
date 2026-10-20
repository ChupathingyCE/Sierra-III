# Review: me-jobs (ME programme, job 1: `mepoints`)

Point physics for the frame's particles and contrails, run on the Media Engine beside the main CPU. Off by default (keep word `mepoints`); nothing changes without it.

Status: the ME's answers differed from the main CPU's in the leaf only; the cause is found and fixed [D]. The exactness gate (emulator) is the next step; no PSP figures yet.

## 1. The job (`mepoints`, `mepointscheck`, `mepointsminN`)

- `particles_update` lists the points whose physics the frame will run (the turn rule's prediction), then the contrails' points, and hands the last 128 to the ME as one ring job (`psp_me.c`, `ME_JOB_POINTS`).
- The ME makes answers from the back of the list. The main CPU runs its loops from the front, takes an answer the ME made from bit-identical inputs, or makes the point itself and moves the line the ME must not cross. Neither CPU waits inside the loops.
- The ME's copy (`psp_point_physics_quiet`, `psp_collision_test_vector_points`, `psp_collision_bsp_test_vector_quiet`) is the engine's code without the usage log, the collision-user stack and the error reports. A point whose ray would report (a map's bad leaf, material, fog chain, BSP node or plane) returns -1 and is left to the main CPU, which reports it as before.
- `point_physics_update` copies a collision's leaf and cluster, not the whole `location`: its pad word is never set, so copying it made the two CPUs' bytes differ for no reason.
- `mepointscheck` makes every point on the main CPU too, compares each ME answer field by field (in hex) and re-runs the ME's copy at the loop from the listed inputs. That re-run says whether the inputs moved on after the hand-over or the two copies of the code differ.

Cost: the job is speed work. The emulator runs the ME job inside the push, so it can say only whether the job is exact and keeps the rules, never how fast it is. Speed comes from the PSP A/B (section 4).

## 2. The emulator's ME stack (diagnostic fidelity)

`mewrites` counted 35 lines written by both CPUs. They were the producer thread's own stack: the emulator runs a job on the pushing thread, so the job's locals written through pointers (a BSP walk's leaf list) landed below its stack pointer, where the producer writes again after the push. On the PSP the ME's stack is in its own eDRAM. Simulated jobs now run on a 48 KB stack of their own (`psp_me_sim_call`), so the trace counts only real sharing. After it: 0 lines by both [E].

## 3. The leaf difference: cause and fix

What the check showed [E]: in 12% of a30's checked answers (b30 0.5%, d20 10%), only `leaf` differed (a few times `cluster` too); position, velocity, result and normal were bit-identical. The ME's copy re-run on the main CPU agreed with the ME every time, so the two copies of the code differed, not their inputs. The interpreter (`-i`) run differed too, so the emulator's JIT is not involved.

The ray-level note (each CPU's rays, and both BSP walks run on each ray) showed the same ray on both CPUs, with the engine's ray ending in the walk's last leaf and the ME's ray ending in no leaf at all (`end -1/-1`).

The cause [D]: `collision_test_vector` has a branch for a ray with nothing to test (no structure, water or object flags, or `debug_collision_skip_vectors`):

```c
	else
	{
		collision->t = 1.0f;
		add_vectors3d((real_vector3d const *)point, vector, (real_vector3d *)&collision->point);
		scenario_location_from_point(&collision->location, &collision->point);
	}
```

Particles whose definition has neither structure nor water collisions take it: they don't collide, but their location still follows them into the next leaf. The ME's copy had no such branch, so its location stayed `NONE` and the point kept its old leaf.

The fix (`FIXES_MEJOBS`, collisions.c `#mepoints`): `psp_collision_test_vector_points` gains the same `else`, with the same `add_vectors3d` and a quiet `scenario_location_from_point`:

```c
static short psp_location_from_point_quiet(struct location *location, real_point3d const *point)
```

It is `bsp3d_test_point`'s walk (same depth guard, same plane test) and `scenario_location_from_point`'s leaf and cluster checks. Where `bsp3d_test_point` would report (a node the map doesn't have, or too deep) or the tag lookup would (a plane index out of range), it returns -1 and the main CPU makes the point.

The ray note (`psp_points_test_vector` around the main loop's `collision_test_vector`, and the rays/walk report) was diagnostic only and is gone, so `point_physics_update` is the engine's again apart from the leaf/cluster copy.

Cost: the main CPU's code is unchanged by the fix; the ME does one more BSP point walk per non-colliding particle, which is what the main CPU did before [D].

## 4. Still to do, in order

1. The exactness gate (emulator): `mepointscheck` 0 differed on a30, b30 and a10; `statecrc` with `mepoints` on and off identical on d20, a50 and c20; a30 in the strict interpreter; `mewrites` 0 lines by both.
2. Full builds (campaign, and `PSP_MULTIPLAYER=1`).
3. The PSP A/B: keepsets `- | mepoints`, repeat 2, a30 and b30 timed routes; compare `particles_update` + `contrails_update` ms per frame and the frame time. Only the PSP says whether it pays.

## Keep words

| Word | Default | What it does |
|---|---|---|
| `mepoints` | off | Hands the frame's particle and contrail point physics to the ME |
| `mepointscheck` | off | Makes every point on the main CPU too and reports each differing answer |
| `mepointsminN` | 8 | The fewest points worth a job |
