"""Regenerate committed browser fixtures from the C++ simulator."""
import json
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
examples = {}
with tempfile.TemporaryDirectory() as directory:
    for mode, quorum in [('unsafe', 1), ('safe', 2)]:
        target = pathlib.Path(directory) / f'{mode}.json'
        result = subprocess.run([str(root / 'build/faultline'), 'replay',
                                 str(root / 'scenarios/stale-read.fl'), str(quorum), str(target)],
                                check=False)
        if result.returncode != (1 if quorum == 1 else 0):
            raise SystemExit('Unexpected replay outcome')
        examples[mode] = json.loads(target.read_text())
(root / 'web/demo.js').write_text('window.FAULTLINE_DEMOS = ' + json.dumps(examples, separators=(',', ':')) + ';\n')
print('Updated web/demo.js from verified replay outputs')
