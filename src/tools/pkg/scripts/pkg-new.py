#!/usr/bin/env python3
"""Create a starter recipe for an OpenSillicon package."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[4]
SOURCE_ROOT = ROOT / "src/tools/pkg/repo/_src"
IDENTIFIER = re.compile(r"^[A-Za-z0-9_][A-Za-z0-9_.+\-]*$")
DESCRIPTION = re.compile(r"^[A-Za-z0-9][A-Za-z0-9 _.,:+\-]*$")


def fail(message: str) -> "NoReturn":
    print(f"pkg-new: {message}", file=sys.stderr)
    raise SystemExit(2)


def main() -> None:
    if len(sys.argv) not in (3, 4):
        fail("usage: make pkg-new NAME=name VERSION=version [DESCRIPTION='text']")

    name, version = sys.argv[1:3]
    description = sys.argv[3] if len(sys.argv) == 4 and sys.argv[3] else f"{name} package"
    if not IDENTIFIER.fullmatch(name) or len(name) >= 64:
        fail("name must be a package identifier shorter than 64 characters")
    if name.startswith((".", "-")):
        fail("name cannot start with '.' or '-'")
    if not IDENTIFIER.fullmatch(version) or len(version) >= 32:
        fail("version must be a package identifier shorter than 32 characters")
    if version.startswith((".", "-")):
        fail("version cannot start with '.' or '-'")
    if len(description) >= 192 or not DESCRIPTION.fullmatch(description):
        fail("description must be plain text under 192 characters (letters, numbers, spaces, . , : + -)")

    package_dir = SOURCE_ROOT / name
    if package_dir.exists():
        fail(f"{package_dir.relative_to(ROOT)} already exists; leaving it untouched")

    makefile = f'''ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../../../../..)
NAME := {name}
VERSION := {version}
DESCRIPTION := {description}
STAGE := $(ROOT)/src/build/pkg/{name}-stage

.DEFAULT_GOAL := all
.PHONY: all clean

all:
\trm -rf "$(STAGE)"
\tmkdir -p "$(STAGE)"
\tsh "$(CURDIR)/build.sh" "$(STAGE)"
\t$(MAKE) -C "$(ROOT)" pkg-from-stage PKG_NAME="$(NAME)" \\
\t\tPKG_VERSION="$(VERSION)" PKG_STAGE="$(STAGE)" \\
\t\tPKG_DESCRIPTION="$(DESCRIPTION)"

clean:
\trm -rf "$(STAGE)"
'''
    build_script = '''#!/bin/sh
set -eu

stage=$1

# Build your program here, then install files under "$stage/usr" or
# "$stage/opt". For example:
# mkdir -p "$stage/usr/bin"
# install -m 755 path/to/program "$stage/usr/bin/program"

echo "Edit build.sh to build and stage this package." >&2
exit 1
'''
    readme = f'''# {name}

Package version: `{version}`.

Edit `build.sh` to build the program and copy its installed files under the
provided staging directory. The package manager accepts regular files under
`usr/` and `opt/`; for example, a command belongs at `usr/bin/{name}`.

Build and publish this package from the repository root:

```sh
make -C src/tools/pkg/repo/_src/{name}
make pkg-publish PKGS={name}
```
'''

    package_dir.mkdir(parents=True)
    (package_dir / "Makefile").write_text(makefile, encoding="utf-8")
    (package_dir / "build.sh").write_text(build_script, encoding="utf-8")
    (package_dir / "README.md").write_text(readme, encoding="utf-8")
    print(f"Created {package_dir.relative_to(ROOT)}")
    print("Edit build.sh, then run make pkg-publish PKGS=" + name)


if __name__ == "__main__":
    main()
