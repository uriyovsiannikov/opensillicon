#!/usr/bin/env python3
"""Check fetch's typed system queries without requiring a guest display."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='opensillicon-fetch-') as temporary:
    executable = Path(temporary) / 'fetch-check'
    sanitizer = ['-fsanitize=address,undefined'] if os.environ.get('FETCH_SANITIZE') == '1' else []
    subprocess.run([*shlex.split(os.environ.get('CC', 'cc')),
                    '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-O1', '-g',
                    '-Wall', '-Wextra', '-Werror', *sanitizer,
                    '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                    '-Isrc/user', '-Isrc/include',
                    '-Isrc/user/thirdparty/musl/opensillicon/include',
                    'src/tools/checks/fetch.c', '-o', str(executable)],
                   cwd=root, check=True)
    subprocess.run([str(executable)], check=True)
