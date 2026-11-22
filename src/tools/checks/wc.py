#!/usr/bin/env python3
"""Check actual wc across block boundaries, flags, totals and I/O errors."""
from pathlib import Path
import itertools
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='opensillicon-wc-') as directory:
    work = Path(directory)
    executable = work / 'wc'
    sanitizer = ['-fsanitize=address,undefined'] if os.environ.get('WC_SANITIZE') == '1' else []
    subprocess.run([*shlex.split(os.environ.get('CC', 'cc')),
                    '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-O1', '-g',
                    '-Wall', '-Wextra', '-Werror', *sanitizer,
                    'src/user/coreutils/wc.c', '-o', str(executable)],
                   cwd=root, check=True)

    def run(args=(), data=b'', expected=b'', status=0, **kwargs):
        result = subprocess.run([str(executable), *map(str, args)],
                                input=data, capture_output=True, **kwargs)
        assert result.returncode == status, (args, result.returncode, result.stderr)
        assert result.stdout == expected, (args, result.stdout[:100], expected[:100])
        assert b'Sanitizer' not in result.stderr, result.stderr
        return result

    def counts(data, flags='lwc', name=None):
        values = {'l': data.count(b'\n'), 'w': len(data.split()), 'c': len(data)}
        output = ' '.join(str(values[flag]) for flag in 'lwc' if flag in flags)
        if name is not None:
            output += ' ' + str(name)
        return (output + '\n').encode()

    source = work / 'input'
    for data in (b'', b'abc', b'one two\nthree\n', b' \t\v\f\r\n',
                 b'a\x00b\x80\xff\nc', b'x' * 8191 + b' a' + b'y' * 9000,
                 (b'one\ttwo\nthree\r' * 150000)):
        source.write_bytes(data)
        run(data=data, expected=counts(data))
        for width in (1, 2, 3):
            for flags in itertools.combinations('lwc', width):
                run(['-' + ''.join(flags), source], expected=counts(data, flags, source))
        run(['-c', '-w', '-l', '-c', source], expected=counts(data, name=source))
    source.write_bytes(b'one two\n')
    second = work / 'second'
    second.write_bytes(b'three\nfour\n')
    run([source, second], expected=counts(source.read_bytes(), name=source) +
        counts(second.read_bytes(), name=second) + counts(b'one two\nthree\nfour\n', name='total'))
    run(['-', '-'], data=b'one\n', expected=b'1 1 4 -\n0 0 0 -\n1 1 4 total\n')
    named = work / '-name'
    named.write_bytes(b'one\n')
    run(['--', '-name'], cwd=work, expected=b'1 1 4 -name\n')
    run(['-z'], status=2)
    run([work], status=1)
    result = run([work / 'missing', source], status=1,
                 expected=counts(source.read_bytes(), name=source) +
                 counts(source.read_bytes(), name='total'))
    assert b'missing' in result.stderr
    if Path('/dev/full').exists():
        with open('/dev/full', 'wb') as output:
            result = subprocess.run([str(executable), str(source)], stdout=output,
                                    stderr=subprocess.PIPE)
        assert result.returncode == 1 and b'wc:' in result.stderr, result.stderr
    print('WC_PASS: binary/large inputs, word boundaries, option combinations, totals and I/O errors')
