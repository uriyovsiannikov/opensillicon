#!/usr/bin/env python3
"""Check global block flushing without issuing real hardware I/O."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='opensillicon-block-flush-') as directory:
    output = Path(directory) / 'check'
    sanitizer = ['-fsanitize=address,undefined'] if os.environ.get('BLOCK_FLUSH_SANITIZE') == '1' else []
    subprocess.run([*shlex.split(os.environ.get('CC', 'cc')),
                    '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                    '-Isrc/include', *sanitizer, 'src/tools/checks/block_flush.c',
                    'src/kernel/fs/block/block.c', '-o', str(output)], cwd=root, check=True)
    subprocess.run([str(output)], check=True)
