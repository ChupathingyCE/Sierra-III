#!/usr/bin/env python3
"""passtable.py - where a frame's time goes, pass by pass, over a stretch of
play: the engine walk's pass table (docs/plan_measurement_round.md, M5).

Reads a run's log folder (log.txt and profile.txt, a PSP's or the emulator's)
and sorts the profile's steps into the passes of a frame: the tick, the
visibility, the level, the objects, decals, effects, the first-person view,
the HUD, the frame's start and end, input and sound, and map changes. A step's
time runs until the next step, wherever that is, so a pass holds what its
steps call, the GE layer's CPU included (rasterizer_model_draw is mostly the
GE layer's model path). The budget line's split (ticks, drawing, the GE
layer's CPU, waiting for the GE) is printed beside it, so the walk (drawing
less the GE layer) can be read off.

The profile's windows (every 5 s) carry no game time; each is matched to the
log by its "objects_update by type" line, written with it, and takes the
game time and map of the last heartbeat before that line. --map and
--from/--to (game time in seconds) choose the stretch; windows with no ticks
(the pause menu, a stall) are left out.

Two folders give two columns and their ratio: a PSP's run against the
emulator's cache model (timing on) over the same stretch is the measurement
round's M3, by pass.

usage: passtable.py LOGDIR [LOGDIR2] [--map b30] [--from 90] [--to 450]
                    [--steps 4] [--unclassified]
       passtable.py - --cachemap TO_A/cachemap.txt,TO_B/cachemap.txt --frames N
                    (the cache model's functions by pass, the stretch between
                    two runs of the same play)
"""
import argparse, collections, os, re

NUMBER = r"(-?[0-9]+(?:\.[0-9]+)?)"

# (pass, step-name patterns, matched in order, first wins)
PASSES = [
    ("map and saves", [r"^main_(saving|change|switch|lost|load)", r"^collision_log_end_period", r"^checkpoint"]),
    ("frame cap", [r"^main_rasterizer_throttle"]),
    ("frame end (GE wait, swap)", [r"^psp_frame_account", r"^render_frame_present", r"^profile_(frame|render|tick)_"]),
    ("frame start", [r"^rasterizer_frame_begin", r"^rasterizer_windows?_begin", r"^set_window_camera_values", r"^render_debug",
                     r"^rasterizer_window_end", r"^halo_screen_ui_offset"]),
    ("visibility", [r"^structure_visibility_", r"^BIT_VECTOR_SIZE_IN_BYTES", r"^find_rendered_objects", r"^render_nonplayer_frame",
                    r"^lights_preprocess_scene", r"^render_object_shadows$", r"^render_view$", r"^render_camera"]),
    ("level", [r"^structure_render_", r"^render_sky", r"^lights_render_diffuse", r"^rasterizer_environment_(?!shadow)",
               r"^render_environment", r"^rasterizer_fog", r"^render_water"]),
    ("objects", [r"^render_model", r"^render_object", r"^object_get_", r"^object_definition_get", r"^rasterizer_model_",
                 r"^widgets_render", r"^rasterizer_environment_shadow", r"^object_render_state", r"^hud: unit mappers"]),
    ("decals", [r"^rasterizer_decals", r"^render_decals", r"^decals_"]),
    ("effects", [r"^render_particles", r"^particle_systems_render", r"^render_contrails", r"^weather_particle_systems_render",
                 r"^rasterizer_lens_flares", r"^rasterizer_transparent", r"^rasterizer_screen_flash", r"^player_effect_",
                 r"^render_transparent", r"^lens_flare", r"^render_effects"]),
    ("first person", [r"^first_person_weapon_(render|draw)", r"^first_person_weapon_draw"]),
    ("HUD and menus", [r"^hud: ", r"^hud_render", r"^hud_draw", r"^render_ui_widgets", r"^process_ui_widgets", r"^interface_",
                       r"^progress_bar", r"^motion_sensor_draw", r"^render_hud", r"^hud_show", r"^hud_play"]),
    ("tick", [r"^objects_update", r"^ai_update", r"^hs_update", r"^players_update", r"^game_sound_update", r"^particles_update",
              r"^effects_update", r"^antennas_update", r"^hud_update", r"^hud_messaging_update", r"^contrails_update",
              r"^first_person_weapons_update", r"^particle_systems_update", r"^motion_sensor_tick", r"^recorded_animations_update",
              r"^game_state_check_data_arrays", r"^unlock_global_random_seed", r"^scenario_frame_update", r"^psp_tick_account",
              r"^rumble_update", r"^event_manager_update", r"^weather_particle_systems_update", r"^game_tick", r"^game_engine_update",
              # PSP_TICK_STEPS's breadcrumbs inside the tick
              r"^biped_", r"^unit_", r"^actor", r"^prop_", r"^object_update", r"^ai_", r"^cs_", r"^hs_", r"^vehicle_",
              r"^weapon_", r"^projectile", r"^item_", r"^device", r"^machine", r"^damage_", r"^collision_", r"^physics",
              r"^havok", r"^object_(move|set|attach|detach|connect|reconnect|compute|wake|postprocess|update)",
              r"^node_matrices", r"^animation", r"^model_animation", r"^character_", r"^swarm", r"^encounter", r"^command_"]),
    ("input, camera and sound", [r"^input_", r"^player_control_update", r"^observer_update", r"^director_update", r"^sound_render",
                                 r"^update_client_local_ticks", r"^main_update_time", r"^psp_step$", r"^memset$"]),
]
COMPILED = [(name, [re.compile(p) for p in patterns]) for name, patterns in PASSES]
ORDER = [name for name, _ in PASSES] + ["unclassified"]


