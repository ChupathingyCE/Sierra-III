# Runs one of vme2-next-level's entry scripts (argv[1]: its path) under python3 -I
# with only the toolkit's own folder on sys.path, never the current one.
import os, runpy, sys
script = os.path.abspath(sys.argv[1])
sys.path.insert(0, os.path.dirname(script))
sys.argv = [script] + sys.argv[2:]
runpy.run_path(script, run_name="__main__")
