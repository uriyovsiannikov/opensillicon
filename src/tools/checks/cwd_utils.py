#!/usr/bin/env python3
"""Verify real pwd/mkdir sources with deep directories, logical cwd and failures."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='opensillicon-cwd-utils-') as temporary:
    work = Path(temporary)
    sanitizer = ['-fsanitize=address,undefined'] if os.environ.get('CWD_UTILS_SANITIZE') == '1' else []
    for name in ('pwd', 'mkdir'):
        subprocess.run([*shlex.split(os.environ.get('CC', 'cc')), '-std=c11',
                        '-D_POSIX_C_SOURCE=200809L', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                        *sanitizer, f'src/user/coreutils/{name}.c', '-o', str(work / name)],
                       cwd=root, check=True)

    def run(name, args=(), cwd=work, status=0, env=None, **kwargs):
        result = subprocess.run([str(work / name), *map(str, args)], cwd=cwd,
                                env=env, capture_output=True, text=True, **kwargs)
        assert result.returncode == status, (name, args, result.stderr)
        assert 'Sanitizer' not in result.stderr, result.stderr
        return result

    env = os.environ.copy()
    env.pop('PWD', None)
    assert run('pwd', env=env).stdout == str(work) + '\n'
    deep = work
    for i in range(12):
        deep /= 'directory-' + str(i) + '-abcdefghijk'
        deep.mkdir()
    assert len(str(deep)) > 256
    assert run('pwd', cwd=deep, env=env).stdout == str(deep) + '\n'
    alias = work / 'alias'
    alias.symlink_to(deep, target_is_directory=True)
    env['PWD'] = str(alias)
    assert run('pwd', ['-L'], cwd=deep, env=env).stdout == str(alias) + '\n'
    assert run('pwd', ['-P'], cwd=deep, env=env).stdout == str(deep) + '\n'
    for invalid in (str(work), '.', str(deep) + '/.', str(deep) + '/missing/..'):
        env['PWD'] = invalid
        assert run('pwd', cwd=deep, env=env).stdout == str(deep) + '\n'
    run('pwd', ['extra'], status=2)
    run('pwd', ['-z'], status=2)
    run('mkdir', status=2)
    run('mkdir', ['-z'], status=2)
    assert not run('mkdir', ['new'], preexec_fn=lambda: os.umask(0o027)).stdout
    assert (work / 'new').stat().st_mode & 0o777 == 0o750
    result = run('mkdir', ['new', 'after-error'], status=1)
    assert not result.stdout and 'new' in result.stderr
    assert (work / 'after-error').is_dir()
    run('mkdir', ['missing/child'], status=1)
    run('mkdir', ['--', '-name'])
    assert (work / '-name').is_dir()
    run('mkdir', ['new/../sibling'])
    assert (work / 'sibling').is_dir()
    (work / 'file').touch()
    run('mkdir', ['file/../bad'], status=1)
    assert not (work / 'bad').exists()
    if Path('/dev/full').exists():
        with open('/dev/full', 'wb') as output:
            result = subprocess.run([str(work / 'pwd')], stdout=output, stderr=subprocess.PIPE)
        assert result.returncode == 1 and b'pwd: stdout' in result.stderr
    print('CWD_UTILS_PASS: long cwd, validated logical paths, umask, operands and I/O failures')
