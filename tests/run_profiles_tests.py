#!/usr/bin/env python3
"""Profiles save/load round-trip against the real backend, data dir redirected to a tmp path."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
os.environ.setdefault('ASAN_OPTIONS', 'detect_leaks=0:halt_on_error=1' if sys.platform == 'darwin' else 'detect_leaks=1:halt_on_error=1')
os.environ.setdefault('UBSAN_OPTIONS', 'halt_on_error=1:print_stacktrace=1')

with tempfile.TemporaryDirectory(prefix='pldmgr-profiles-') as temp:
    build = Path(temp)
    (build / 'data').mkdir()
    # Redirect the data root into the sandbox so profiles.json is written there.
    (build / 'pldmgr.h').write_text((ROOT / 'include/pldmgr.h').read_text().replace('/data/pldmgr', str(build / 'data')))
    flags = shlex.split(os.environ.get('HOST_CC', 'clang')) + [
        '-std=c11', '-D_DEFAULT_SOURCE', '-g', '-O1', '-Wall', '-Wextra',
        '-fno-omit-frame-pointer', '-fsanitize=address,undefined',
        '-I' + str(build), '-Iinclude',
    ]
    srcs = ['tests/profiles_driver.c', 'src/profiles.c', 'src/json_helpers.c', 'src/utils.c', 'src/config.c']
    binary = build / 'profiles_tests'
    subprocess.run(flags + srcs + ['-o', str(binary)], cwd=ROOT, check=True)
    result = subprocess.run([str(binary)], cwd=ROOT, text=True, capture_output=True, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    print(result.stdout.strip())
