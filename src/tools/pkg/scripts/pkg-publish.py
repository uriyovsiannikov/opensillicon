#!/usr/bin/env python3
"""Build selected package ports and publish archives to the VDS repository."""

from pathlib import Path
import os
import re
import subprocess
import sys
import urllib.request
import uuid


ROOT = Path(__file__).resolve().parents[4]
ARCHIVES = ROOT / "src/tools/pkg/repo/_pkges"
PORTS = ROOT / "src/tools/pkg/repo/_src"
REMOTE_BASE = "/opt/opensillicon-pkg"
NAME_RE = re.compile(r"^[A-Za-z0-9_][A-Za-z0-9_.+\-]*$")


def fail(message: str) -> "NoReturn":
    print(f"pkg-publish: {message}", file=sys.stderr)
    raise SystemExit(2)


def archive_identity(path: Path) -> tuple[str, str]:
    data = path.read_bytes()
    if len(data) < 304 or data[:8] != b"OSPKG001":
        fail(f"{path.name}: invalid package header")
    total_size = int.from_bytes(data[8:12], "little")
    if total_size != len(data):
        fail(f"{path.name}: package size field does not match the file")
    name = data[16:80].split(b"\0", 1)[0].decode("ascii", errors="strict")
    version = data[80:112].split(b"\0", 1)[0].decode("ascii", errors="strict")
    if not NAME_RE.fullmatch(name) or not NAME_RE.fullmatch(version):
        fail(f"{path.name}: invalid package name or version in header")
    if path.name != f"{name}-{version}.spkg":
        fail(f"{path.name}: filename does not match its package header")
    return name, version


def ssh_prefix(key: Path) -> list[str]:
    return [
        "ssh",
        "-i",
        str(key),
        "-o",
        "BatchMode=yes",
        "-o",
        "IdentitiesOnly=yes",
        "-o",
        "StrictHostKeyChecking=yes",
        "-o",
        "ConnectTimeout=10",
    ]


def commit_program(run_id: str) -> str:
    return f'''import os
from pathlib import Path

incoming = Path("{REMOTE_BASE}/incoming/{run_id}")
repo = Path("{REMOTE_BASE}/repo/_pkges")
archives = sorted(incoming.glob("*.spkg"))
if not archives:
    raise SystemExit("no package archives to publish")

def identity(path):
    data = path.read_bytes()
    if len(data) < 304 or data[:8] != b"OSPKG001":
        raise SystemExit(f"invalid package archive: {{path.name}}")
    if int.from_bytes(data[8:12], "little") != len(data):
        raise SystemExit(f"invalid package size: {{path.name}}")
    name = data[16:80].split(b"\\0", 1)[0].decode("ascii")
    version = data[80:112].split(b"\\0", 1)[0].decode("ascii")
    if path.name != f"{{name}}-{{version}}.spkg":
        raise SystemExit(f"filename does not match archive: {{path.name}}")
    return name, version

packages = {{}}
for archive in archives:
    name, version = identity(archive)
    if name in packages:
        raise SystemExit(f"multiple versions selected for {{name}}")
    packages[name] = (archive, version)

for name, (archive, version) in packages.items():
    for old in repo.glob("*.spkg"):
        if old == archive:
            continue
        try:
            old_name, _ = identity(old)
        except (UnicodeDecodeError, ValueError):
            continue
        if old_name == name:
            old.unlink()
    os.chmod(archive, 0o644)
    destination = repo / archive.name
    os.replace(archive, destination)
    print(f"Published {{name}} {{version}}")
incoming.rmdir()
'''


def main() -> None:
    selected = os.environ.get("PKGS", "").split()
    for name in selected:
        if not NAME_RE.fullmatch(name):
            fail(f"invalid package selector: {name!r}")
        port = PORTS / name
        if not (port / "Makefile").is_file():
            fail(f"no package recipe at {port.relative_to(ROOT)}")
        subprocess.run(["make", "-C", str(port)], check=True)

    if not ARCHIVES.is_dir():
        fail(f"package archive directory not found: {ARCHIVES}")
    candidates = sorted(ARCHIVES.glob("*.spkg"))
    packages: dict[str, tuple[str, Path]] = {}
    selected_set = set(selected)
    for archive in candidates:
        name, version = archive_identity(archive)
        if selected_set and name not in selected_set:
            continue
        previous = packages.get(name)
        if previous:
            previous_version, previous_archive = previous
            previous_mtime = previous_archive.stat().st_mtime_ns
            current_mtime = archive.stat().st_mtime_ns
            if current_mtime == previous_mtime:
                fail(f"multiple local versions of {name} have the same timestamp; remove the stale .spkg archive")
            if current_mtime < previous_mtime:
                continue
        packages[name] = (version, archive)
    for name in selected_set:
        if name not in packages:
            fail(f"no built archive found for {name}")
    if not packages:
        fail("no .spkg archives found")

    host = os.environ.get("PKG_PUBLISH_HOST", "pkgpublish@48.102.169.100")
    key = Path(os.environ.get("PKG_PUBLISH_KEY", str(Path.home() / ".ssh/opensillicon_pkg_publish"))).expanduser()
    if not key.is_file():
        fail(f"SSH key not found: {key}")
    ssh = ssh_prefix(key)
    scp = ["scp", "-i", str(key), "-o", "BatchMode=yes", "-o", "IdentitiesOnly=yes",
           "-o", "StrictHostKeyChecking=yes", "-o", "ConnectTimeout=10"]
    run_id = uuid.uuid4().hex
    remote_dir = f"{REMOTE_BASE}/incoming/{run_id}"
    subprocess.run(ssh + [host, "mkdir", "-m", "2770", "--", remote_dir], check=True)
    try:
        subprocess.run(scp + [str(path) for _, path in packages.values()] + [f"{host}:{remote_dir}/"], check=True)
        subprocess.run(
            ssh + [host, "python3", "-", run_id],
            input=commit_program(run_id),
            text=True,
            check=True,
        )
    except Exception:
        subprocess.run(ssh + [host, "rm", "-rf", "--", remote_dir], check=False)
        raise

    url = os.environ.get("PKG_PUBLISH_URL", "https://packages.chipset.bond")
    try:
        with urllib.request.urlopen(url + "/index", timeout=20) as response:
            lines = response.read().decode("ascii").splitlines()
    except Exception as error:
        fail(f"published, but could not verify the repository index: {error}")
    available = set()
    for line in lines[1:]:
        fields = line.split("\t")
        if len(fields) >= 2:
            available.add((fields[0], fields[1]))
    missing = [(name, version) for name, (version, _) in packages.items() if (name, version) not in available]
    if missing:
        fail("published, but the index is missing: " + ", ".join(f"{name} {version}" for name, version in missing))
    print(f"Repository updated: {len(packages)} package(s) at {url}")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.returncode or 1)
