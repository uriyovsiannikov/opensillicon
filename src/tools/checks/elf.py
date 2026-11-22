#!/usr/bin/env python3
"""Check ELF validation without booting the kernel."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='opensillicon-elf-') as directory:
    output = Path(directory) / 'elf-check'
    sanitizer = ['-fsanitize=address,undefined'] if os.environ.get('ELF_SANITIZE') == '1' else []
    subprocess.run([*shlex.split(os.environ.get('CC', 'cc')),
                    '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                    '-Isrc/include', '-Isrc/limine', *sanitizer,
                    'src/tools/checks/elf.c', 'src/kernel/proc/elf.c',
                    '-o', str(output)], cwd=root, check=True)
    subprocess.run([str(output)], check=True)