def classify(step):
    for name, patterns in COMPILED:
        if any(p.search(step) for p in patterns):
            return name
    return "unclassified"


class Run:
    def __init__(self, folder):
        self.folder = folder
        self.windows = []   # dicts: map, game_time (s), seconds, frames, ticks, steps {name: microseconds}
        self.budget = []    # (map, game time s, clock, ticks, frame, GE layer CPU, GE wait) ms a frame, and fps
        self.read()

    def read(self):
        log = os.path.join(self.folder, "log.txt")
        places = []         # per window, in order: (map, game time s)
        mapname, game_time, last, frames = "?", -1.0, None, 0.0
        for line in open(log, errors="replace"):
            m = re.search(r"map d:\\maps\\(\w+)\.map: opened", line)
            if m:
                mapname, game_time, last = m.group(1), -1.0, None
                continue
            m = re.match(r"heartbeat: game time (-?\d+) \(" + NUMBER + r" ticks/s\).*?" + NUMBER + r" fps", line)
            if m:
                game_time = int(m.group(1)) / 30.0
                # the heartbeat's frames: its fps over the time its ticks took
                tps, fps = float(m.group(2)), float(m.group(3))
                frames = fps * (int(m.group(1)) - last) / tps if last is not None and tps > 0 and int(m.group(1)) > last else 0.0
                last = int(m.group(1))
                continue
            m = re.match(r"budget: " + NUMBER + r" ms a frame by the clock \(" + NUMBER + r" fps.*ticks " + NUMBER +
                         r" ms a frame.*frame " + NUMBER + r" \(the GE renderer's CPU " + NUMBER + r" of it, waiting for the GE " +
                         NUMBER, line)
            if m:
                self.budget.append((mapname, game_time) + tuple(float(v) for v in m.groups()) + (frames,))
                continue
            if "objects_update by type" in line:
                places.append((mapname, game_time))
        k = 0
        window = None
        for line in open(os.path.join(self.folder, "profile.txt"), errors="replace"):
            m = re.match(r"window \d+: " + NUMBER + r" s, (\d+) frames, (\d+) ticks", line)
            if m:
                place = places[k] if k < len(places) else ("?", -1.0)
                k += 1
                window = {"map": place[0], "game_time": place[1], "seconds": float(m.group(1)),
                          "frames": int(m.group(2)), "ticks": int(m.group(3)), "steps": {}}
                self.windows.append(window)
                continue
            if window is None or line.startswith("step ") or ":" in line[:12] and line.startswith(("objcost", "particlecensus")):
                continue
            m = re.match(r"(.+?)\s+" + NUMBER + r"\s+" + NUMBER + r"\s+" + NUMBER + r"\s+" + NUMBER + r"$", line.rstrip())
            if m:
                window["steps"][m.group(1).strip()] = float(m.group(2)) * 1000.0 * window["frames"]

    def select(self, mapname, start, end):
        return [w for w in self.windows if w["ticks"] > 0 and w["frames"] > 0 and
                (mapname is None or w["map"] == mapname) and start <= w["game_time"] <= end]

    def budget_mean(self, mapname, start, end):
        rows = [b for b in self.budget if (mapname is None or b[0] == mapname) and start <= b[1] <= end and b[1] >= 0]
        if not rows:
            return None
        # weighted by each heartbeat's frames
        weights = [b[8] for b in rows]
        total = sum(weights) or 1.0
        return [sum(b[i] * w for b, w in zip(rows, weights)) / total for i in (2, 3, 4, 5, 6, 7)]


