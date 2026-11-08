"""PlatformIO extra script — 'mergedfirmware' target.

Merges bootloader, partition table, firmware and LittleFS into a single
flashable binary using esptool's merge_bin.

Usage:  pio run -t mergedfirmware [-e default]
"""

Import("env")

import os
import subprocess


def merge_bin(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    project_dir = env.subst("$PROJECT_DIR")

    bootloader = os.path.join(build_dir, "bootloader.bin")
    partitions = os.path.join(build_dir, "partitions.bin")
    firmware = os.path.join(build_dir, "firmware.bin")
    littlefs = os.path.join(build_dir, "littlefs.bin")
    output = os.path.join(project_dir, "merged_flash.bin")

    # build LittleFS image if missing
    if not os.path.isfile(littlefs):
        print("Building LittleFS image…")
        ret = subprocess.run(
            ["pio", "run", "-t", "buildfs", "-e", env.subst("$PIOENV")],
            cwd=project_dir,
        )
        if ret.returncode != 0:
            print("Error: LittleFS build failed")
            env.Exit(1)

    # read flash settings from board config
    board = env.BoardConfig()
    flash_size = board.get("upload.flash_size", "16MB")
    flash_mode = board.get("build.flash_mode", "qio")
    f_flash = int(board.get("build.f_flash", "80000000L").rstrip("L"))
    flash_freq = f"{f_flash // 1000000}m"
    mcu = board.get("build.mcu", "esp32s3")

    # spiffs/littlefs partition offset from the partition table CSV
    partitions_csv = env.GetProjectOption(
        "board_build.partitions",
        board.get("build.arduino.partitions", "default_16MB.csv"),
    )
    # check project root first, then framework's partitions directory
    csv_path = None
    candidate = os.path.join(project_dir, partitions_csv)
    if os.path.isfile(candidate):
        csv_path = candidate
    else:
        for pkg in ["framework-arduinoespressif32"]:
            candidate = os.path.join(
                env.PioPlatform().get_package_dir(pkg),
                "tools", "partitions", partitions_csv,
            )
            if os.path.isfile(candidate):
                csv_path = candidate
                break

    fs_offset = None
    if csv_path:
        with open(csv_path) as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                cols = [c.strip() for c in line.split(",")]
                if len(cols) >= 4 and cols[1] == "data" and cols[2] == "spiffs":
                    fs_offset = cols[3]
                    break

    for path, label in [
        (bootloader, "bootloader"),
        (partitions, "partition table"),
        (firmware, "firmware"),
    ]:
        if not os.path.isfile(path):
            print(f"Error: {label} not found at {path} — run a build first")
            env.Exit(1)

    esptool = os.path.join(
        env.PioPlatform().get_package_dir("tool-esptool"), "esptool"
    )

    cmd = [
        esptool,
        "--chip", mcu,
        "merge-bin",
        "--flash-mode", flash_mode,
        "--flash-size", flash_size,
        "--flash-freq", flash_freq,
        "--format", "hex",
        "-o", output,
        "0x0", bootloader,
        "0x8000", partitions,
        "0x10000", firmware,
    ]

    if fs_offset and os.path.isfile(littlefs):
        cmd += [fs_offset, littlefs]
    elif not os.path.isfile(littlefs):
        print("Warning: littlefs.bin not found — merging without filesystem")

    print(f"\nMerging into {output}")
    print(f"  flash: {mcu} {flash_mode} {flash_size} @ {flash_freq}")
    result = subprocess.run(cmd)
    if result.returncode != 0:
        env.Exit(1)

    size_mb = os.path.getsize(output) / (1024 * 1024)
    print(f"\n=> {output} ({size_mb:.2f} MB)")


env.AddCustomTarget(
    name="mergedfirmware",
    dependencies=["$BUILD_DIR/firmware.bin"],
    actions=merge_bin,
    title="Merge Firmware",
    description="Merge bootloader + partitions + firmware + LittleFS into one binary",
)
