"""PlatformIO extra script — 'combined' target.

Packs firmware.bin + littlefs.bin into a single upload image with a
12-byte header understood by the device's /update endpoint.

Header format (little-endian):
  [0..3]   magic   "ESPB" (0x45535042)
  [4..7]   fw_size uint32_t
  [8..11]  fs_size uint32_t

Usage:  pio run -t combined [-e default]
"""

Import("env")

import os
import struct
import subprocess
import sys

MAGIC = b"ESPB"
HEADER_SIZE = 12


def build_combined(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    project_dir = env.subst("$PROJECT_DIR")

    firmware = os.path.join(build_dir, "firmware.bin")
    littlefs = os.path.join(build_dir, "littlefs.bin")
    output = os.path.join(project_dir, "combined.bin")

    # build LittleFS image if missing
    if not os.path.isfile(littlefs):
        print("Building LittleFS image…")
        ret = subprocess.run(
            [sys.executable, "-m", "platformio", "run",
             "-t", "buildfs", "-e", env.subst("$PIOENV")],
            cwd=project_dir,
        )
        if ret.returncode != 0:
            print("Error: LittleFS build failed", file=sys.stderr)
            env.Exit(1)

    if not os.path.isfile(firmware):
        print(f"Error: firmware not found at {firmware} — run a build first",
              file=sys.stderr)
        env.Exit(1)

    fw_data = open(firmware, "rb").read()
    fs_data = open(littlefs, "rb").read()

    header = MAGIC + struct.pack("<II", len(fw_data), len(fs_data))
    with open(output, "wb") as f:
        f.write(header)
        f.write(fw_data)
        f.write(fs_data)

    print(f"\n=> {output}")
    print(f"   firmware  : {len(fw_data) / 1024:.0f} KB")
    print(f"   filesystem: {len(fs_data) / 1024:.0f} KB")
    print(f"   combined  : {(HEADER_SIZE + len(fw_data) + len(fs_data)) / 1024:.0f} KB")


env.AddCustomTarget(
    name="combined",
    dependencies=["$BUILD_DIR/firmware.bin"],
    actions=build_combined,
    title="Build Combined Image",
    description="Pack firmware + LittleFS into a single OTA upload image",
)
