#!/usr/bin/env python3
"""Check stat's libc metadata, link policy, type/permission output and errors."""
from pathlib import Path
import os
import shlex
import socket
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='opensillicon-stat-') as directory:
    work = Path(directory)
    executable = work / 'stat'
    sanitizer = ['-fsanitize=address,undefined'] if os.environ.get('STAT_SANITIZE') == '1' else []
    subprocess.run([*shlex.split(os.environ.get('CC', 'cc')),
                    '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-O1', '-g',
                    '-Wall', '-Wextra', '-Werror', *sanitizer,
                    'src/user/coreutils/stat.c', '-o', str(executable)],
                   cwd=root, check=True)

    def run(args=(), status=0, **kwargs):
        result = subprocess.run([str(executable), *map(str, args)],
                                capture_output=True, text=True, **kwargs)
        assert result.returncode == status, (args, result.returncode, result.stderr)
        assert 'Sanitizer' not in result.stderr, result.stderr
        return result

    source = work / 'file'
    source.write_bytes(b'abc\x00def')
    source.chmod(0o4715)
    result = run([source])
    assert '  Type: regular file\n  Size: 7 bytes\n  Mode: 4715\n' in result.stdout
    assert '  Type: directory\n' in run([work]).stdout
    link = work / 'link'
    link.symlink_to('file')
    assert '  Type: symbolic link\n  Size: 4 bytes\n' in run([link]).stdout
    assert '  Type: regular file\n  Size: 7 bytes\n' in run(['-L', link]).stdout
    dangling = work / 'dangling'
    dangling.symlink_to('missing')
    assert '  Type: symbolic link\n' in run([dangling]).stdout
    run(['-L', dangling], status=1)
    fifo = work / 'fifo'
    os.mkfifo(fifo)
    assert '  Type: fifo\n' in run([fifo]).stdout
    with socket.socket(socket.AF_UNIX) as listener:
        endpoint = work / 'socket'
        listener.bind(str(endpoint))
        assert '  Type: socket\n' in run([endpoint]).stdout
    assert '  Type: character device\n' in run(['/dev/null']).stdout
    result = run([work / 'missing', source], status=1)
    assert result.stdout.count('  File: ') == 1 and 'regular file' in result.stdout
    assert 'stat: cannot stat:' in result.stderr
    run(status=2)
    run(['-z'], status=2)
    named = work / '-name'
    named.touch()
    assert '  File: -name\n' in run(['--', '-name'], cwd=work).stdout
    if Path('/dev/full').exists():
        with open('/dev/full', 'wb') as output:
            result = subprocess.run([str(executable), str(source)], stdout=output,
                                    stderr=subprocess.PIPE)
        assert result.returncode == 1 and b'stat: stdout' in result.stderr, result.stderr
    print('STAT_PASS: sizes/modes, links, special types, mixed operands and I/O errors')
