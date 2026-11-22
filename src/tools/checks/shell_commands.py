#!/usr/bin/env python3
"""Exercise actual shell command enumeration and completion against PATH fixtures."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='opensillicon-shell-commands-') as directory:
    work = Path(directory)
    executable = work / 'bin' / 'shell-check'
    executable.parent.mkdir()
    sanitizer = ['-fsanitize=address,undefined'] if os.environ.get('SHELL_COMMANDS_SANITIZE') == '1' else []
    subprocess.run([*shlex.split(os.environ.get('CC', 'cc')),
                    '-std=c11', '-D_GNU_SOURCE', '-O1', '-g', '-ffunction-sections', '-fdata-sections',
                    '-Wall', '-Wextra', '-Werror', *sanitizer, '-Wl,--gc-sections',
                    '-Isrc/user', '-Isrc/include', 'src/tools/checks/shell_commands.c',
                    '-o', str(executable)], cwd=root, check=True)
    first, second = work / 'first', work / 'second'
    first.mkdir()
    second.mkdir()

    def command(path, mode=0o755):
        path.write_bytes(b'fixture')
        path.chmod(mode)

    command(first / 'program')
    command(second / 'program')
    command(second / 'another')
    command(second / 'help')
    command(second / 'not-executable', 0o600)
    (second / 'directory').mkdir()
    (second / 'broken').symlink_to('missing')
    env = os.environ.copy()

    def check(path, expected, prefix='pro', completed='program ', capacity=16, status=0):
        env['PATH'] = path
        result = subprocess.run([str(executable), str(capacity), prefix], cwd=work,
                                env=env, capture_output=True, text=True)
        assert result.returncode == 0 and 'Sanitizer' not in result.stderr, result.stderr
        lines = result.stdout.splitlines()
        assert lines[0] == f'RESULT {status}', result.stdout
        names = [line[5:] for line in lines if line.startswith('NAME ')]
        assert set(names) == set(expected) and len(names) == len(expected), result.stdout
        assert lines[-1] == 'COMPLETE ' + completed, result.stdout

    check(str(first) + ':' + str(second) + ':' + str(first), ['help', 'program', 'another'])
    check('missing:' + str(first), ['help', 'program'])
    check('second', ['help', 'program', 'another'])
    command(work / 'local')
    check('', ['help', 'local'], prefix='loc', completed='local ')
    check('missing:', ['help', 'local'], prefix='loc', completed='local ')
    check(':missing', ['help', 'local'], prefix='loc', completed='local ')
    check('missing', ['help'], prefix='pro', completed='pro')
    check('x' * 5000 + ':' + str(first), ['help', 'program'])
    check(str(first), ['help'], capacity=1, status=-2)
    command(second / 'provide')
    check(str(second), ['help', 'program', 'another', 'provide'], prefix='pr', completed='pro')
    print('SHELL_COMMANDS_PASS: shared PATH scan, duplicates, permissions/types, empty/relative entries and completion')
