import sys, re, glob
for path in sys.argv[1:]:
    t = open(path, errors='replace').read()
    def last(p):
        m = re.findall(p, t)
        return m[-1] if m else None
    # (b116 added ", N draws left out" inside the brackets)
    geo = last(r'geometry: (\d+) hits, (\d+) misses \((\d+) frames missed something(?:, \d+ draws left out)?\); (\d+) loaded \((\d+) KB\)')
    tex = last(r'textures: (\d+) loaded \((\d+) KB\), (\d+) requests waited for the next frame, (\d+) for room; (\d+) evicted; largest frame (\d+) textures, (\d+) KB; (\d+) frames drew something untextured')
    snd = last(r'sound cache: .*?(\d+) loads \((\d+) KB, largest \d+ KB\), (\d+) failed')
    fps = re.findall(r'budget: ([\d.]+) ms a frame by the clock', t)
    route = last(r'route.txt: done, (\d+) spots')
    print(path.split('/')[-2], '| geo hits/miss/frames-missing/loaded KB:', geo and (geo[0], geo[1], geo[2], geo[4]),
          '| tex loaded KB/untextured frames/evicted:', tex and (tex[1], tex[7], tex[4]), '| sound loads/KB/failed:', snd,
          '| route done:', route)
