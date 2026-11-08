"""
PlatformIO custom upload script — packs firmware + LittleFS into a single
combined image and sends it to the device over HTTP POST.

Combined format (little-endian):
  [0..3]   magic   "ESPB"
  [4..7]   fw_size uint32_t
  [8..11]  fs_size uint32_t
  [12..]   firmware bytes, then filesystem bytes
"""
import os
import struct
import subprocess
import sys

Import("env")

MAGIC = b"ESPB"


def upload_http_ota(source, target, env):
    firmware_path = str(source[0])
    upload_port = env.GetProjectOption("upload_port")
    build_dir = env.subst("$BUILD_DIR")
    fs_image = os.path.join(build_dir, "littlefs.bin")
    combined = os.path.join(build_dir, "combined.bin")

    fw_data = open(firmware_path, "rb").read()
    fs_data = open(fs_image, "rb").read() if os.path.exists(fs_image) else b""

    header = MAGIC + struct.pack("<II", len(fw_data), len(fs_data))
    with open(combined, "wb") as f:
        f.write(header)
        f.write(fw_data)
        f.write(fs_data)

    total_kb = os.path.getsize(combined) / 1024
    url = f"http://{upload_port}/update"
    print(f"HTTP OTA: uploading combined image ({total_kb:.0f} KB) → {url}")
    print(f"  firmware  : {len(fw_data) / 1024:.0f} KB")
    if fs_data:
        print(f"  filesystem: {len(fs_data) / 1024:.0f} KB")

    result = subprocess.run(
        ["curl", "--progress-bar", "-f",
         "-F", f"firmware=@{combined}", url],
        capture_output=False,
    )
    if result.returncode != 0:
        print("HTTP OTA: upload failed!", file=sys.stderr)
        sys.exit(1)
    print("\nHTTP OTA: success — device is rebooting")


env.Replace(UPLOADCMD=upload_http_ota)
