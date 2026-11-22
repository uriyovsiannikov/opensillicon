#!/usr/bin/env python3
"""Check package parsing and install/remove with more than 128 files on the host."""
from pathlib import Path
import os
import functools
import hashlib
import http.server
import ssl
import threading
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
subprocess.run(['make', '-j4', 'src/build/pkg/pkg-host', 'src/build/pkg/pkg-build'],
               cwd=root, check=True)
cc = shlex.split(os.environ.get('CC', 'cc'))
sanitize = os.environ.get('PKG_SANITIZE') == '1'
flags = ['-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
         '-D_POSIX_C_SOURCE=200809L', '-DPKG_HOST', '-Isrc/user/pkg',
         '-fno-omit-frame-pointer']
if sanitize:
    flags += ['-fsanitize=address,undefined']
common = ['src/user/pkg/' + f + '.c' for f in ['format', 'sha256', 'io']]
with tempfile.TemporaryDirectory(prefix='opensillicon-pkg-') as directory:
    tmp = Path(directory)
    probe = tmp / 'archive-check'
    subprocess.run([*cc, *flags, 'src/tools/checks/pkg_archive.c',
                    *common, '-o', str(probe)], cwd=root, check=True)
    subprocess.run([str(probe)], check=True)
    client = tmp / 'pkg'
    subprocess.run([*cc, *flags,
                    '-Isrc/user/thirdparty/mbedtls/include',
                    '-Isrc/user/thirdparty/mbedtls/opensillicon',
                    '-DMBEDTLS_CONFIG_FILE="opensillicon_mbedtls_config.h"',
                    *['src/user/pkg/' + f + '.c' for f in ['pkg', 'http', 'tls']],
                    *common,
                    *['src/build/mbedtls-host/lib' + f + '.a'
                      for f in ['mbedtls', 'mbedx509', 'mbedcrypto']],
                    '-o', str(client)], cwd=root, check=True)
    stage = tmp / 'stage'
    data = stage / 'usr/share/table-check'
    data.mkdir(parents=True)
    for i in range(200):
        (data / f'file-{i:04}').write_text(f'package file {i}\n')
    archive = tmp / 'table-check-1.0.spkg'
    subprocess.run([str(root / 'src/build/pkg/pkg-build'), 'table-check', '1.0',
                    str(stage), str(archive)], check=True)
    dest = tmp / 'root'
    dest.mkdir()

    environment = dict(os.environ)

    def pkg(*args, succeeds=True):
        result = subprocess.run([str(client), '-r', str(dest), *map(str, args)],
                                capture_output=True, text=True, env=environment)
        if (result.returncode == 0) != succeeds or 'Sanitizer' in result.stderr:
            raise AssertionError(result.stdout + result.stderr)
        return result.stdout

    installed = dest / 'usr/share/table-check'
    installed.mkdir(parents=True)
    (installed / 'file-0000').write_text('existing file')
    pkg('add', archive, succeeds=False)
    assert not (dest / 'var/db/pkg/table-check.db').exists()
    (installed / 'file-0000').unlink()
    pkg('add', archive)
    assert 'table-check 1.0' in pkg('list', '-i')
    pkg('add', archive, succeeds=False)
    for source in data.iterdir():
        assert (installed / source.name).read_bytes() == source.read_bytes()
    changed = installed / 'file-0000'
    changed.write_text('modified after installation')
    pkg('remove', 'table-check', succeeds=False)
    assert (dest / 'var/db/pkg/table-check.db').is_file()
    changed.write_bytes((data / changed.name).read_bytes())
    # Exercise both simultaneously loaded manifests and every upgrade exit.
    repo = tmp / 'repo'
    (repo / 'packages').mkdir(parents=True)
    updated = repo / 'packages/table-check-2.0.spkg'
    (data / 'file-0000').write_text('updated package file\n')
    subprocess.run([str(root / 'src/build/pkg/pkg-build'), 'table-check', '2.0',
                    str(stage), str(updated)], check=True)
    payload = updated.read_bytes()
    digest = hashlib.sha256(payload).hexdigest()
    (repo / 'index').write_text('OSPKG-INDEX-1\n'
        + f'table-check\t2.0\t{len(payload)}\t{digest}\t{updated.name}\tTable check\n')
    certificate, key = tmp / 'cert.pem', tmp / 'key.pem'
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
                    '-days', '1', '-subj', '/CN=localhost',
                    '-addext', 'subjectAltName=IP:127.0.0.1',
                    '-keyout', str(key), '-out', str(certificate)],
                   stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, check=True)
    environment['SSL_CERT_FILE'] = str(certificate)

    class Handler(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *args):
            pass

    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0),
        functools.partial(Handler, directory=str(repo)))
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(certificate, key)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        pkg('repo', f'https://127.0.0.1:{server.server_port}')
        changed.write_text('changed before upgrade')
        pkg('upgrade', 'table-check', succeeds=False)
        assert 'table-check 1.0' in pkg('list', '-i')
        changed.write_text('package file 0\n')
        pkg('upgrade', 'table-check')
        assert changed.read_text() == 'updated package file\n'
        assert 'table-check 2.0' in pkg('list', '-i')
        assert 'already current' in pkg('upgrade', 'table-check')
    finally:
        server.shutdown()
        server.server_close()
        worker.join(timeout=5)
    pkg('remove', 'table-check')
    assert not list(installed.iterdir())
    assert not (dest / 'var/db/pkg/table-check.db').exists()
    assert 'Installed packages (0)' in pkg('list', '-i')
print('package install/upgrade/remove and rejection paths passed'
      + (' under ASan/UBSan' if sanitize else ''))
