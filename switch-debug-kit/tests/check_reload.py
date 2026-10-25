"""Exercise actual portable updater and compile the libnx adapter."""
from pathlib import Path
import subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
import sys
sys.path.insert(0,str(ROOT/'host'))
from sdk import SDK,tool,HOST_CC
from project import load,header
cfg=load();cfg['title_id']='0500000000001234'
test_config=ROOT/'build/test-config';test_config.mkdir(parents=True,exist_ok=True)
(test_config/'reload_project.h').write_text(header(cfg),encoding='utf-8')
OUT=ROOT/'build/tests';OUT.mkdir(parents=True,exist_ok=True)
exe=OUT/'reload-core.exe'
subprocess.run([HOST_CC,'-std=gnu11','-O2','-Wall','-Wextra','-Werror',
    '-Wno-misleading-indentation','-Ireceiver','-I'+str(test_config),'tests/reload_core.c',
    'receiver/reload_core.c','-lbcrypt','-o',str(exe)],cwd=ROOT,check=True)
with tempfile.TemporaryDirectory(prefix='reload-',dir=OUT) as td:
    subprocess.run([str(exe),td],check=True)

for unit in ('reload_core','reload_bridge','remote_reload','reload_launch','reload_reports','reload_main'):
    subprocess.run([tool('gcc'),'-std=gnu11','-O2',
        '-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-D__SWITCH__',
        '-march=armv8-a+crypto','-mtune=cortex-a57','-mtp=soft','-fPIE',
        '-I'+str(SDK/'libnx/include'),'-I'+str(ROOT/'build/config'),'-Ireceiver','-c','receiver/'+unit+'.c',
        '-o',str(OUT/(unit+'-switch.o'))],cwd=ROOT,check=True)
print('reload adapter: strict installed libnx compilation PASS')
exe=OUT/'reload-launch.exe'
subprocess.run([HOST_CC,'-std=gnu11','-O2','-Wall','-Wextra','-Werror',
    '-Itests/reload_launch_stubs','-Ireceiver','-I'+str(test_config),
    'tests/reload_launch.c','receiver/reload_launch.c','-o',str(exe)],cwd=ROOT,check=True)
subprocess.run([str(exe)],check=True)
exe=OUT/'reload-reports.exe'
subprocess.run([HOST_CC,'-std=gnu11','-O2','-Wall','-Wextra','-Werror',
    '-Wno-misleading-indentation','-Ireceiver','-I'+str(test_config),'tests/reload_reports.c',
    'receiver/reload_reports.c','-o',str(exe)],cwd=ROOT,check=True)
with tempfile.TemporaryDirectory(prefix='reports-',dir=OUT) as td:
    subprocess.run([str(exe),td],check=True)
exe=OUT/'reload-main.exe'
subprocess.run([HOST_CC,'-std=gnu11','-O2','-Wall','-Wextra','-Werror',
    '-Itests/reload_main_stubs','-Ireceiver','-I'+str(test_config),'tests/reload_main.c',
    'receiver/reload_main.c','-pthread','-o',str(exe)],cwd=ROOT,check=True)
for case in ('handoff','idle','accepted','finish'):subprocess.run([str(exe),case],timeout=10,check=True)
# Reproduce the former architecture: network worker executes the system call.
source=(ROOT/'receiver/reload_main.c').read_text()
start=source.index('uint32_t game_reload_submit(')
end=source.index('\nvoid game_reload_main_poll(',start)
mutant=OUT/'reload-main-worker-mutant.c'
mutant.write_text(source[:start]+'''uint32_t game_reload_submit(const ReloadLaunch *record)
{ int accepted=0; return game_reload_request_launch(record,&accepted); }
'''+source[end:])
bad=OUT/'reload-main-worker-mutant.exe'
subprocess.run([HOST_CC,'-std=gnu11','-O2','-Wall','-Wextra','-Werror',
    '-Itests/reload_main_stubs','-Ireceiver','-I'+str(test_config),'tests/reload_main.c',
    str(mutant),'-pthread','-o',str(bad)],cwd=ROOT,check=True)
r=subprocess.run([str(bad),'handoff'],capture_output=True,text=True,timeout=5)
assert r.returncode and 'pthread_equal' in r.stderr,'Regression must detect the old worker-owned launch'
print('reload main: former worker-owned launch is rejected by regression PASS')

zero_config=ROOT/'build/zero-config';zero_config.mkdir(exist_ok=True)
zero_cfg=load();zero_cfg['title_id']='0000000000000000'
(zero_config/'reload_project.h').write_text(header(zero_cfg),encoding='utf-8')
zero=OUT/'reload-disabled.exe'
subprocess.run([HOST_CC,'-std=gnu11','-O2','-Wall','-Wextra','-Werror',
    '-Itests/reload_launch_stubs','-Ireceiver','-I'+str(zero_config),
    'tests/reload_launch.c','receiver/reload_launch.c','-o',str(zero)],cwd=ROOT,check=True)
subprocess.run([str(zero)],check=True)

exe=OUT/'reload-bridge.exe'
subprocess.run([HOST_CC,'-std=gnu11','-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
    '-Itests/reload_bridge_stubs','-Ireceiver','-I'+str(test_config),
    'tests/reload_bridge.c','receiver/reload_bridge.c','receiver/reload_core.c','-o',str(exe)],cwd=ROOT,check=True)
subprocess.run([str(exe)],check=True)

cpp=OUT/'cpp-consumer.o'
subprocess.run([tool('g++'),'-std=gnu++17','-D__SWITCH__','-mtp=soft','-fPIE','-Ireceiver','-I'+str(SDK/'libnx/include'),
    '-c','tests/cpp_consumer.cpp','-o',str(cpp)],cwd=ROOT,check=True)
undefined=subprocess.check_output([tool('nm'),'-u',str(cpp)],text=True)
assert ' U game_reload_main_poll' in undefined and ' U game_reload_main_finish' in undefined
print('C++ consumer links the C API names without mangling PASS')
