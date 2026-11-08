"""
PlatformIO pre-upload script — builds and uploads the LittleFS filesystem
image before the firmware, so HTML/CSS/JS changes deploy automatically.

For serial: runs 'pio run -t uploadfs' (builds + flashes FS via serial).
For OTA:    runs 'pio run -t buildfs' only (the OTA script sends it over HTTP).
"""
import subprocess
import sys

Import("env")


def pre_upload_fs(source, target, env):
    pioenv = env.subst("$PIOENV")
    protocol = env.GetProjectOption("upload_protocol", "esptool")

    if protocol == "custom":
        # OTA — just build the image; http_ota_upload.py sends it
        print(f"Building filesystem image for [{pioenv}]…")
        cmd = [sys.executable, "-m", "platformio", "run",
               "-t", "buildfs", "-e", pioenv]
    else:
        # Serial — build and flash via serial before firmware
        print(f"Uploading filesystem via serial for [{pioenv}]…")
        cmd = [sys.executable, "-m", "platformio", "run",
               "-t", "uploadfs", "-e", pioenv]

    result = subprocess.run(cmd, cwd=env.subst("$PROJECT_DIR"))
    if result.returncode != 0:
        print("Filesystem step failed!", file=sys.stderr)
        env.Exit(1)


env.AddPreAction("upload", pre_upload_fs)
