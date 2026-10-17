import json
import pathlib
import subprocess
import tempfile

binary = str(pathlib.Path('build/faultline').resolve())
def run(*args, expected=0):
    result = subprocess.run([binary, *map(str, args)], capture_output=True, text=True)
    assert result.returncode == expected, (args, result.returncode, result.stdout, result.stderr)
    return result

with tempfile.TemporaryDirectory() as directory:
    trace = pathlib.Path(directory) / 'trace.json'
    run('replay', 'scenarios/stale-read.fl', 1, trace, expected=1)
    data = json.loads(trace.read_text())
    assert data['violated'] and any(e['type'] == 'violation' for e in data['trace'])
    run('replay', 'scenarios/stale-read.fl', 2, trace)
    assert not json.loads(trace.read_text())['violated']
    prefix = pathlib.Path(directory) / 'found'
    run('search', 0, 1000, 1, prefix, expected=1)
    run('replay', str(prefix) + '.fl', 1, trace, expected=1)
    assert trace.read_bytes() == prefix.with_suffix('.json').read_bytes()
    run('search', 0, 1000, 2, prefix)
    run('replay', '/nonexistent/scenario.fl', 2, expected=2)
    run('search', 0, 0, 2, prefix, expected=2)
    run('replay', 'scenarios/stale-read.fl', '2garbage', expected=2)
    run('replay', 'scenarios/stale-read.fl', 3, expected=2)
print('CLI: replay, JSON, search, artifact reproducibility, and errors passed')
