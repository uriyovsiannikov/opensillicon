"""Verify native TLS, certificate rejection and NetSurf HTTPS in a disposable Live VM."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import socket
import ssl
import subprocess
import threading
import time

BUILD = Path("src/build/tls-guest").resolve()
BUILD.mkdir(parents=True, exist_ok=True)
SOURCE = Path("src/user/thirdparty/mbedtls").resolve()
PORT = SOURCE / "opensillicon"


def run(*args):
    result = subprocess.run(list(map(str, args)), stdout=subprocess.DEVNULL,
                            stderr=subprocess.PIPE)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.decode(errors="replace"))


def fixture_certificates(subject_alt_name="IP:10.15.3.2"):
    (BUILD / "index").write_text("")
    (BUILD / "index.attr").write_text("unique_subject = no\n")
    (BUILD / "serial").write_text("01\n")
    config = BUILD / "ca.conf"
    config.write_text(f"""[ca]
default_ca=issuer
[issuer]
database={BUILD}/index
serial={BUILD}/serial
new_certs_dir={BUILD}
certificate={BUILD}/ca.pem
private_key={BUILD}/ca.key
default_md=sha256
policy=identity
[identity]
commonName=supplied
[server]
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth
subjectAltName={subject_alt_name}
[wrong_name]
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth
subjectAltName=DNS:wrong.example
""")
    for name in ("ca", "untrusted"):
        run("openssl", "req", "-new", "-x509", "-newkey", "rsa:2048", "-nodes",
            "-days", "3650", "-subj", f"/CN=OpenSillicon {name} test CA",
            "-addext", "basicConstraints=critical,CA:TRUE",
            "-keyout", BUILD / f"{name}.key", "-out", BUILD / f"{name}.pem")
    run("openssl", "req", "-new", "-newkey", "rsa:2048", "-nodes",
        "-subj", "/CN=10.15.3.2", "-keyout", BUILD / "server.key",
        "-out", BUILD / "server.csr")
    for name, extension, end in (("valid", "server", "20400101000000Z"),
                                  ("wrong", "wrong_name", "20400101000000Z"),
                                  ("expired", "server", "20200101000000Z")):
        run("openssl", "ca", "-batch", "-notext", "-config", config,
            "-in", BUILD / "server.csr", "-out", BUILD / f"{name}.pem",
            "-extensions", extension, "-startdate", "20190101000000Z",
            "-enddate", end)
    run("openssl", "x509", "-req", "-in", BUILD / "server.csr",
        "-CA", BUILD / "untrusted.pem", "-CAkey", BUILD / "untrusted.key",
        "-set_serial", "99", "-days", "3650", "-extfile", config,
        "-extensions", "server", "-out", BUILD / "unknown.pem")
    ca = (BUILD / "ca.pem").read_bytes()
    (BUILD / "tls_test_ca.h").write_text(
        "static const unsigned char test_ca[] = {" +
        ",".join(map(str, ca + b"\0")) + "};\n")


def build_probe():
    stage = BUILD / "stage"
    (stage / "usr/bin").mkdir(parents=True, exist_ok=True)
    compiler = os.environ.get("CC", "gcc")
    includes = subprocess.check_output([compiler, "-print-file-name=include"], text=True).strip()
    run(compiler, "-std=c11", "-O2", "-ffreestanding", "-fno-builtin",
        "-fno-stack-protector", "-fno-pic", "-fno-pie", "-m64", "-mno-red-zone",
        "-nostdinc", "-isystem", includes,
        "-Isrc/user/libos/include", f"-I{SOURCE}/include",
        f"-I{PORT}", f"-I{BUILD}",
        '-DMBEDTLS_CONFIG_FILE="opensillicon_mbedtls_config.h"',
        "-c", "src/tests/tls_guest.c", "-o", BUILD / "guest.o")
    run(compiler, "-nostdlib", "-static", "-no-pie", "-Wl,--build-id=none",
        "-Wl,-T,src/user/c_linker.ld", "src/build/libos/crt0.o", BUILD / "guest.o",
        "-Wl,--start-group", "src/build/mbedtls/libmbedtls.a",
        "src/build/mbedtls/libmbedx509.a", "src/build/mbedtls/libmbedcrypto.a",
        "src/build/libos/liblibos.a", "-Wl,--end-group", "-lgcc",
        "-o", stage / "usr/bin/tlscheck")
    run(compiler, "-std=c11", "-O2", "-ffreestanding", "-fno-builtin",
        "-fno-stack-protector", "-fno-pic", "-fno-pie", "-m64", "-mno-red-zone",
        "-nostdinc", "-isystem", includes, "-Isrc/user/libos/include",
        "-Isrc/user/thirdparty/xorg/deps/libxcb/include",
        "-Isrc/build/netsurf/prefix/include", "-Isrc/build/x11/prefix/include",
        "-nostdlib", "-static", "-no-pie", "-Wl,--build-id=none",
        "-Wl,-T,src/user/c_linker.ld", "src/build/libos/crt0.o",
        "src/tests/netsurf_guest.c", "-Wl,--start-group",
        "src/build/netsurf/prefix/lib/libxcb-keysyms.a", "src/build/x11/prefix/lib/libxcb.a",
        "src/build/x11/prefix/lib/libXau.a", "src/build/libos/liblibos.a",
        "-Wl,--end-group", "-lgcc", "-o", stage / "usr/bin/browsercheck")
    (BUILD / "repo/_pkges").mkdir(parents=True, exist_ok=True)
    run("src/build/pkg/pkg-build", "tlscheck", "1.0", stage,
        BUILD / "repo/_pkges/tlscheck-1.0.spkg", "Native TLS regression")


requests = []


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        requests.append((self.server.server_port, self.path))
        body = b"""<!doctype html><html><head><title>OpenSillicon HTTPS</title></head>
