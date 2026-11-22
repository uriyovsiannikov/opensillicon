#!/usr/bin/env python3
"""Check basename/dirname corner cases and suffix/output handling."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='opensillicon-path-names-') as temporary:
    work = Path(temporary)
    sanitizer = ['-fsanitize=address,undefined'] if os.environ.get('PATH_NAMES_SANITIZE') == '1' else []
    for name in ('basename', 'dirname'):
        subprocess.run([*shlex.split(os.environ.get('CC', 'cc')), '-std=c11', '-O1', '-g',
                        '-Wall', '-Wextra', '-Werror', *sanitizer,
                        f'src/user/coreutils/{name}.c', '-o', str(work / name)], cwd=root, check=True)

    def run(name, args=(), expected='', status=0):
        result = subprocess.run([str(work / name), *args], capture_output=True, text=True)
        assert result.returncode == status, (name, args, result.stderr)
        assert result.stdout == expected, (name, args, result.stdout, expected)
        assert 'Sanitizer' not in result.stderr, result.stderr
        return result

    for path, base, directory in (
            ('', '.', '.'), ('/', '/', '/'), ('////', '/', '/'),
            ('file', 'file', '.'), ('file///', 'file', '.'),
            ('/file', 'file', '/'), ('/dir/file', 'file', '/dir'),
            ('dir//file///', 'file', 'dir'), ('dir/..', '..', 'dir'),
            ('dir/.', '.', 'dir'), ('./file', 'file', '.'),
            ('a/b/c', 'c', 'a/b'), ('a/' + 'z' * 10000, 'z' * 10000, 'a')):
        run('basename', [path], base + '\n')
        run('dirname', [path], directory + '\n')
    for path, suffix, expected in (('/dir/file.txt', '.txt', 'file'),
                                    ('file', 'file', 'file'), ('file', '', 'file'),
                                    ('file', 'long-file', 'file'), ('file', 'ile', 'f'),
                                    ('file', 'foo', 'file'), ('file///', 'ile', 'f')):
        run('basename', [path, suffix], expected + '\n')
    run('basename', ['--', '-name'], '-name\n')
    run('dirname', ['--', '-dir/file'], '-dir\n')
    for name in ('basename', 'dirname'):
        result = run(name, status=2)
        assert 'usage:' in result.stderr
        run(name, ['--'], status=2)
        run(name, ['a', 'b', 'c'], status=2)
        if Path('/dev/full').exists():
            with open('/dev/full', 'wb') as output:
                result = subprocess.run([str(work / name), 'file'], stdout=output,
                                        stderr=subprocess.PIPE)
            assert result.returncode == 1 and b'stdout' in result.stderr, result.stderr
    print('PATH_NAMES_PASS: empty/root/trailing slashes, long paths, suffixes and output errors')