def table(run, mapname, start, end):
    windows = run.select(mapname, start, end)
    frames = sum(w["frames"] for w in windows)
    seconds = sum(w["seconds"] for w in windows)
    passes = collections.Counter()
    steps = collections.defaultdict(collections.Counter)
    for w in windows:
        for step, us in w["steps"].items():
            name = classify(step)
            passes[name] += us
            steps[name][step] += us
    return windows, frames, seconds, passes, steps


# the cache model's functions (cachemap.txt) into the same passes, and the
# GE layer and the sound apart: a function there is the work itself, not a
# step with what it calls. First match wins
FUNCTION_PASSES = [
    ("sound (the ME's on the PSP)", [r"^psp_dsound", r"^decode_block", r"^sound_", r"adpcm", r"^psp_meaudio"]),
    ("GE layer", [r"^psp_ge_(?!.*shadow)(?!.*decal)", r"^psp_tex_", r"^psp_geo_", r"^sceGu", r"^sceGum", r"^__wrap_sceGe", r"^list_(take|full)",
                  r"^psp_model_draw", r"^psp_vertex_lock", r"^resident_entry", r"^vm_redirect", r"^constants_store", r"^model_convert",
                  r"^psp_anim", r"^psp_level_"]),
    ("shadows", [r"shadow"]),
    ("decals", [r"decal"]),
    ("effects", [r"particle", r"contrail", r"^build_sprite", r"lens_flare", r"transparent", r"^render_effects", r"weather"]),
    ("visibility", [r"^structure_visibility", r"^portal_", r"^convex_polygon", r"^render_frustum", r"^clip_triangle", r"^cluster_partition",
                    r"^collision_bsp_test_vector", r"^find_rendered_objects", r"^structure_leaf", r"^lights_preprocess"]),
    ("level", [r"^structure_render", r"^rasterizer_environment", r"lightmap", r"^render_sky", r"^structure_"]),
    ("objects", [r"^model_", r"^render_model", r"^render_object", r"^object_", r"^rasterizer_model", r"^_rasterizer_model", r"^find_point_lights",
                 r"^halo_matrix", r"^matrix4x3", r"^rigid_corner", r"^animation", r"^node_", r"^widgets_", r"^process_rendered_objects",
                 r"^rasterizer_set_model"]),
    ("HUD and menus", [r"^hud", r"^interface_", r"widget", r"^draw_string", r"^text_", r"^font", r"^rasterizer_draw", r"^motion_sensor"]),
    ("helpers (libc, maths, tags, data arrays)", [r"^mem(set|cpy|cmp|move)", r"^tag_", r"^datum_", r"^data_", r"^__kernel", r"^__ieee754",
                                                  r"^psp_step", r"^find$", r"^str", r"^sqrt", r"^cosf?$", r"^sinf?$"]),
]
# funcfetches.txt counts every function over the whole run, all phases: the
# tick's work goes into passes of its own (first match wins, after the
# drawing's above)
TICK_FUNCTION_PASSES = [
    ("tick: AI", [r"^actor", r"^ai_", r"^psp_perc", r"^psp_perception", r"^prop_", r"^cs_", r"^encounter", r"^swarm", r"^path", r"^sector", r"^character_", r"^command_",
                  r"^danger_zone", r"^actor_perception", r"^ai_"]),
    ("tick: units and bipeds", [r"^unit_", r"^biped_", r"^aiming_", r"^find_aim_assist", r"^player", r"^weapon_", r"^first_person_weapon", r"^vehicle_", r"^item_",
                                r"^equipment_", r"^projectile", r"^damage_", r"^device", r"^machine"]),
    ("collision and physics", [r"^collision", r"^psp_bsp[23]d", r"^fast_vector", r"^physics", r"^bsp3d", r"^bsp2d", r"^global_physics",
                               r"^havok", r"^mass_point", r"^sphere_", r"^cylinder_", r"^object_.*collision", r"^psp_sphere_",
                               r"^cluster_get_next_collideable", r"^convex_hull", r"^objects_in_(clusters|sphere)", r"^planes_intersect",
                               r"^point_physics"]),
    ("tick: object updates and sleep", [r"^objects_update", r"^psp_object_(update|count)", r"^psp_sleepy", r"^psp_checksum_span",
                                        r"^light_(reconnect|compute|reach)", r"^effect_update", r"^objects_"]),
    ("animation", [r"animation", r"^animate", r"^psp_pose", r"^quaternion", r"^pose_"]),
    ("scripts", [r"^hs_", r"^recorded_animation", r"^scenario_"]),
]
MORE_FUNCTION_PASSES = [
    ("soft double (the FPU is single)", [r"^__(pack|unpack)_d", r"^__(add|sub|mul|div)df3", r"^__truncdfsf2", r"^__extendsfdf2",
                                         r"^__(fix|float)(un)?(si|di)df", r"^__(eq|ne|lt|le|gt|ge|un)df2", r"^__fixdfsi"]),
    ("logging, debug text", [r"printf", r"^pspDebugScreen", r"^platform_log", r"^psp_crumb", r"^log_"]),
    ("saves", [r"^game_state_write", r"^psp_checkpoint", r"^game_state_(save|revert)"]),
    ("caches (lruv)", [r"^lruv_"]),
    ("system", [r"^sce", r"^psp[Ss]dk", r"^XInput", r"^enumerate_memory_units", r"^D3D"]),
]
FUNCTION_PASSES = (FUNCTION_PASSES[:1] + MORE_FUNCTION_PASSES + TICK_FUNCTION_PASSES[2:4] + FUNCTION_PASSES[1:] + TICK_FUNCTION_PASSES[:2] +
                   TICK_FUNCTION_PASSES[4:])
