#!/usr/bin/env python3
"""Check rectangular framebuffer copies without accessing real hardware."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='opensillicon-framebuffer-rect-') as directory:
    output = Path(directory) / 'check'
    sanitizer = ['-fsanitize=address,undefined'] if os.environ.get('FRAMEBUFFER_RECT_SANITIZE') == '1' else []
    subprocess.run([*shlex.split(os.environ.get('CC', 'cc')),
                    '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                    '-Isrc/include', '-Isrc/limine', *sanitizer, 'src/tools/checks/framebuffer_rect.c',
                    'src/kernel/tty/fb/framebuffer_device.c', '-o', str(output)], cwd=root, check=True)
    subprocess.run([str(output)], check=True)
