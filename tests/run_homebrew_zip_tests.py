#!/usr/bin/env python3
"""Homebrew .zip extraction against the real backend: safe archive extracts with
its folder tree + install marker; a zip-slip archive is rejected and nothing
escapes HOMEBREW_DIR. Data dir redirected to a tmp sandbox."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
os.environ.setdefault('ASAN_OPTIONS', 'detect_leaks=0:halt_on_error=1' if sys.platform == 'darwin' else 'detect_leaks=1:halt_on_error=1')

with tempfile.TemporaryDirectory(prefix='pldmgr-hbzip-') as temp:
    build = Path(temp)
    hb = build / 'hb'          # becomes HOMEBREW_DIR after the sed redirect
    (build / 'pldmgr.h').write_text(
        (ROOT / 'include/pldmgr.h').read_text()
        .replace('/data/homebrew', str(hb))
        .replace('/data/pldmgr', str(build / 'data')))
    flags = shlex.split(os.environ.get('HOST_CC', 'clang')) + [
        '-std=c11', '-D_DEFAULT_SOURCE', '-g', '-O1',
        '-fno-omit-frame-pointer', '-fsanitize=address',
        '-I' + str(build), '-Iinclude', '-Ithird_party/miniz',
    ]
    srcs = ['tests/homebrew_zip_driver.c', 'src/homebrew_zip.c',
            'third_party/miniz/miniz.c', 'src/utils.c', 'src/json_helpers.c']
    binary = build / 'hbzip_tests'
    subprocess.run(flags + srcs + ['-o', str(binary)], cwd=ROOT, check=True)

    # Safe archive: a nested folder dump.
    safe = build / 'safe.zip'
    with zipfile.ZipFile(safe, 'w') as z:
        z.writestr('App/eboot.bin', b'GAME')
        z.writestr('App/sce_sys/param.json', b'{}')
        z.writestr('App/data/level.pak', b'x' * 4096)
    # Malicious archive: a zip-slip entry that must not escape.
    evil = build / 'evil.zip'
    with zipfile.ZipFile(evil, 'w') as z:
        z.writestr('ok/file.bin', b'x')
        z.writestr('../../escapee.bin', b'ESCAPE')

    r = subprocess.run([str(binary), 'prosperotv.zip', str(safe)], cwd=ROOT, text=True, capture_output=True, timeout=30)
    assert r.returncode == 0, 'safe.zip should extract: ' + r.stdout + r.stderr
    assert (hb / 'App' / 'eboot.bin').read_bytes() == b'GAME', 'nested file extracted'
    assert (hb / 'App' / 'sce_sys' / 'param.json').exists(), 'deep nested file extracted'
    assert (hb / '.pldmgrx_installed' / 'prosperotv.zip').exists(), 'install marker written'
    assert not safe.exists(), 'source archive consumed on success'

    r = subprocess.run([str(binary), 'evil.zip', str(evil)], cwd=ROOT, text=True, capture_output=True, timeout=30)
    assert r.returncode != 0, 'zip-slip archive must be rejected: ' + r.stdout + r.stderr
    assert not (build / 'escapee.bin').exists(), 'zip-slip must not escape HOMEBREW_DIR'
    assert not (hb.parent / 'escapee.bin').exists(), 'zip-slip must not escape HOMEBREW_DIR'

    print('Homebrew zip: nested extract + marker + source-consume; zip-slip rejected with no escape')