for _name, _patterns in FUNCTION_PASSES:
    if _name == "sound (the ME's on the PSP)":
        _patterns += [r"^audio_thread", r"^dsound_", r"^update_channels", r"^compute_sound_obstruction", r"^game_sound"]
    elif _name == "GE layer":
        _patterns += [r"^ge_kind", r"^get_sprite_clip_rect", r"^multipurpose_pass", r"^gum", r"^skin_plan", r"^real_argb_color_to_pixel"]
    elif _name == "visibility":
        _patterns += [r"^render_camera_build_frustum"]
    elif _name == "objects":
        _patterns += [r"^psp_vfpu_matrix"]
    elif _name == "helpers (libc, maths, tags, data arrays)":
        _patterns += [r"^vfpu_", r"^vectors?3d", r"^reflect_vector", r"^atan", r"^rint", r"^floor", r"^fabs", r"^finite", r"^__udivdi3",
                      r"^__divdi3", r"^cs(memcpy|memmove|memset|strcmp)", r"^memchr", r"^qsort", r"^global_(scenario|structure)",
                      r"^game_time_get", r"^breakable_surface", r"^rotate_vector", r"^euler_angles", r"^component_vectors",
                      r"^valid_real", r"^(transition|periodic)_function", r"^(real_)?seed_random", r"^real_", r"^psp_microseconds",
                      r"^effect_real_random", r"^local_random", r"^game_connection"]
FUNCTION_COMPILED = [(name, [re.compile(p) for p in patterns]) for name, patterns in FUNCTION_PASSES]


