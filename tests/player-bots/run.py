#!/usr/bin/env python3
"""Run standalone bot regressions without a database or a server process."""
from pathlib import Path
import os, shlex, subprocess, tempfile
from retreat_fixture import generate
root = Path(__file__).resolve().parents[2]
compiler = shlex.split(os.environ.get('CXX', 'c++'))
with tempfile.TemporaryDirectory(prefix='takp-bot-tests-') as tmp:
    generate(root, Path(tmp))
    for source in sorted(Path(__file__).parent.glob('*.cpp')):
        binary = Path(tmp) / (source.stem + ('.exe' if os.name == 'nt' else ''))
        print(source.name, flush=True)
        subprocess.run(compiler + ['-std=c++20', '-I', str(root/'zone'), '-I', tmp, str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
print('All standalone bot test programs passed.')