<body><h1>OpenSillicon HTTPS works</h1><p>NetSurf loaded this page over TLS 1.2.</p>
<p>The certificate chain and server name were verified.</p></body></html>"""
        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_):
        pass


def main():
    run("make", "-j8", "live-iso", "src/build/pkg/pkg-server", "src/build/pkg/pkg-build")
    fixture_certificates()
    build_probe()
    servers = []
    vm = repository = None
    for port, name in ((18443, "valid"), (18444, "wrong"),
                       (18445, "expired"), (18446, "unknown")):
        server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = context.maximum_version = ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(BUILD / f"{name}.pem", BUILD / "server.key")
        server.socket = context.wrap_socket(server.socket, server_side=True)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        servers.append(server)
    serial, qmp = BUILD / "serial.log", BUILD / "qmp"
    serial.write_bytes(b"")
    qmp.unlink(missing_ok=True)
    try:
        repository = subprocess.Popen(["src/build/pkg/pkg-server", "--repo", str(BUILD / "repo"),
                                       "--bind", "127.0.0.1", "--port", "18089"],
                                      stdout=subprocess.DEVNULL)
        vm = subprocess.Popen([
            "qemu-system-x86_64", "-accel", os.environ.get("QEMU_ACCEL", "kvm"),
            "-cpu", os.environ.get("QEMU_CPU", "host"), "-machine", "q35",
            "-m", "1024M", "-smp", "2", "-bios", "/usr/share/OVMF/OVMF_CODE.fd",
            "-cdrom", "src/build/opensillicon-live.iso", "-display", "none",
            "-serial", f"file:{serial}", "-qmp", f"unix:{qmp},server=on,wait=off",
            "-netdev", "user,id=n,net=10.15.3.0/24,host=10.15.3.2,dns=10.15.3.3",
            "-object", f"filter-dump,id=capture,netdev=n,file={BUILD / 'network.pcap'}",
            "-device", "e1000,netdev=n"], stderr=subprocess.PIPE)
        deadline = time.monotonic() + 5
        while not qmp.exists():
            assert vm.poll() is None, vm.stderr.read().decode()
            if time.monotonic() > deadline:
                raise TimeoutError("QMP socket")
            time.sleep(.05)
        connection = socket.socket(socket.AF_UNIX)
        connection.connect(str(qmp))
        channel = connection.makefile("rwb")
        channel.readline()

        def control(name, args=None):
            channel.write(json.dumps({"execute": name, "arguments": args or {}}).encode() + b"\n")
            channel.flush()
            while True:
                result = json.loads(channel.readline())
                if "return" in result:
                    return result["return"]
                assert "error" not in result, result

        def wait(marker, timeout=45):
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                text = serial.read_text(errors="replace")
                if "TLS NATIVE FAIL" in text or "NETSURF XCB FAIL" in text:
                    raise AssertionError(text[-3000:])
                if marker in text:
                    return
                assert vm.poll() is None, vm.stderr.read().decode()
                time.sleep(.1)
            raise TimeoutError(marker + ": " + text[-3000:])

        def type_command(text):
            keys = {" ": "spc", "/": "slash", "\n": "ret", ".": "dot",
                    "-": "minus", ":": "shift-semicolon", "&": "shift-7"}
            for char in text + "\n":
                control("human-monitor-command", {"command-line": "sendkey " + keys.get(char, char) + " 18"})
                time.sleep(.06)

        control("qmp_capabilities")
        wait("/% ")
        type_command("doas pkg repo http://10.15.3.2:18089")
        wait("Repository saved in /etc/pkg.conf")
        type_command("doas pkg update")
        wait("Repository updated: 1 packages")
        type_command("doas pkg add tlscheck")
        wait("Installed tlscheck")
        type_command("tlscheck")
        wait("TLS NATIVE PASS")
        print("Native HTTPS and certificate rejection checks passed", flush=True)
        dns_name = os.environ.get("TLS_DNS_NAME")
        if dns_name:
            type_command("ifconfig")
            wait(" dns ")
            type_command("host " + dns_name)
            wait(dns_name + " has address ")
            print("Native DNS resolved", dns_name, flush=True)
        native_count = requests.count((18443, "/"))
        browser_url = os.environ.get("TLS_BROWSER_URL", "https://10.15.3.2:18443/")
        test_input = os.environ.get("NETSURF_TEST_INPUT") == "1"
        if test_input:
            type_command("browsercheck &")
        type_command("tlscheck --browser " + browser_url + " &")
        type_command("startx")
        wait("startx: TinyX, twm and xterm launched")
        profile_seconds = int(os.environ.get("TLS_PROFILE_SECONDS", "0"))
        if profile_seconds > 0:
            samples = []
            started = time.monotonic()
            while time.monotonic() - started < profile_seconds:
                registers = control("human-monitor-command",
                                    {"command-line": "info registers -a"})
                samples.append([time.monotonic() - started, registers])
                time.sleep(.04)
            (BUILD / "profile.json").write_text(json.dumps(samples))
            print("Saved QEMU CPU samples to", BUILD / "profile.json", flush=True)
        else:
            time.sleep(2)
        if browser_url == "https://10.15.3.2:18443/":
            deadline = time.monotonic() + 30
            while requests.count((18443, "/")) == native_count and time.monotonic() < deadline:
                time.sleep(.2)
            assert requests.count((18443, "/")) > native_count, "NetSurf did not request the HTTPS page"
            print("NetSurf requested the trusted HTTPS page", flush=True)
        if test_input:
            wait("NetSurf XCB window ready")
            wait("NetSurf XCB keyboard request submitted")
            deadline = time.monotonic() + 20
            while (18443, "/keyboard") not in requests and time.monotonic() < deadline:
                time.sleep(.1)
            assert (18443, "/keyboard") in requests, "NetSurf did not navigate from the URL widget"
            print("NetSurf mouse focus and URL keyboard navigation passed", flush=True)
        time.sleep(2)
        control("screendump", {"filename": str(BUILD / "netsurf.ppm")})
        print("NetSurf screenshot at", BUILD / "netsurf.ppm", flush=True)
        if test_input:
            wait("NetSurf XCB WM_DELETE passed")
            print("NetSurf WM_DELETE_WINDOW passed", flush=True)
        channel.close()
        connection.close()
    finally:
        for process in (vm, repository):
            if process and process.poll() is None:
                process.terminate()
                process.wait(timeout=5)
        for server in servers:
            server.shutdown()
            server.server_close()


if __name__ == "__main__":
    main()
