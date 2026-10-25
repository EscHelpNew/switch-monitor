#!/usr/bin/env python3
"""Trusted-LAN GAME/ARM lab deployment (28285) and reconnecting nxlink logs.

Never accepts a token on the command line or includes one in error messages.
"""
from pathlib import Path
import argparse,hashlib,json,re,socket,subprocess,sys,time
from project import load
DEFAULT_HOST=load()['host']
PORT=28285
MAX_BYTES=64*1024*1024
class ReloadError(Exception):pass

def _line(stream):
    data=stream.readline(1025)
    if not data.endswith(b'\n') or len(data)>1024:raise ReloadError('Invalid reply')
    return data.rstrip(b'\n')

def _connect(host,token,port,timeout=8):
    if re.fullmatch(r'[0-9a-fA-F]{64}',token) is None:raise ValueError('Invalid token file')
    sock=socket.create_connection((host,port),timeout=timeout);sock.settimeout(timeout)
    stream=sock.makefile('rb')
    try:
        sock.sendall(b'AUTH '+token.encode('ascii')+b'\n')
        if _line(stream)!=b'OK AUTH':raise ReloadError('Authentication rejected')
    except BaseException:
        stream.close();sock.close();raise
    return sock,stream

def status(host,token,port=PORT,timeout=8):
    sock,stream=_connect(host,token,port,timeout)
    with sock,stream:
        sock.sendall(b'STATUS\n')
        try:result=json.loads(_line(stream))
        except (ValueError,UnicodeError):raise ReloadError('Invalid status') from None
        if not isinstance(result,dict) or result.get('protocol')!=2:raise ReloadError('Unsupported peer')
        return result

def crash(host,token,port=PORT):
    sock,stream=_connect(host,token,port)
    with sock,stream:
        sock.sendall(b'CRASH\n');header=_line(stream)
        match=re.fullmatch(rb'OK REPORT ([0-9]{1,6})',header)
        if not match or not 0<int(match[1])<=65536+1024:raise ReloadError('Invalid crash report length')
        size=int(match[1]);data=stream.read(size)
        if len(data)!=size:raise ReloadError('Incomplete crash report')
        return data.replace(token.encode('ascii'),b'[REDACTED]')

def put(host,token,target,path,port=PORT):
    if target not in ('BRIDGE','GAME','LAB'):raise ValueError('Invalid target')
    path=Path(path);size=path.stat().st_size
    if not 128<=size<=MAX_BYTES:raise ValueError('NRO size outside 128..64MiB')
    with path.open('rb') as initial:
        digest=hashlib.file_digest(initial,'sha256').hexdigest()
    sock,stream=_connect(host,token,port)
    sock.settimeout(130)
    with sock,stream,path.open('rb') as image:
        sock.sendall(f'PUT {target} {size} {digest}\n'.encode('ascii'))
        if _line(stream)!=b'OK SEND':raise ReloadError('Upload was rejected')
        while data:=image.read(32768):sock.sendall(data)
        if _line(stream)!=b'OK STORED':raise ReloadError('NRO verification/storage failed')
    return digest

def run(host,token,target,expected=None,port=PORT,timeout=70):
    if target not in ('GAME','LAB'):raise ValueError('Invalid run target')
    sock,stream=_connect(host,token,port)
    with sock,stream:
        sock.sendall(f'RUN {target}\n'.encode('ascii'))
        if _line(stream)!=b'OK RESTART_REQUESTED':raise ReloadError('Restart rejected')
        # A subsequent Result error means IPC failed; EOF means termination.
        sock.settimeout(4)
        try:
            result=stream.readline(1025)
            if result.startswith(b'ERR '):
                code=re.fullmatch(rb'ERR restart_([0-9A-F]{8})\n',result)
                raise ReloadError('System restart request failed'+(' (0x'+code[1].decode()+')' if code else ''))
        except (TimeoutError,OSError):pass
    end=time.monotonic()+timeout;want=load()['name'] if target=='GAME' else 'ARM lab'
    while time.monotonic()<end:
        try:
            state=status(host,token,port,timeout=min(4,max(0.1,end-time.monotonic())))
            if state.get('app')==want and (expected is None or state.get('image_sha256')==expected):return state
        except (OSError,ReloadError):pass
        time.sleep(1)
    raise ReloadError('Restart was requested, but target reconnection was not verified')

