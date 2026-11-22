#!/usr/bin/env python3
"""Check PATH lookup, executable checks and literal path semantics in which."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='opensillicon-which-') as directory:
    work = Path(directory)
    executable = work / 'which'
    sanitizer = ['-fsanitize=address,undefined'] if os.environ.get('WHICH_SANITIZE') == '1' else []
    subprocess.run([*shlex.split(os.environ.get('CC', 'cc')),
                    '-std=c11', '-D_DEFAULT_SOURCE', '-D_POSIX_C_SOURCE=200809L', '-O1', '-g',
                    '-Wall', '-Wextra', '-Werror', *sanitizer,
                    'src/user/coreutils/which.c', '-o', str(executable)],
                   cwd=root, check=True)
    env = os.environ.copy()
    first, second = work / 'first', work / 'second'
    first.mkdir()
    second.mkdir()
    a, b = first / 'program', second / 'program'
    a.write_bytes(b'first')
    b.write_bytes(b'second')
    a.chmod(0o600)
    b.chmod(0o755)
    env['PATH'] = str(first) + ':' + str(second)

    def run(args=(), expected=b'', status=0, **kwargs):
        result = subprocess.run([str(executable), *map(str, args)],
                                capture_output=True, env=env, cwd=work, **kwargs)
        assert result.returncode == status, (args, result.returncode, result.stderr)
        assert result.stdout == expected, (args, result.stdout, expected)
        assert b'Sanitizer' not in result.stderr, result.stderr
        return result

    run(['program'], expected=(str(b) + '\n').encode())
    a.chmod(0o755)
    run(['program'], expected=(str(a) + '\n').encode())
    a.unlink()
    a.mkdir()
    run(['program'], expected=(str(b) + '\n').encode())
    run(['missing'], status=1)
    run(['missing', 'program'], status=1, expected=(str(b) + '\n').encode())
    run([b], expected=(str(b) + '\n').encode())
    env['PATH'] = ''
    local = work / 'local'
    local.write_bytes(b'local')
    local.chmod(0o755)
    run(['local'], expected=b'local\n')
    run(['./local'], expected=b'./local\n')
    env['PATH'] = ':missing'
    run(['local'], expected=b'local\n')
    env['PATH'] = 'missing:'
    run(['local'], expected=b'local\n')
    env['PATH'] = 'second'
    run(['program'], expected=b'second/program\n')
    env['PATH'] = 'x' * 5000 + ':' + str(second)
    run(['program'], expected=(str(b) + '\n').encode())
    # Kernel path traversal through a symlink differs from lexical normalization.
    nested = second / 'nested'
    nested.mkdir()
    (work / 'link').symlink_to(nested, target_is_directory=True)
    run(['./link/../program'], expected=b'./link/../program\n')
    run([''], status=1)
    run(status=2)
    run(['-z'], status=2)
    named = work / '-name'
    named.write_bytes(b'option operand')
    named.chmod(0o755)
    env['PATH'] = ''
    run(['--', '-name'], expected=b'-name\n')
    if Path('/dev/full').exists():
        with open('/dev/full', 'wb') as output:
            result = subprocess.run([str(executable), str(b)], env=env,
                                    stdout=output, stderr=subprocess.PIPE)
        assert result.returncode == 1 and b'which: stdout' in result.stderr, result.stderr
    print('WHICH_PASS: PATH ordering/empty/relative/long entries, file access, symlinks and output errors')
