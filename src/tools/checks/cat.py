#!/usr/bin/env python3
"""Check binary concatenation, streaming -u output and error separation."""
from pathlib import Path
import os
import select
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='opensillicon-cat-') as directory:
    work = Path(directory)
    executable = work / 'cat'
    sanitizer = ['-fsanitize=address,undefined'] if os.environ.get('CAT_SANITIZE') == '1' else []
    subprocess.run([*shlex.split(os.environ.get('CC', 'cc')),
                    '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-O1', '-g',
                    '-Wall', '-Wextra', '-Werror', *sanitizer,
                    'src/user/coreutils/cat.c', '-o', str(executable)],
                   cwd=root, check=True)

    def run(args=(), data=b'', expected=b'', status=0, **kwargs):
        result = subprocess.run([str(executable), *map(str, args)],
                                input=data, capture_output=True, **kwargs)
        assert result.returncode == status, (args, result.returncode, result.stderr)
        assert result.stdout == expected, (args, len(result.stdout), len(expected))
        assert b'Sanitizer' not in result.stderr, result.stderr
        return result

    data = bytes(range(256)) * 10001
    source = work / 'input'
    source.write_bytes(data)
    empty = work / 'empty'
    empty.touch()
    run(data=data, expected=data)
    run([empty])
    run([source, '-', source], data=b'stdin\n', expected=data + b'stdin\n' + data)
    run(['-u', source], expected=data)
    result = subprocess.run([str(executable), str(source), '-', str(source)],
                            capture_output=True, preexec_fn=lambda: os.close(0))
    assert result.returncode == 1 and result.stdout == data * 2, result.stderr
    assert b'cat: -:' in result.stderr and b'Sanitizer' not in result.stderr
    run(['-z'], status=2)
    run([work], status=1)
    result = run([work / 'missing', source], status=1, expected=data)
    assert b'missing' in result.stderr
    named = work / '-name'
    named.write_bytes(b'operand\n')
    run(['--', '-name'], cwd=work, expected=b'operand\n')
    # Output must arrive while the short input stream remains open.
    process = subprocess.Popen([str(executable), '-u'], stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        process.stdin.write(b'live\x00input')
        process.stdin.flush()
        assert select.select([process.stdout], [], [], 5)[0], 'cat -u waited for EOF'
        assert os.read(process.stdout.fileno(), 10) == b'live\x00input'
        process.stdin.close()
        assert process.wait(timeout=5) == 0
        assert not process.stderr.read()
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
    if Path('/dev/full').exists():
        with open('/dev/full', 'wb') as output:
            result = subprocess.run([str(executable), str(source)], stdout=output,
                                    stderr=subprocess.PIPE)
        assert result.returncode == 1 and b'cat:' in result.stderr, result.stderr
    print('CAT_PASS: binary files/stdin, unbuffered streaming, option termination and I/O errors')
