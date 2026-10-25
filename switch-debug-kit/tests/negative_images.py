from pathlib import Path
import importlib.util
HERE=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('lab_build',HERE/'build.py');build=importlib.util.module_from_spec(spec);spec.loader.exec_module(build)
missed=[]
for name in ['bad_write','bad_branch']:
    for suffix in ['.lab','.json']:(HERE/'build'/(name+suffix)).unlink(missing_ok=True)
    try:build.module(name,HERE/'lab/tests/fixtures'/f'{name}.c')
    except AssertionError as error:print('Rejected negative module:',name,str(error))
    else:missed.append(name)
    assert not (HERE/'build'/(name+'.lab')).exists(),'Rejected modules must not be packaged'
assert not missed,'Unsafe image accepted: '+','.join(missed)
