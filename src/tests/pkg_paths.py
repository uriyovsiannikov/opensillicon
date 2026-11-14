"""Check system package paths, removal and the four-command interface."""
from pathlib import Path
import struct
import subprocess
import tempfile

CLIENT = Path("src/build/pkg/pkg-host").resolve()
BUILDER = Path("src/build/pkg/pkg-build").resolve()


def run(*arguments, success=True):
    result = subprocess.run(arguments, capture_output=True, text=True)
    assert (result.returncode == 0) == success, result.stdout + result.stderr
    return result.stdout + result.stderr


with tempfile.TemporaryDirectory(prefix="pkg-paths-") as directory:
    work = Path(directory)
    root = work / "root"
    root.mkdir()
    source = work / "stage"
    (source / "usr/bin").mkdir(parents=True)
    payload = b"hello package\n"
    program = source / "usr/bin/hello"
    program.write_bytes(payload)
    program.chmod(0o755)
    archive = work / "hello.spkg"
    run(str(BUILDER), "hello", "1.0", str(source), str(archive), "Test program")
    record = root / "var/db/pkg/hello.db"
    destination = root / "usr/bin/hello"

    def pkg(*arguments, success=True):
        return run(str(CLIENT), "-r", str(root), *map(str, arguments), success=success)

    pkg("repo", "http://127.0.0.1:8080")
    assert (root / "etc/pkg.conf").read_text().strip() == "http://127.0.0.1:8080"
    pkg("add", archive)
    assert destination.read_bytes() == payload
    assert destination.stat().st_mode & 0o7777 == 0o755
    assert not (root / "bin/hello").exists()
    installed = record.read_bytes()
    assert installed[304:496].split(b"\0", 1)[0] == b"usr/bin/hello"

    destination.write_bytes(b"local change")
    pkg("remove", "hello", success=False)
    assert destination.read_bytes() == b"local change"
    destination.write_bytes(payload)
    pkg("remove", "hello")
    assert not destination.exists() and not record.exists()

    # Incomplete writes can be cleaned up with the same remove command.
    incomplete = bytearray(installed)
    struct.pack_into("<I", incomplete, 8, 0)
    record.write_bytes(incomplete)
    destination.write_bytes(payload[:3])
    destination.chmod(0o600)
    pkg("remove", "hello")
    assert not record.exists() and not destination.exists()

    # Removal can be retried after an interrupted operation.
    pending = bytearray(installed)
    struct.pack_into("<I", pending, 8, 2)
    record.write_bytes(pending)
    pkg("remove", "hello")
    assert not record.exists()

    # Old records can still remove their original bin/ files.
    old = bytearray(installed)
    old[304:496] = b"bin/hello".ljust(192, b"\0")
    record.write_bytes(old)
    (root / "bin").mkdir()
    legacy = root / "bin/hello"
    legacy.write_bytes(payload)
    legacy.chmod(0o755)
    pkg("remove", "hello")
    assert not legacy.exists()

    # Reject old archive destinations instead of rewriting their paths.
    (source / "bin").mkdir()
    (source / "bin/hello").write_bytes(payload)
    run(str(BUILDER), "hello", "1.0", str(source), str(archive),
        "Test program", success=False)

    for removed in ("list", "info", "verify", "delete", "recover", "install", "search"):
        pkg(removed, "hello", success=False)
    pkg(success=False)

print("System package paths, removal and command interface passed")
