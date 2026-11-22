#!/usr/bin/env python3
"""Check shared diagnostic streaming, errors and listing arguments."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='opensillicon-deviceconf-') as directory:
    work = Path(directory)
    executable = work / 'device-test'
    sanitizer = ['-fsanitize=address,undefined'] if os.environ.get('DEVICECONF_SANITIZE') == '1' else []
    subprocess.run([*shlex.split(os.environ.get('CC', 'cc')), '-std=c11', '-O1', '-g',
                    '-Wall', '-Wextra', '-Werror', *sanitizer, 'src/tools/checks/deviceconf.c',
                    'src/user/coreutils/deviceconf.c', '-o', str(executable)], cwd=root, check=True)

    def run(args, expected=b'', status=0):
        result = subprocess.run([str(executable), *map(str, args)], capture_output=True)
        assert result.returncode == status, (args, result.returncode, result.stderr)
        assert result.stdout == expected, (args, len(result.stdout), len(expected))
        assert b'Sanitizer' not in result.stderr, result.stderr
        return result

    source = work / 'input'
    data = bytes(range(256)) * 10000
    source.write_bytes(data)
    run([source], data)
    run([source, '-l'], data)
    source.write_bytes(b'')
    run([source], b'No devices found.\n')
    run([source, '-z'], status=2)
    run([source, '-l', 'extra'], status=2)
    result = run([work / 'missing'], status=1)
    assert b'device-test:' in result.stderr
    run([work], status=1)
    source.write_bytes(data)
    if Path('/dev/full').exists():
        with open('/dev/full', 'wb') as output:
            result = subprocess.run([str(executable), str(source)], stdout=output,
                                    stderr=subprocess.PIPE)
        assert result.returncode == 1 and b'device-test:' in result.stderr
    print('DEVICECONF_PASS: binary/large/empty input, listing flags, read/open/output failures')
