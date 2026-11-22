#!/usr/bin/env python3
"""Check real kill parsing and signaling with disposable host children."""
from pathlib import Path
import os
import shlex
import signal
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='opensillicon-kill-') as directory:
    executable = Path(directory) / 'kill'
    sanitizer = ['-fsanitize=address,undefined'] if os.environ.get('KILL_SANITIZE') == '1' else []
    subprocess.run([*shlex.split(os.environ.get('CC', 'cc')), '-std=c11', '-D_GNU_SOURCE',
                    '-O1', '-g', '-Wall', '-Wextra', '-Werror', *sanitizer,
                    'src/user/coreutils/kill.c', '-o', str(executable)], cwd=root, check=True)

    def run(args=(), status=0):
        result = subprocess.run([str(executable), *map(str, args)], capture_output=True, text=True)
        assert result.returncode == status, (args, result.returncode, result.stderr)
        assert not result.stdout and 'Sanitizer' not in result.stderr, result.stderr
        return result

    for name, number in ((None, signal.SIGTERM), ('TERM', signal.SIGTERM),
                         ('SIGTERM', signal.SIGTERM), ('INT', signal.SIGINT),
                         ('SIGINT', signal.SIGINT), ('KILL', signal.SIGKILL),
                         ('SIGKILL', signal.SIGKILL), ('15', signal.SIGTERM)):
        child = subprocess.Popen(['/bin/sleep', '30'])
        try:
            run(['-s', '0', child.pid])
            assert child.poll() is None
            run(([child.pid] if name is None else ['-s', name, child.pid]))
            assert child.wait(timeout=5) == -number
        finally:
            if child.poll() is None:
                child.kill()
                child.wait()
    for pid in ('0', '2147483648', '18446744073709551615', '1x', '', '+1'):
        result = run(['-s', '0', '--', pid], status=1)
        assert 'invalid pid' in result.stderr
    for signo in ('bad', '-1', '999999999999999999999999', '1x', ''):
        run(['-s', signo, os.getpid()], status=2)
    run(status=2)
    run(['-s'], status=2)
    run(['-z'], status=2)
    # Invalid operands must not prevent checking later valid operands.
    result = run(['-s', '0', 'bad', os.getpid()], status=1)
    assert result.stderr.count('kill:') == 1
    print('KILL_PASS: names/numbers, default signal, existence checks, pid bounds and mixed operands')
