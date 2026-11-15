"""Package HTTPS integration: real repository, invalid certificates and truncation.

Add --qemu to exercise the installed native pkg in an isolated Live image.
The fixture CA is never added to the normal root image or system trust store.
"""
from contextlib import contextmanager
from pathlib import Path
import json
import os
import re
import shutil
import socket
import ssl
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import tls_qemu

ROOT = Path.cwd()
CLIENT = ROOT / "src/build/pkg/pkg-host"
SERVER = ROOT / "src/build/pkg/pkg-server"
BUILDER = ROOT / "src/build/pkg/pkg-build"


def run(*args, success=True, env=None):
    result = subprocess.run(list(map(str, args)), capture_output=True, text=True,
                            timeout=45, env=env)
    assert (result.returncode == 0) == success, result.stdout + result.stderr
    return result.stdout + result.stderr


@contextmanager
def repository(work, certificate, secure=True):
    args = [str(SERVER), "--repo", str(work / "repo"), "--bind", "127.0.0.1", "--port", "0"]
    if secure:
        args += ["--cert", str(work / ("fullchain.pem" if certificate == "valid" else f"{certificate}.pem")), "--key", str(work / "server.key")]
    with (work / f"server-{certificate}.log").open("w") as errors:
        process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=errors, text=True)
        try:
            line = process.stdout.readline()
            match = re.search(r":(\d+) repo=", line)
            assert match, line
            yield int(match[1])
        finally:
            process.terminate()
            process.wait(timeout=35)


