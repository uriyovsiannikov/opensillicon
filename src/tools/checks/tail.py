#!/usr/bin/env python3
"""Exercise the actual tail source with large lines, streams and I/O failures."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='opensillicon-tail-') as directory:
    work = Path(directory)
    executable = work / 'tail'
    sanitizer = ['-fsanitize=address,undefined'] if os.environ.get('TAIL_SANITIZE') == '1' else []
    subprocess.run([*shlex.split(os.environ.get('CC', 'cc')),
                    '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-O1', '-g',
                    '-Wall', '-Wextra', '-Werror', *sanitizer,
                    'src/user/coreutils/tail.c', '-o', str(executable)],
                   cwd=root, check=True)

    def run(args=(), data=b'', expected=b'', status=0, **kwargs):
        result = subprocess.run([str(executable), *map(str, args)],
                                input=data, capture_output=True, **kwargs)
        assert result.returncode == status, (args, result.returncode, result.stderr)
        assert result.stdout == expected, (args, len(result.stdout), len(expected))
        assert b'Sanitizer' not in result.stderr, result.stderr
        return result

    run(data=b'')
    data = b''.join(f'line {i}\n'.encode() for i in range(30))
    run(data=data, expected=b''.join(data.splitlines(keepends=True)[-10:]))
    for data in (b'a\nb\n', b'a\nb', b'\n\n\n', b'zero\x00byte\nlast\x00byte',
                 b'x' * (2 * 1024 * 1024) + b'\n',
                 b'first\n' + b'z' * (2 * 1024 * 1024),
                 b'x' * 200 + b'\n' * (1024 * 1024 + 1)):
        source = work / 'input'
        source.write_bytes(data)
        # Only LF terminates lines; bytes.splitlines also treats CR/VT/FF as breaks.
        parts = data.split(b'\n')
        chunks = [part + b'\n' for part in parts[:-1]]
        if parts[-1]:
            chunks.append(parts[-1])
        for count in (0, 1, 2, 10, 2000000, 18446744073709551615):
            expected = b''.join(chunks[-count:]) if count else b''
            run(['-n', count], data=data, expected=expected)
            run(['-n', count, source], expected=expected)
    run(['-n', 1, '-'], data=b'a\nb\n', expected=b'b\n')
    for invalid in ('', '-1', '+1', '1x', '18446744073709551616'):
        result = run(['-n', invalid], status=2)
        assert b'usage: tail' in result.stderr
    run(['-n'], status=2)
    source = work / '-name'
    source.write_bytes(b'file\n')
    run(['--', '-name'], cwd=work, expected=b'file\n')
    run([work], status=1)
    missing = work / 'missing'
    result = run([missing, source], status=1,
                 expected=f'==> {source} <==\nfile\n'.encode())
    assert str(missing).encode() in result.stderr
    run(['-n', 1, source, '-'], data=b'stdin\n',
        expected=f'==> {source} <==\nfile\n\n==> - <==\nstdin\n'.encode())
    if Path('/dev/full').exists():
        with open('/dev/full', 'wb') as output:
            result = subprocess.run([str(executable), str(source)], stdout=output,
                                    stderr=subprocess.PIPE)
        assert result.returncode == 1 and b'stdout' in result.stderr, result.stderr
    print('TAIL_PASS: files/stdin, large and binary lines, counts, headers and I/O errors')
