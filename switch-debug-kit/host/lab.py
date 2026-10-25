"""Authenticated resident ARM lab client; token values are never logged."""
from pathlib import Path
import argparse,hashlib,json,socket,struct,time
HERE=Path(__file__).resolve().parent
ROOT=HERE.parent
from project import load
class Client:
    def __init__(self,host,port=28284,token_file=None):
        self.address=(host,port);self.token=(token_file or ROOT/'data/token.txt').read_text().strip()
        if not 16<=len(self.token)<=128 or any(ord(c)<33 or ord(c)>126 for c in self.token):raise ValueError('Invalid token file')
    @staticmethod
    def line(sock):
        data=bytearray()
        while len(data)<256:
            chunk=sock.recv(1)
            if not chunk:raise ConnectionError('Host closed connection')
            if chunk==b'\n':return data.decode('ascii')
            data+=chunk
        raise ValueError('Oversized response')
    def command(self,text,payload=b''):
        with socket.create_connection(self.address,timeout=10) as sock:
            sock.settimeout(10);sock.sendall(('AUTH '+self.token+'\n').encode())
            if self.line(sock)!='OK AUTH':raise PermissionError('Host authentication rejected')
            sock.sendall((text+'\n').encode()+payload);header=self.line(sock)
            if not header.startswith('OK '):raise RuntimeError('ARM lab '+header)
            size=int(header[3:]);assert 0<=size<=32768
            data=bytearray()
            while len(data)<size:
                chunk=sock.recv(size-len(data))
                if not chunk:raise ConnectionError('Truncated result')
                data+=chunk
            return json.loads(data)
    def upload(self,kind,blob):return self.command(f'{kind} {len(blob)} {hashlib.sha256(blob).hexdigest()}',blob)
    def run(self,iterations=1000,warmups=2,trials=7,budget_ms=500):
        self.command(f'RUN {iterations} {warmups} {trials} {budget_ms}')
        deadline=time.monotonic()+budget_ms/1000+10
        while time.monotonic()<deadline:
            result=self.command('STATUS')
            if result['state']!=1:return self.command('RESULTS')
            time.sleep(.1)
        raise TimeoutError('Worker has not cooperatively returned; CANCEL or close host manually')
def main():
    p=argparse.ArgumentParser(__doc__);p.add_argument('--host',default=load()['host']);p.add_argument('--port',type=int,default=28284);p.add_argument('--token-file',type=Path);p.add_argument('--out',type=Path)
    sub=p.add_subparsers(dest='command',required=True)
    for command in ['status','results','cancel','smoke']:sub.add_parser(command)
    for command in ['module','input']:sub.add_parser(command).add_argument('file',type=Path)
    run=sub.add_parser('run');run.add_argument('--iterations',type=int,default=1000);run.add_argument('--warmups',type=int,default=2);run.add_argument('--trials',type=int,default=7);run.add_argument('--budget-ms',type=int,default=500)
    compare=sub.add_parser('compare');compare.add_argument('--baseline',type=Path,required=True);compare.add_argument('--candidate',type=Path,required=True);compare.add_argument('--input',type=Path,required=True);compare.add_argument('--iterations',type=int,default=1000);compare.add_argument('--trials',type=int,default=7);compare.add_argument('--pairs',type=int,default=2)
    a=p.parse_args();client=Client(a.host,a.port,a.token_file)
    if a.command in ['status','results','cancel']:result=client.command(a.command.upper())
    elif a.command in ['module','input']:result=client.upload(a.command.upper(),a.file.read_bytes())
    elif a.command=='run':result=client.run(a.iterations,a.warmups,a.trials,a.budget_ms)
    elif a.command=='compare':
        if not 1<=a.pairs<=16:raise ValueError('pairs must be1..16')
        client.upload('INPUT',a.input.read_bytes());runs=[];oracle=None
        for pair in range(a.pairs):
            for side in ([0,1] if not pair&1 else [1,0]):
                image=(a.baseline if side==0 else a.candidate).read_bytes();client.upload('MODULE',image);run=client.run(a.iterations,2,a.trials)
                assert run['state']==2 and not run['failed'] and run['trials'],'Incomplete comparison'
                for trial in run['trials']:
                    assert trial['returned']==0 and trial['status']==0 and trial['valid_reply'] and trial['completed']==a.iterations and trial['fp_match'],'Invalid comparison trial'
                    value=tuple(trial[k] for k in ['completed','checksum','output_bytes','output_sha256'])
                    if oracle is None:oracle=value
                    assert oracle==value,'Baseline/candidate output mismatch'
                runs.append({'pair':pair,'side':'baseline' if side==0 else 'candidate','result':run})
        result={'parity':True,'raw_runs':runs,'note':'Raw timer/threadCPU observations; no gameplay FPS inference'}
    else:
        client.upload('MODULE',(ROOT/'build/integer.lab').read_bytes());client.upload('INPUT',struct.pack('<QQ',7,3));result=client.run()
        assert result['state']==2 and not result['failed'] and result['trials'],'Smoke did not finish'
        for trial in result['trials']:
            expected=(7+3*trial['completed'])&((1<<64)-1)
            assert trial['returned']==0 and trial['status']==0 and trial['valid_reply'] and trial['completed']==1000 and trial['fp_match'] and int.from_bytes(bytes.fromhex(trial['output_prefix'])[:8],'little')==expected and trial['checksum']==expected^1000,'Module smoke mismatch'
        result['integer_smoke_parity']=True
    text=json.dumps(result,indent=2);print(text)
    if a.out:a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(text+'\n')
if __name__=='__main__':main()