def funcfetches_passes(paths, frames, ticks):
    """every function's fetches and I-misses (funcfetches.txt, the cache
    model with fabound), by pass; with two (to A, to B), their difference"""
    tables = []
    for path in paths:
        t = {}
        for line in open(path, errors="replace"):
            f = line.split()
            if len(f) == 5 and f[0].isdigit():
                t[f[4]] = (int(f[0]), int(f[1]))
        tables.append(t)
    passes = collections.defaultdict(lambda: [0.0, 0.0])
    for name in tables[-1]:
        v = [tables[-1][name][k] - (tables[0].get(name, (0, 0))[k] if len(tables) == 2 else 0) for k in range(2)]
        for k in range(2):
            passes[classify_function(name)][k] += v[k]
    total = [sum(p[k] for p in passes.values()) for k in range(2)]
    print("\nthe cache model, every function by pass (%s), all phases: fetches and I-misses a frame (%.0f frames, %.0f ticks)" % (
        " minus ".join(reversed(paths)) if len(paths) == 2 else paths[0], frames, ticks))
    print("  %-44s %12s %7s %10s %7s" % ("pass", "fetches (k)", "%", "I-misses", "%"))
    for name, (f, i) in sorted(passes.items(), key=lambda kv: -kv[1][0]):
        print("  %-44s %12.1f %6.1f%% %10.0f %6.1f%%" % (name, f / frames / 1e3, 100.0 * f / total[0], i / frames, 100.0 * i / total[1]))


def classify_function(name):
    for pass_name, patterns in FUNCTION_COMPILED:
        if any(p.search(name) for p in patterns):
            return pass_name
    return "the rest (functions outside the lists' top)"


def cachemap_phase(path, phase):
    """a phase's (fetches, I-misses, D-misses) by function, from the lists the
    cache map prints (the top functions of each)"""
    section, inside, out = None, False, collections.defaultdict(lambda: [0, 0, 0])
    totals = None
    for line in open(path, errors="replace"):
        if line.startswith("== "):
            inside = line[3:].strip() == phase
            section = None
            continue
        if not inside:
            m = re.match(r"\s*" + re.escape(phase) + r"\s+data:\s+\d+ cached accesses,\s+(\d+) misses.*instructions:\s+(\d+) fetches,\s+(\d+) misses", line)
            if m:
                totals = (int(m.group(2)), int(m.group(3)), int(m.group(1)))
            continue
        if line.startswith("data misses by the function"):
            section = "d"
        elif line.startswith("instruction fetches by function"):
            section = "f"
        elif line.startswith(("instruction misses by function", "data misses by", "instructions:", "hot lines")):
            section = None
        elif section == "d":
            m = re.match(r"\s+(\d+)\s+[\d.]+%\s+(\S+)\s*$", line)
            if m:
                out[m.group(2)][2] += int(m.group(1))
        elif section == "f":
            m = re.match(r"\s+(\d+)\s+[\d.]+%\s+misses\s+(\d+)\s+\d+ B\s+(\S+)", line)
            if m and out[m.group(3)][0] == 0:
                out[m.group(3)][0] = int(m.group(1))
                out[m.group(3)][1] = int(m.group(2))
    return out, totals


