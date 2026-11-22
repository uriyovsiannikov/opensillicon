#!/usr/bin/env python3
"""Check public POSIX ls/rm behavior in an isolated host directory."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='opensillicon-dirutils-') as directory:
    work = Path(directory)
    binaries = {}
    sanitizer = ['-fsanitize=address,undefined'] if os.environ.get('DIRUTILS_SANITIZE') == '1' else []
    for name in ['ls', 'rm']:
        binaries[name] = work / name
        subprocess.run([*shlex.split(os.environ.get('CC', 'cc')),
                        '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-O1', '-g',
                        '-Wall', '-Wextra', '-Werror', *sanitizer,
                        str(root / f'src/user/coreutils/{name}.c'),
                        '-o', str(binaries[name])], check=True)

    def run(name, *args, succeeds=True):
        result = subprocess.run([str(binaries[name]), *map(str, args)],
                                capture_output=True, text=True)
        assert (result.returncode == 0) == succeeds, result.stdout + result.stderr
        assert 'Sanitizer' not in result.stderr, result.stderr
        return result.stdout

    outside = work / 'outside'
    outside.mkdir()
    sentinel = outside / 'keep'
    sentinel.write_text('must survive removal of a symlink')
    target = work / 'target'
    target.mkdir()
    (target / 'nested').mkdir()
    (target / 'nested/file').write_text('data')
    (target / 'link').symlink_to(outside, target_is_directory=True)
    (target / 'dangling').symlink_to(work / 'missing')
    assert set(run('ls', target).splitlines()) == {'nested', 'link', 'dangling'}
    assert run('ls', sentinel).strip() == str(sentinel)
    run('ls', work / 'missing', succeeds=False)
    run('rm', '-f', target, succeeds=False)
    run('rm', '-r', str(target) + '/.', succeeds=False)
    run('rm', '-r', str(target) + '/..', succeeds=False)
    assert (target / 'nested/file').read_text() == 'data'
    run('rm', '-r', target)
    assert not target.exists() and sentinel.is_file()
    run('rm', '-f', target)
    run('rm', sentinel)
    assert not sentinel.exists()
    run('rm', work / 'missing', succeeds=False)
    run('rm', '-x', succeeds=False)
print('directory listing, recursive removal, symlink and dot-path checks passed')
