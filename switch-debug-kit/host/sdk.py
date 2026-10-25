"""Locate an installed SDK; never downloads tools or reads console keys."""
from pathlib import Path
import os,shutil

msys = Path(os.environ.get('NXDK_MSYS_ROOT', 'C:/msys64'))
value = os.environ.get('DEVKITPRO')
SDK = msys / 'opt/devkitpro' if not value or value == '/opt/devkitpro' else Path(value)
BIN = Path(os.environ.get('DEVKITA64', str(SDK / 'devkitA64'))) / 'bin'

def tool(name):
    suffix = '.exe' if os.name == 'nt' else ''
    return str(BIN / ('aarch64-none-elf-' + name + suffix))

def sdk_tool(name):
    return str(SDK / 'tools/bin' / (name + ('.exe' if os.name == 'nt' else '')))

HOST_CC = os.environ.get('NXDK_HOST_CC') or (str(msys/'mingw64/bin/gcc.exe') if (msys/'mingw64/bin/gcc.exe').is_file() else shutil.which('gcc') or 'gcc')