class Truncated(BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Length", "100")
        self.end_headers()
        self.wfile.write(b"SPKI")
    def log_message(self, *args):
        pass


def host_tests(work):
    root = work / "host-root"
    root.mkdir()
    env = os.environ.copy()
    env["SSL_CERT_FILE"] = str(work / "ca.pem")
    def pkg(*args, success=True):
        return run(CLIENT, "-r", root, *args, success=success, env=env)
    for malformed in ("https://", "https://host:0", "https://host:65536",
                      "https://host/path", "https://user@host", "https://host\r\nX:yes",
                      "https://host?query", "ftp://host"):
        pkg("repo", malformed, success=False)
    for port_default in ("https://localhost", "https://localhost/", "http://localhost"):
        pkg("repo", port_default)
    with repository(work, "valid") as port:
        pkg("repo", f"https://localhost:{port}")
        pkg("update")
        pkg("repo", f"https://127.0.0.1:{port}")
        pkg("update")
        pkg("add", "hello")
        assert (root / "usr/bin/hello").read_bytes() == (work / "stage/usr/bin/hello").read_bytes()
        pkg("remove", "hello")
        assert not (root / "usr/bin/hello").exists()
        index = (root / "var/cache/pkg/index").read_bytes()
    for certificate in ("wrong", "expired", "unknown"):
        with repository(work, certificate) as port:
            pkg("repo", f"https://127.0.0.1:{port}")
            message = pkg("update", success=False)
            assert "TLS" in message, message
            assert (root / "var/cache/pkg/index").read_bytes() == index
    # Explicit HTTP is supported, but HTTPS must never retry it in plaintext.
    with repository(work, "plaintext", secure=False) as port:
        pkg("repo", f"https://127.0.0.1:{port}")
        pkg("update", success=False)
        assert (root / "var/cache/pkg/index").read_bytes() == index
        pkg("repo", f"http://127.0.0.1:{port}")
        pkg("update")
    server = ThreadingHTTPServer(("127.0.0.1", 0), Truncated)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(work / "valid.pem", work / "server.key")
    server.socket = context.wrap_socket(server.socket, server_side=True)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        pkg("repo", f"https://127.0.0.1:{server.server_port}")
        pkg("update", success=False)
        assert (root / "var/cache/pkg/index").read_bytes() == index
    finally:
        server.shutdown()
        server.server_close()
    print("Host HTTPS: update/add/remove, hostname/expiry/issuer rejection, no downgrade and truncation passed", flush=True)


def qemu_test(work):
    # Own image and ISO: private CA and test output never enter release images.
    image = work / "root.img"
    log = work / "build.log"
    with log.open("w") as out:
        subprocess.run(["make", "-j8", f"ROOT_IMAGE={image}", f"LIVE_ROOT_IMAGE={image}",
                        f"PKG_CA_CERT={work / 'ca.pem'}", str(image)],
                       stdout=out, stderr=subprocess.STDOUT, check=True)
    tree = work / "iso"
    (tree / "EFI/BOOT").mkdir(parents=True)
    (tree / "boot").mkdir()
    shutil.copyfile("src/limine/BOOTX64.EFI", tree / "EFI/BOOT/BOOTX64.EFI")
    shutil.copyfile("src/limine/limine-uefi-cd.bin", tree / "boot/limine-uefi-cd.bin")
    shutil.copyfile("src/build/kernel.elf", tree / "boot/kernel.elf")
    shutil.copyfile("src/build/initramfs.cpio", tree / "boot/initramfs.cpio")
    shutil.copyfile(image, tree / "boot/root.img")
    (tree / "limine.conf").write_text("""timeout: 0
/OpenSillicon HTTPS fixture
    protocol: limine
    path: boot():/boot/kernel.elf
    module_path: boot():/boot/initramfs.cpio
    module_string: initramfs
    module_path: boot():/boot/root.img
    module_string: live-root
    resolution: 1024x768x32
""")
    iso = work / "test.iso"
    with log.open("a") as out:
        subprocess.run(["xorriso", "-as", "mkisofs", "-R", "-r", "-J", "--efi-boot",
                        "boot/limine-uefi-cd.bin", "-efi-boot-part", "--efi-boot-image",
                        "--protective-msdos-label", str(tree), "-o", str(iso)],
                       stdout=out, stderr=subprocess.STDOUT, check=True)
    serial, qmp = work / "serial.log", work / "qmp"
    process = subprocess.Popen(["qemu-system-x86_64", "-accel", os.environ.get("QEMU_ACCEL", "tcg"),
        "-cpu", os.environ.get("QEMU_CPU", "max"), "-machine", "q35", "-m", "1024M", "-smp", "2",
        "-bios", "/usr/share/OVMF/OVMF_CODE.fd", "-cdrom", str(iso), "-display", "none",
        "-serial", f"file:{serial}", "-qmp", f"unix:{qmp},server=on,wait=off",
        "-netdev", "user,id=n,net=10.15.3.0/24,host=10.15.3.2,dns=10.15.3.3",
        "-device", "e1000,netdev=n"], stderr=subprocess.PIPE)
    connection = None
    try:
        deadline = time.monotonic() + 10
        while not qmp.exists():
            assert process.poll() is None
            assert time.monotonic() < deadline
            time.sleep(.1)
        connection = socket.socket(socket.AF_UNIX)
        connection.connect(str(qmp))
        channel = connection.makefile("rwb", buffering=0)
        channel.readline()
        def control(command, arguments=None):
            channel.write(json.dumps({"execute": command, "arguments": arguments or {}}).encode() + b"\n")
            while True:
                reply = json.loads(channel.readline())
                if "return" in reply: return reply["return"]
                assert "error" not in reply, reply
        control("qmp_capabilities")
        def wait(marker, start=0):
            deadline = time.monotonic() + 90
            while time.monotonic() < deadline:
                text = serial.read_text(errors="replace") if serial.exists() else ""
                if marker in text[start:]: return len(text)
                assert process.poll() is None, text[-2000:]
                time.sleep(.1)
            raise AssertionError(marker + "\n" + text[-3000:])
        def command(text):
            start = serial.stat().st_size
            keys = {" ": "spc", "/": "slash", ".": "dot", "-": "minus", ":": "shift-semicolon"}
            for char in text + "\n":
                control("human-monitor-command", {"command-line": "sendkey " + keys.get(char, "ret" if char == "\n" else char) + " 18"})
                time.sleep(.06)
            return start
        wait("/% ")
        with repository(work, "valid") as port:
            wait("Repository saved", command(f"doas pkg repo https://10.15.3.2:{port}"))
            wait("Repository updated: 1 packages", command("doas pkg update"))
            wait("Installed hello", command("doas pkg add hello"))
            wait("Hello from an OpenSillicon package!", command("hello"))
            wait("Removed hello", command("doas pkg remove hello"))
        for cert in ("wrong", "expired", "unknown"):
            with repository(work, cert) as port:
                wait("Repository saved", command(f"doas pkg repo https://10.15.3.2:{port}"))
                wait("repository request failed", command("doas pkg update"))
        print("Native QEMU pkg: HTTPS update/add/run/remove and invalid certificate rejection passed", flush=True)
    finally:
        if connection: connection.close()
        process.terminate()
        process.wait(timeout=5)


def main():
    subprocess.run(["make", "-j8", "src/build/pkg/pkg-host", "src/build/pkg/pkg-server",
                    "src/build/pkg/pkg-build", "src/tools/pkg/repo/_pkges/hello-1.0.0.spkg"], check=True)
    # Preserve guest logs and certificates under ignored build output for diagnosis.
    work = ROOT / "src/build/pkg-https-test"
    work.mkdir(parents=True, exist_ok=True)
    for name in ("host-root", "repo", "stage", "iso"):
        if (work / name).exists(): shutil.rmtree(work / name)
    (work / "qmp").unlink(missing_ok=True)
    tls_qemu.BUILD = work
    tls_qemu.fixture_certificates("IP:10.15.3.2,IP:127.0.0.1,DNS:localhost")
    (work / "fullchain.pem").write_bytes((work / "valid.pem").read_bytes() + (work / "ca.pem").read_bytes())
    stage = work / "stage/usr/bin"
    stage.mkdir(parents=True)
    shutil.copyfile("src/build/pkg/hello-system-stage/usr/bin/hello", stage / "hello")
    (stage / "hello").chmod(0o755)
    (work / "repo/_pkges").mkdir(parents=True)
    run(BUILDER, "hello", "1.0.0", work / "stage", work / "repo/_pkges/hello-1.0.0.spkg", "HTTPS test")
    host_tests(work)
    if "--qemu" in sys.argv:
        qemu_test(work)


if __name__ == "__main__":
    main()
