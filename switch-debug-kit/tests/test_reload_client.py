"""Socket-level client tests against an independent tiny protocol peer."""
from pathlib import Path
import importlib.util,hashlib,socket,threading,unittest,tempfile,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'host'))
spec=importlib.util.spec_from_file_location('reload_client',ROOT/'host/reload.py')
mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
TOKEN='a'*64
class ClientTest(unittest.TestCase):
    def test_cli_starts_without_parser_conflicts(self):
        result=subprocess.run([sys.executable,str(ROOT/'host/reload.py'),'--help'],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('deploy',result.stdout)
    def test_crash_report_read_is_bounded_and_redacts_key(self):
        data=b'Process: GAME\nargv='+TOKEN.encode()+b'\n'
        def handle(c,f):
            self.assertEqual(f.readline(),b'CRASH\n');c.sendall(f'OK REPORT {len(data)}\n'.encode()+data)
        port,t,errors=self.peer(handle)
        report=mod.crash('127.0.0.1',TOKEN,port)
        self.assertIn(b'Process: GAME',report);self.assertNotIn(TOKEN.encode(),report)
        t.join(3);self.assertEqual(errors,[])
    def peer(self,handle):
        server=socket.socket();server.bind(('127.0.0.1',0));server.listen(1);server.settimeout(3)
        errors=[]
        def work():
            try:
                conn,_=server.accept()
                with conn,conn.makefile('rb') as f:
                    self.assertEqual(f.readline(),b'AUTH '+TOKEN.encode()+b'\n')
                    conn.sendall(b'OK AUTH\n');handle(conn,f)
            except BaseException as e:errors.append(e)
            finally:server.close()
        thread=threading.Thread(target=work,daemon=True);thread.start()
        return server.getsockname()[1],thread,errors
    def test_put_streams_exact_bytes_and_checks_ack(self):
        data=b'x'*100000
        def handle(c,f):
            self.assertEqual(f.readline(),f'PUT GAME {len(data)} {hashlib.sha256(data).hexdigest()}\n'.encode())
            c.sendall(b'OK SEND\n');self.assertEqual(f.read(len(data)),data);c.sendall(b'OK STORED\n')
        port,t,errors=self.peer(handle)
        with tempfile.TemporaryDirectory() as td:
            p=Path(td)/'lab.nro';p.write_bytes(data)
            self.assertEqual(mod.put('127.0.0.1',TOKEN,'GAME',p,port),hashlib.sha256(data).hexdigest())
        t.join(3);self.assertFalse(t.is_alive());self.assertEqual(errors,[])
    def test_failed_store_is_not_a_success(self):
        def handle(c,f):
            f.readline();c.sendall(b'OK SEND\n');f.read(128);c.sendall(b'ERR verification\n')
        port,t,errors=self.peer(handle)
        with tempfile.TemporaryDirectory() as td:
            p=Path(td)/'bad.nro';p.write_bytes(bytes(128))
            with self.assertRaises(mod.ReloadError):mod.put('127.0.0.1',TOKEN,'GAME',p,port)
        t.join(3);self.assertEqual(errors,[])
    def test_rejects_arbitrary_target_before_connect(self):
        with self.assertRaises(ValueError):mod.put('127.0.0.1',TOKEN,'../save',Path('missing'))
    def test_status_decodes_only_versioned_peer(self):
        def handle(c,f):
            self.assertEqual(f.readline(),b'STATUS\n');c.sendall(b'{"protocol":2,"app":"ARM lab"}\n')
        port,t,errors=self.peer(handle)
        self.assertEqual(mod.status('127.0.0.1',TOKEN,port)['app'],'ARM lab');t.join(3);self.assertEqual(errors,[])
    def test_rejects_previous_protocol_before_using_peer(self):
        def handle(c,f):
            self.assertEqual(f.readline(),b'STATUS\n');c.sendall(b'{"protocol":1,"app":"Game"}\n')
        port,t,errors=self.peer(handle)
        with self.assertRaises(mod.ReloadError):mod.status('127.0.0.1',TOKEN,port)
        t.join(3);self.assertEqual(errors,[])
if __name__=='__main__':unittest.main()
