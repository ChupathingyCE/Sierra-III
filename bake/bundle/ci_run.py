#!/usr/bin/env python3
"""ci_run.py - runs a workflow step's command and, when it fails, puts the
end of its output in a GitHub annotation, which the run's page (and the API)
shows without opening the job's log.

    python ci_run.py "<what it is>" <command> [arguments ...]

The output still streams to the log as it comes; the exit code passes
through."""
import collections
import subprocess
import sys

title, command = sys.argv[1], sys.argv[2:]
tail = collections.deque(maxlen=60)
p = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
for raw in p.stdout:
    line = raw.decode('utf-8', 'replace')
    sys.stdout.write(line)
    sys.stdout.flush()
    tail.append(line.rstrip('\r\n'))
code = p.wait()
if code:
    # an annotation is one line: GitHub's escapes keep its line breaks
    text = '\n'.join(tail).replace('%', '%25').replace('\r', '%0D').replace('\n', '%0A')
    print('::error title=%s failed (%d)::%s' % (title.replace(',', ' ').replace(':', ' '), code, text), flush=True)
sys.exit(code)
