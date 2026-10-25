"""All checks are local; this script never connects to a console."""
from pathlib import Path
import subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'host'))
from sdk import tool,HOST_CC
for file in ['tests/test_project.py','tests/test_reload_client.py']:
 subprocess.run([sys.executable,str(ROOT/file)],check=True)
subprocess.run([sys.executable,str(ROOT/'build.py')],check=True)
subprocess.run([sys.executable,str(ROOT/'tests/check_reload.py')],check=True)
OUT=ROOT/'build/tests';OUT.mkdir(exist_ok=True)
for name,sources in [('lab-core',['lab/tests/core.c','lab/source/core.c']),('integer',['lab/tests/integer.c','lab/modules/integer.c']),('sleep',['lab/tests/sleep.c'])]:
 exe=OUT/(name+'.exe');subprocess.run([HOST_CC,'-std=gnu11','-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Ilab/include',*sources,'-o',str(exe)],cwd=ROOT,check=True);subprocess.run([str(exe)],check=True)
subprocess.run([sys.executable,str(ROOT/'tests/negative_images.py')],check=True)
print('Standalone kit: local config, sockets, storage, launch ownership, module parity/rejection and SDK builds PASS')

if (ROOT.parent/'package.ps1').is_file():subprocess.run([sys.executable,str(ROOT/'tests/test_package.py')],check=True)
