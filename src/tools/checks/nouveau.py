#!/usr/bin/env python3
"""Build/run the native Nouveau tests without a GPU or modifying boot images."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix="opensillicon-nouveau-") as tmp:
    sanitizer = (["-fsanitize=address,undefined"]
                 if os.environ.get("NOUVEAU_SANITIZE") == "1" else [])
    for name, sources in [
        ("nouveau", ["device.c", "pci.c"]),
        ("nouveau_bios", ["bios.c", "bios_shadow.c", "pll.c"]),
        ("nouveau_edid", ["edid.c", "ddc.c"]),
        ("nouveau_framebuffer", ["../../../tty/fb/framebuffer_device.c"]),
        ("nouveau_pll", ["pll.c"]),
        ("nouveau_supervisor", ["supervisor.c"]),
        ("nouveau_engine", ["engine.c", "upload.c", "vram_pool.c"]),
        ("nouveau_engine_pci", ["engine_pci.c", "engine.c", "upload.c", "vram_pool.c", "upload_pci.c", "memory.c", "channel_pci.c", "channel.c"]),
        ("nouveau_lifecycle", ["upload.c", "vram_pool.c", "channel.c"]),
        ("nouveau_upload", ["upload.c", "vram_pool.c", "vram.c"]),
        ("nouveau_channel", ["channel.c"]),
        ("nouveau_vram_pool", ["vram_pool.c"]),
        ("nouveau_vram", ["memory.c", "vram.c", "vram_pci.c"]),
        ("nouveau_memory", ["memory.c", "memory_dma.c", "display.c", "vram_pool.c"]),
        ("nouveau_display", ["display.c", "display_pci.c", "memory.c", "memory_dma.c", "vram_pool.c", "pll.c"]),
    ]:
        exe = Path(tmp) / name
        subprocess.run(shlex.split(os.environ.get("CC", "gcc")) + [
            "-std=c11", "-O1", "-g",
            "-Wall", "-Wextra", "-Werror", *sanitizer,
            "-fno-omit-frame-pointer", "-ffunction-sections", "-fdata-sections",
            "-Wl,--gc-sections", "-Isrc/include", "-Isrc/limine",
            f"src/tools/checks/{name}.c",
            *[f"src/kernel/drivers/gpu/nouveau/{s}" for s in sources],
            "-o", str(exe)
        ], cwd=root, check=True)
        subprocess.run([str(exe)], cwd=root, check=True)