def logs(token,path,bind='0.0.0.0',port=28771):
    """One listener survives app exits. Do not run nxlink -s simultaneously."""
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
    secret=token.encode('ascii')
    with socket.socket() as server,path.open('ab',buffering=0) as output:
        server.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1);server.bind((bind,port));server.listen(4)
        print(f'Reconnecting log receiver on {port}: {path}',flush=True)
        while True:
            conn,address=server.accept()
            # Buffer by line so tokens crossing recv boundaries are redacted.
            pending=b'';dropping=False
            with conn:
                output.write(f'\n[HOST] connected {address[0]}\n'.encode())
                while data:=conn.recv(16384):
                    pending+=data
                    while b'\n' in pending:
                        line,pending=pending.split(b'\n',1)
                        if not dropping:output.write((line+b'\n').replace(secret,b'[REDACTED]'))
                        dropping=False
                    if len(pending)>65536:
                        output.write(b'[HOST] overlong log line discarded\n');pending=b'';dropping=True
                if not dropping:output.write(pending.replace(secret,b'[REDACTED]'))
                output.write(b'\n[HOST] disconnected\n')

def bootstrap(host,token,image,application='GAME'):
    from sdk import SDK
    sdk=SDK/'tools/bin/nxlink.exe'
    try:
        result=subprocess.run([str(sdk),'-a',host,'-r','3',str(Path(image).resolve()),
            '--args','--token='+token],capture_output=True,timeout=100)
    except (OSError,subprocess.TimeoutExpired):raise ReloadError('Netloader transfer failed/timed out') from None
    print((result.stdout+result.stderr).replace(token.encode(),b'[REDACTED]').decode(errors='replace'))
    if result.returncode:raise ReloadError('Netloader transfer returned an error')

def main():
    p=argparse.ArgumentParser(__doc__);p.add_argument('--host',default=DEFAULT_HOST)
    p.add_argument('--token-file',type=Path,default=Path(__file__).resolve().parents[1]/'data/token.txt')
    sub=p.add_subparsers(dest='command',required=True)
    sub.add_parser('status')
    q=sub.add_parser('crash');q.add_argument('output',type=Path)
    q=sub.add_parser('put');q.add_argument('target',choices=['BRIDGE','GAME','LAB']);q.add_argument('image',type=Path)
    q=sub.add_parser('run');q.add_argument('target',choices=['GAME','LAB']);q.add_argument('--sha256')
    q=sub.add_parser('deploy');q.add_argument('target',choices=['GAME','LAB']);q.add_argument('image',type=Path)
    q=sub.add_parser('logs');q.add_argument('output',type=Path)
    q=sub.add_parser('bootstrap');q.add_argument('image',type=Path);q.add_argument('--application',choices=['GAME','LAB'],default='GAME')
    a=p.parse_args();token=a.token_file.read_text().strip()
    if re.fullmatch('[0-9a-fA-F]{64}',token) is None:raise ReloadError('Invalid token file')
    if a.command=='status':print(json.dumps(status(a.host,token),ensure_ascii=False))
    elif a.command=='crash':
        data=crash(a.host,token);a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_bytes(data)
        print('Saved crash report: '+str(a.output))
    elif a.command=='put':print('Stored '+a.target+' SHA256 '+put(a.host,token,a.target,a.image))
    elif a.command=='run':print(json.dumps(run(a.host,token,a.target,a.sha256)))
    elif a.command=='deploy':
        sha=put(a.host,token,a.target,a.image)
        print('Verified upload; requesting automatic restart',flush=True)
        print(json.dumps(run(a.host,token,a.target,sha)))
    elif a.command=='logs':logs(token,a.output)
    elif a.command=='bootstrap':bootstrap(a.host,token,a.image,a.application)
if __name__=='__main__':
    try:main()
    except (ReloadError,OSError,ValueError) as e:
        # Our own messages and OS error classes contain no command/token argv.
        print('Reload error: '+str(e),file=sys.stderr);raise SystemExit(1)
