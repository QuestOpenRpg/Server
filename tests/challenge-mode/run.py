"""Generate fixtures from current source, compile, and run using a C++20 compiler."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
p=Path(__file__).resolve().parent
repo=p.parents[1]
with tempfile.TemporaryDirectory(prefix='challenge-tests-') as temp:
    for generator,filename in [('tests.py','test.cpp'),('raid_tests.py','raid-test.cpp'),('enforce_tests.py','enforce-test.cpp')]:
        # Generators write only their output next to themselves; execute copies
        # with their source root explicitly redirected to this repository.
        source=(p/generator).read_text().replace('zone=p.resolve().parents[1]/"zone"',f'zone=Path({str(repo / "zone")!r})')
        g=Path(temp)/generator
        g.write_text(source)
        subprocess.run([sys.executable,str(g)],check=True)
        exe=Path(temp)/(filename+'.exe')
        subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-I'+str(repo/'zone'),'-I'+str(repo/'submodules/fmt/include'),str(Path(temp)/filename),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
