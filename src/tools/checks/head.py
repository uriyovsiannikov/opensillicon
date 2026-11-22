#!/usr/bin/env python3
"""Check head's line boundaries, stdin reuse, counts and I/O errors."""
from pathlib import Path
import os
import select
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='opensillicon-head-') as directory:
    work = Path(directory)
    executable = work / 'head'
    sanitizer = ['-fsanitize=address,undefined'] if os.environ.get('HEAD_SANITIZE') == '1' else []
    subprocess.run([*shlex.split(os.environ.get('CC', 'cc')),
                    '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-O1', '-g',
                    '-Wall', '-Wextra', '-Werror', *sanitizer,
                    'src/user/coreutils/head.c', '-o', str(executable)],
                   cwd=root, check=True)

    def run(args=(), data=b'', expected=b'', status=0, **kwargs):
        result = subprocess.run([str(executable), *map(str, args)],
                                input=data, capture_output=True, **kwargs)
        assert result.returncode == status, (args, result.returncode, result.stderr)
        assert result.stdout == expected, (args, len(result.stdout), len(expected))
        assert b'Sanitizer' not in result.stderr, result.stderr
        return result

    for data in (b'', b'a\nb\n', b'a\nb', b'\n\n\n', b'a\x00b\rc\nd\x00e',
                 b'x' * (2 * 1024 * 1024) + b'\nend\n',
                 b''.join(f'line {i}\n'.encode() for i in range(30))):
        source = work / 'input'
        source.write_bytes(data)
        parts = data.split(b'\n')
        chunks = [part + b'\n' for part in parts[:-1]]
        if parts[-1]:
            chunks.append(parts[-1])
        run(data=data, expected=b''.join(chunks[:10]))
        for count in (0, 1, 2, 10, 1000001, 18446744073709551615):
            expected = b''.join(chunks[:count])
            run(['-n', count], data=data, expected=expected)
            run(['-n', count, source], expected=expected)
    # Repeated stdin operands must preserve the data beyond each requested line.
    run(['-n', 1, '-', '-'], data=b'first\nsecond\nthird\n',
        expected=b'==> - <==\nfirst\n\n==> - <==\nsecond\n')
    for invalid in ('', '-1', '+1', '1x', '18446744073709551616'):
        result = run(['-n', invalid], status=2)
        assert b'usage: head' in result.stderr
    run(['-n'], status=2)
    named = work / '-name'
    named.write_bytes(b'file\n')
    run(['--', '-name'], cwd=work, expected=b'file\n')
    run([work], status=1)
    run([work / 'missing', named], status=1,
        expected=f'==> {named} <==\nfile\n'.encode())
    # Reaching the requested line must finish without waiting for EOF.
    process = subprocess.Popen([str(executable), '-n', '1'], stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        process.stdin.write(b'one\ntwo\n')
        process.stdin.flush()
        assert select.select([process.stdout], [], [], 5)[0], 'head waited for EOF'
        assert process.wait(timeout=5) == 0
        assert process.stdout.read() == b'one\n'
        assert not process.stderr.read()
    finally:
        process.stdin.close()
        if process.poll() is None:
            process.kill()
            process.wait()
    if Path('/dev/full').exists():
        with open('/dev/full', 'wb') as output:
            result = subprocess.run([str(executable), str(named)], stdout=output,
                                    stderr=subprocess.PIPE)
        assert result.returncode == 1 and b'head:' in result.stderr, result.stderr
    print('HEAD_PASS: binary/large lines, counts, stdin boundaries and I/O errors')
