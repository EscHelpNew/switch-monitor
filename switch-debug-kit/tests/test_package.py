"""Exercise the real source packager with private files present; never use a console."""
from pathlib import Path
import json
import subprocess
import tempfile
import unittest
import zipfile
import shutil
import secrets

MONITOR = Path(__file__).resolve().parents[2]

class PackageTests(unittest.TestCase):
    def test_clean_archive_is_complete_and_private_files_stay_out(self):
        with tempfile.TemporaryDirectory(prefix='switch kit package ') as temp:
            root = Path(temp) / 'switch-monitor'
            shutil.copytree(MONITOR, root, ignore=shutil.ignore_patterns('build', 'data', '__pycache__', 'project.json'))
            kit = root / 'switch-debug-kit'
            private_blob = ('PRIVATE_' + secrets.token_hex(24)).encode('ascii')
            (kit / 'data').mkdir(); (kit / 'data/token.txt').write_bytes(private_blob)
            (kit / 'project.json').write_text('{"private":"configuration"}')
            (kit / 'build').mkdir(); (kit / 'build/private.nro').write_bytes(b'not a release')
            result = subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(root / 'package.ps1')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            with zipfile.ZipFile(root / 'build/switch-monitor-source.zip') as archive:
                names = archive.namelist()
                for path in ['README.md', 'AGENTS.md', 'host/reload.py', 'receiver/reload_launch.c', 'lab/source/main.c', 'lab/include/lab_abi.h', 'docs/INTEGRATION.md', 'build.py']:
                    self.assertIn('switch-monitor/switch-debug-kit/' + path, names)
                self.assertEqual(len(names), len(set(names)))
                self.assertFalse(any('/data/' in n or '/build/' in n or n.endswith('/project.json') for n in names))
                self.assertFalse(any(private_blob in archive.read(n) for n in names))

    def test_manifest_path_cannot_escape_kit(self):
        with tempfile.TemporaryDirectory(prefix='switch kit manifest ') as temp:
            root = Path(temp) / 'switch-monitor'
            shutil.copytree(MONITOR, root, ignore=shutil.ignore_patterns('build', 'data', '__pycache__', 'project.json'))
            manifest = root / 'switch-debug-kit/package-files.txt'
            with manifest.open('a', encoding='utf-8') as stream: stream.write('\n../README.md\n')
            result = subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(root / 'package.ps1')], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)

if __name__ == '__main__': unittest.main()
