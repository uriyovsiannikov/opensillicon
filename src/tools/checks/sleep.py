#!/usr/bin/env python3
"""Check sleep input validation and continuation after an interrupted wait."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='opensillicon-sleep-') as directory:
    executable = Path(directory) / 'sleep-check'
    sanitizer = ['-fsanitize=address,undefined'] if os.environ.get('SLEEP_SANITIZE') == '1' else []
    subprocess.run([*shlex.split(os.environ.get('CC', 'cc')),
                    '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-O1', '-g',
                    '-Wall', '-Wextra', '-Werror', *sanitizer,
                    'src/tools/checks/sleep.c', '-o', str(executable)],
                   cwd=root, check=True)
    result = subprocess.run([str(executable)], capture_output=True, text=True)
    if result.returncode or 'Sanitizer' in result.stderr:
        raise AssertionError(result.stdout + result.stderr)
    print(result.stdout, end='')
