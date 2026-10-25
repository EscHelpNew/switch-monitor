"""Build only the standalone ARM lab and import-free modules; never deploy."""
from pathlib import Path
import argparse,hashlib,json,os,re,struct,subprocess
HERE=Path(__file__).resolve().parent
import sys,shutil
sys.path.insert(0,str(HERE/'host'))
from sdk import SDK,BIN,tool,sdk_tool
from project import load,header
OUT=HERE/'build';OUT.mkdir(exist_ok=True)
FLAGS=['-std=gnu11','-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-march=armv8-a+crc+crypto','-mtune=cortex-a57','-ffixed-x18','-fno-strict-aliasing','-ffp-contract=off','-frounding-math']
def elf_sections(data):
    assert data[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',data,18)[0]==183,'Require little-endian ARM64 ELF'
    offset=struct.unpack_from('<Q',data,40)[0];entry_bytes,count,names_index=struct.unpack_from('<HHH',data,58)
    assert entry_bytes==64 and count and names_index<count and offset+count*64<=len(data),'Invalid ELF section table'
    sections=[struct.unpack_from('<IIQQQQIIQQ',data,offset+i*64) for i in range(count)]
    names_section=sections[names_index];names=data[names_section[4]:names_section[4]+names_section[5]]
    for section in sections:
        name_start=section[0];name_end=names.find(b'\0',name_start);assert 0<=name_start<len(names) and name_end>=name_start,'Invalid ELF section name'
        yield names[name_start:name_end].decode('ascii'),section
def module(name='integer',source=None):
    obj=OUT/(name+'.o');elf=OUT/(name+'.elf');raw=OUT/(name+'.bin')
    source=source or HERE/'lab/modules'/f'{name}.c'
    subprocess.run([tool('gcc'),*FLAGS,'-fPIC','-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-I'+str(HERE/'lab/include'),'-c',str(source),'-o',str(obj)],check=True)
    object_relocs=subprocess.check_output([tool('readelf'),'-Wr',str(obj)],text=True)
    allowed={'R_AARCH64_ADR_PREL_PG_HI21','R_AARCH64_ADR_PREL_LO21','R_AARCH64_ADD_ABS_LO12_NC','R_AARCH64_LDST8_ABS_LO12_NC','R_AARCH64_LDST16_ABS_LO12_NC','R_AARCH64_LDST32_ABS_LO12_NC','R_AARCH64_LDST64_ABS_LO12_NC','R_AARCH64_LDST128_ABS_LO12_NC','R_AARCH64_LD_PREL_LO19','R_AARCH64_CALL26','R_AARCH64_JUMP26','R_AARCH64_CONDBR19','R_AARCH64_TSTBR14'}
    assert set(re.findall(r'R_AARCH64_\w+',object_relocs))<=allowed,'Absolute/TLS/GOT module relocations forbidden'
    subprocess.run([tool('gcc'),'-nostdlib','-static','-Wl,--no-undefined','-Wl,-T,'+str(HERE/'lab/modules/module.ld'),str(obj),'-o',str(elf)],check=True)
    symbols=subprocess.check_output([tool('nm'),'-u',str(elf)],text=True);assert not symbols.strip(),'Module imports forbidden'
    relocs=subprocess.check_output([tool('readelf'),'-r',str(elf)],text=True);assert 'There are no relocations' in relocs,'Runtime relocations forbidden'
    for section_name,section in elf_sections(elf.read_bytes()):
        assert not (section[2]&3==3 and section[5]),'Allocated writable section forbidden: '+section_name
    dump=subprocess.check_output([tool('objdump'),'-d',str(elf)],text=True);(OUT/(name+'-aarch64.txt')).write_text(dump)
    assert not re.search(r'\b(?:cntvct_el0|cntpct_el0|mrs|msr)\b',dump),'Smoke module owns no host clocks/FP registers'
    entry=int(re.search(r'^([0-9a-f]+) T lab_module_entry$',subprocess.check_output([tool('nm'),str(elf)],text=True),re.M)[1],16)
    subprocess.run([tool('objcopy'),'-O','binary',str(elf),str(raw)],check=True)
    image=raw.read_bytes();assert len(image)>=4 and len(image)<=1024*1024 and entry%4==0 and entry<=len(image)-4
    for instruction in re.finditer(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(\S+)([^\n]*)',dump,re.M):
        op,operands=instruction.groups()
        if op in ['b','bl','cbz','cbnz','tbz','tbnz'] or op.startswith('b.'):
            target=re.search(r'(?:^|,\s*)([0-9a-f]+)\s*(?:<|$)',operands.strip());assert target,'Unparsed direct branch'
            address=int(target[1],16);assert address%4==0 and address<=len(image)-4,'Direct branch target outside image'
    header=struct.pack('<8I32s',0x3142414c,1,1,len(image),entry,1,0,0,hashlib.sha256(image).digest())
    dest=OUT/(name+'.lab');dest.write_bytes(header+image)
    (OUT/(name+'.json')).write_text(json.dumps({'module':name,'bytes':len(image),'entry':entry,'abi':1,'image_sha256':hashlib.sha256(image).hexdigest(),'file_sha256':hashlib.sha256(header+image).hexdigest()},indent=2))
    print('Built import-free module:',dest,'image bytes',len(image));return dest
def configure():
    cfg=load();dest=OUT/'config';dest.mkdir(exist_ok=True)
    (dest/'reload_project.h').write_text(header(cfg),encoding='utf-8')
    return cfg

def host():
    cfg=configure();shared=HERE/'receiver'
    common=[str(shared/n) for n in ('reload_core.c','reload_bridge.c','remote_reload.c','reload_launch.c','reload_reports.c','reload_main.c')]
    env=os.environ.copy();env['DEVKITPRO']=SDK.as_posix()
    for name,sources,title in [
        ('receiver-demo',[str(HERE/'examples/receiver_main.c')],cfg['name']),
        ('arm-lab',[str(p) for p in sorted((HERE/'lab/source').glob('*.c'))],'Switch ARM lab')]:
        elf=OUT/(name+'.elf');nro=OUT/(name+'.nro');nacp=OUT/(name+'.nacp')
        subprocess.run([tool('gcc'),*FLAGS,'-mtp=soft','-fPIE','-D__SWITCH__',
            '-I'+str(OUT/'config'),'-I'+str(HERE/'lab/include'),'-I'+str(shared),'-I'+str(SDK/'libnx/include'),
            *sources,*common,'-specs='+str(SDK/'libnx/switch.specs'),'-L'+str(SDK/'libnx/lib'),'-lnx','-o',str(elf)],check=True,env=env)
        subprocess.run([sdk_tool('nacptool'),'--create',title,cfg['author'],'1.0.0',str(nacp)],check=True)
        subprocess.run([sdk_tool('elf2nro'),str(elf),str(nro),'--nacp='+str(nacp)],check=True)
        dump=subprocess.check_output([tool('objdump'),'-d',str(elf)],text=True)
        assert 'cntvct_el0' not in dump,'Never use inaccessible virtual timer'
        print('Built:',nro)
    sd=OUT/'sd/switch'/cfg['sd_directory'];sd.mkdir(parents=True,exist_ok=True)
    for name,source in [('bridge.nro','receiver-demo.nro'),('game.nro','receiver-demo.nro'),('arm-lab.nro','arm-lab.nro')]:
        shutil.copy2(OUT/source,sd/name)
    pins={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(OUT.glob('*.nro'))}
    pins['integer.lab']=hashlib.sha256((OUT/'integer.lab').read_bytes()).hexdigest()
    (OUT/'sha256.json').write_text(json.dumps(pins,indent=2)+'\n')
    print('SD demo tree:',sd)
    if cfg['title_id']=='0000000000000000':print('Title ID zero: demo/module lab works; remote RUN deliberately disabled.')

if __name__=='__main__':
    parser=argparse.ArgumentParser(__doc__)
    parser.add_argument('--config-only',action='store_true')
    parser.add_argument('--module',type=Path,help='build one trusted PIC C module instead of the NROs')
    parser.add_argument('--name',default='candidate',help='output basename for --module')
    args=parser.parse_args()
    if args.config_only:configure()
    elif args.module:
        if not re.fullmatch('[a-zA-Z0-9_-]{1,64}',args.name):raise ValueError('Invalid module basename')
        module(args.name,args.module.resolve())
    else:module();host()
