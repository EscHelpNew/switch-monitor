import importlib.util,json,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('kit_project',ROOT/'host/project.py')
project=importlib.util.module_from_spec(spec);spec.loader.exec_module(project)
class ProjectTests(unittest.TestCase):
 def config(self,**updates):
  cfg=dict(name='My Game',author='My Team',title_id='0000000000000000',sd_directory='my-game-dev',host='127.0.0.1');cfg.update(updates);return cfg
 def test_default_cannot_restart_any_title(self):
  cfg=project.validate(self.config());header=project.header(cfg)
  self.assertIn('0x0000000000000000ULL',header);self.assertIn('sdmc:/switch/my-game-dev',header)
  self.assertNotIn('0500424C41430000',header)
 def test_path_or_identity_injection_is_rejected(self):
  for changes in [dict(sd_directory='../save'),dict(sd_directory='x/y'),dict(title_id='not-hex'),dict(title_id='0'*17),dict(name='x'*16),dict(name='Bad"Name'),dict(author=''),dict(host='bad host')]:
   with self.subTest(changes=changes),self.assertRaises(ValueError):project.validate(self.config(**changes))
 def test_explicit_project_identity_is_preserved(self):
  cfg=project.validate(self.config(title_id='0500000000001234',author='Demo "Team"'))
  h=project.header(cfg);self.assertIn('0x0500000000001234ULL',h);self.assertIn(r'Demo \"Team\"',h)
 def test_prepare_does_not_replace_existing_secret(self):
  with tempfile.TemporaryDirectory(prefix='kit space ') as temp:
   root=Path(temp);(root/'project.example.json').write_text(json.dumps(self.config()))
   project.prepare(root);key=(root/'data/token.txt').read_text();self.assertRegex(key.strip(),'^[0-9a-f]{64}$')
   project.prepare(root);self.assertEqual(key,(root/'data/token.txt').read_text())
if __name__=='__main__':unittest.main()