def cachemap_passes(paths, phase, frames):
    """the passes' fetches and misses a frame in the model; with two maps
    (to A, to B), their difference"""
    tables = [cachemap_phase(p, phase) for p in paths]
    names = set()
    for t, _ in tables:
        names |= set(t)
    passes = collections.defaultdict(lambda: [0.0, 0.0, 0.0])
    for name in names:
        values = [t[name] if name in t else [0, 0, 0] for t, _ in tables]
        v = [values[-1][k] - (values[0][k] if len(values) == 2 else 0) for k in range(3)]
        for k in range(3):
            passes[classify_function(name)][k] += v[k]
    totals = [tables[-1][1][k] - (tables[0][1][k] if len(tables) == 2 else 0) for k in range(3)] if all(t[1] for t in tables) else None
    if totals:
        listed = [sum(p[k] for p in passes.values()) for k in range(3)]
        passes["not in the lists"] = [totals[k] - listed[k] for k in range(3)]
    print("\nthe cache model's %s phase by pass (%s), a frame over %.0f frames: fetches (thousands), I-misses, D-misses" % (
        phase, " minus ".join(reversed(paths)) if len(paths) == 2 else paths[0], frames))
    order = [n for n, _ in FUNCTION_PASSES] + ["the rest (functions outside the lists' top)", "not in the lists"]
    for name in order:
        if name in passes:
            f, i, d = passes[name]
            print("  %-44s %10.1f %10.0f %10.0f" % (name, f / frames / 1e3, i / frames, d / frames))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("folders", nargs="+")
    ap.add_argument("--map")
    ap.add_argument("--from", dest="start", type=float, default=5.0, help="game time, seconds (default 5: after the load)")
    ap.add_argument("--to", dest="end", type=float, default=1e9)
    ap.add_argument("--steps", type=int, default=3, help="the largest steps shown under each pass")
    ap.add_argument("--unclassified", action="store_true", help="list every step no pass claims")
    ap.add_argument("--cachemap", help="the cache model's cachemap.txt, or two (to A,to B) for a stretch: its functions by pass")
    ap.add_argument("--phase", default="drawing", help="with --cachemap: the phase (other, tick, drawing, frame end)")
    ap.add_argument("--frames", type=float, help="with --cachemap: the frames in it (fixedticks: ticks / 2)")
    ap.add_argument("--funcfetches", help="the cache model's funcfetches.txt, or two (to A,to B): every function by pass")
    a = ap.parse_args()
    if a.folders == ["-"]:
        if a.cachemap:
            cachemap_passes(a.cachemap.split(","), a.phase, a.frames or 1.0)
        if a.funcfetches:
            funcfetches_passes(a.funcfetches.split(","), a.frames or 1.0, 2.0 * (a.frames or 1.0))
        return
    runs = [Run(f) for f in a.folders[:2]]
    results = [table(r, a.map, a.start, a.end) for r in runs]
    print("stretch: map %s, game time %.0f-%s s" % (a.map or "any", a.start, "end" if a.end >= 1e9 else "%.0f" % a.end))
    for r, (windows, frames, seconds, _, _) in zip(runs, results):
        b = r.budget_mean(a.map, a.start, a.end)
        print("%s: %d profile windows, %d frames, %.0f s%s" % (r.folder, len(windows), frames, seconds,
              "; budget (ms a frame): clock %.1f (%.1f fps), ticks %.1f, drawing %.1f (the GE layer's CPU %.1f, waiting for the GE %.1f), "
              "so the engine walk %.1f" % (b[0], b[1], b[2], b[3], b[4], b[5], b[3] - b[4] - b[5]) if b else ""))
    print()
    head = "%-28s" % "pass (ms a frame)"
    for k in range(len(runs)):
        head += " %9s %6s" % ("run %d" % (k + 1), "%")
    if len(runs) == 2:
        head += " %8s" % "2 / 1"
    print(head)
    for name in ORDER:
        values = [(res[3][name] / 1000.0 / res[1] if res[1] else 0.0) for res in results]
        if not any(v >= 0.005 for v in values):
            continue
        row = "%-28s" % name
        for res, v in zip(results, values):
            total = sum(res[3].values()) / 1000.0 / res[1] if res[1] else 0.0
            row += " %9.2f %6.1f" % (v, 100.0 * v / total if total else 0.0)
        if len(runs) == 2:
            row += " %8s" % ("%.2f" % (values[1] / values[0]) if values[0] > 0.005 else "-")
        print(row)
        for step, us in results[0][4][name].most_common(a.steps):
            row = "    %-24.24s" % step
            for res in results:
                v = res[4][name][step] / 1000.0 / res[1] if res[1] else 0.0
                row += " %9.2f %6s" % (v, "")
            print(row)
    totals = [sum(res[3].values()) / 1000.0 / res[1] if res[1] else 0.0 for res in results]
    print("%-28s" % "all steps" + "".join(" %9.2f %6s" % (t, "") for t in totals))
    if a.cachemap:
        cachemap_passes(a.cachemap.split(","), a.phase, a.frames or 1.0)
    if a.unclassified:
        for k, res in enumerate(results):
            print("\nunclassified in run %d:" % (k + 1))
            for step, us in res[4]["unclassified"].most_common():
                print("  %-50s %8.3f ms a frame" % (step, us / 1000.0 / res[1]))


if __name__ == "__main__":
    main()
