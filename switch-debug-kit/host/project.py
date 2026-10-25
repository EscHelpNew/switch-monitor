"""Shared local project configuration. No game repository or third-party Python packages."""
from pathlib import Path
import ipaddress
import json
import re
import secrets

ROOT = Path(__file__).resolve().parents[1]

def validate(value):
    if not isinstance(value, dict):
        raise ValueError('Project must be a JSON object')
    cfg = dict(value)
    # The qualified receiver's app_name[16] is also the reconnect identity.
    # Preserve that contract and its simple JSON status representation.
    if not isinstance(cfg.get('name'), str) or not re.fullmatch('[A-Za-z0-9][A-Za-z0-9 ._-]{0,14}', cfg['name']):
        raise ValueError('name must be 1..15 ASCII letters/digits/spaces/dot/underscore/hyphen')
    for field, limit in [('name', 128), ('author', 64)]:
        text = cfg.get(field)
        if not isinstance(text, str) or not text.strip() or len(text.encode('utf-8')) > limit or any(ord(c) < 32 for c in text):
            raise ValueError('Invalid project ' + field)
    if not isinstance(cfg.get('title_id'), str) or not re.fullmatch('[0-9a-fA-F]{16}', cfg['title_id']):
        raise ValueError('title_id must be exactly 16 hexadecimal digits; zero disables RUN')
    cfg['title_id'] = cfg['title_id'].upper()
    if not isinstance(cfg.get('sd_directory'), str) or not re.fullmatch('[a-z0-9][a-z0-9_-]{0,47}', cfg['sd_directory']):
        raise ValueError('sd_directory must be one lowercase directory name, never a path')
    try:
        cfg['host'] = str(ipaddress.IPv4Address(cfg['host']))
    except (ValueError, KeyError, TypeError):
        raise ValueError('host must be an IPv4 address') from None
    return cfg

def load(root=ROOT):
    path = root / 'project.json'
    if not path.exists():
        path = root / 'project.example.json'
    return validate(json.loads(path.read_text(encoding='utf-8-sig')))

def header(cfg):
    cfg = validate(cfg)
    quote = lambda text: json.dumps(text, ensure_ascii=False)
    return '\n'.join([
        '/* Generated from project.json. Include this directory in BOTH targets. */',
        '#ifndef SWITCH_DEBUG_PROJECT_H', '#define SWITCH_DEBUG_PROJECT_H',
        '#define NXDK_TITLE_ID 0x' + cfg['title_id'] + 'ULL',
        '#define NXDK_APP_NAME ' + quote(cfg['name']),
        '#define NXDK_APP_AUTHOR ' + quote(cfg['author']),
        '#define NXDK_SD_ROOT ' + quote('sdmc:/switch/' + cfg['sd_directory']),
        '#define NXDK_BRIDGE_FILE "bridge.nro"',
        '#define NXDK_GAME_FILE "game.nro"',
        '#define NXDK_LAB_FILE "arm-lab.nro"',
        '#endif', ''
    ])

def prepare(root=ROOT):
    cfg = load(root)  # Validate before generating any local secret.
    if not (root / 'project.json').exists():
        (root / 'project.json').write_text(json.dumps(cfg, indent=2) + '\n', encoding='utf-8')
    data = root / 'data'
    data.mkdir(exist_ok=True)
    token = data / 'token.txt'
    try:
        with token.open('x', encoding='ascii') as stream:
            stream.write(secrets.token_hex(32) + '\n')
    except FileExistsError:
        pass
    if re.fullmatch('[0-9a-fA-F]{64}', token.read_text().strip()) is None:
        raise ValueError('Existing data/token.txt is invalid; not overwritten')
    return cfg

if __name__ == '__main__':
    cfg = prepare()
    print('Local project.json and data/token.txt ready; secret is never printed.')
    print('Configure your own installed title ID/name/author before using RUN.')
