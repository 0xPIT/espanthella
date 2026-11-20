#!/usr/bin/env python3
"""Dependency-free host regressions for the real light.cpp and light_math.h.

From the repository root:
    python3 scripts/test_light_host.py
    python3 scripts/test_light_host.py --mode 1 --case factory-reset
    python3 scripts/test_light_host.py --board xiao --mode 1
    CXX=/opt/homebrew/bin/g++-16 python3 scripts/test_light_host.py

Needs Python 3.8+ and a C++20 compiler; no PlatformIO, downloads, packages, hardware,
flashing, or changes to firmware/configuration are involved. Both hardware modes
compile src/light.cpp as its own translation unit. LIGHT_CW_PIN=6 is ONLY a host
build example, not a recommendation for connected hardware. Fakes model endpoint
callbacks, GPIO/LEDC calls and Preferences, not the ESP32/Matter implementation,
real task races, NVS, fade timing, or electrical characteristics.

GCC is preferred for the production constexpr __builtin_powf gamma table. If a
compiler cannot evaluate it at compile time (notably Clang), the runner changes
ONLY the GAMMA_LUT variable's `constexpr` to `const` in a temporary copy. The
production generator and all other implementation remain unchanged. The exact
single replacement is validated, reported, and never written back to src/.
"""

import argparse
import glob
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
TEST = ROOT / "test" / "host"


def compiler_command(explicit):
    configured = explicit or os.environ.get("CXX")
    if configured:
        command = shlex.split(configured)
        if not command or not shutil.which(command[0]):
            raise RuntimeError("C++ compiler not found: " + configured)
        return command
    # Apple /usr/bin/g++ is Clang. Prefer real versioned GCC when installed.
    candidates = []
    for directory in os.environ.get("PATH", "").split(os.pathsep):
        candidates.extend(glob.glob(str(Path(directory) / "g++-[0-9]*")))
    for directory in ("/opt/homebrew/bin", "/usr/local/bin"):
        candidates.extend(glob.glob(directory + "/g++-[0-9]*"))
    for candidate in sorted(set(candidates), reverse=True):
        if os.access(candidate, os.X_OK):
            return [candidate]
    for name in ("g++", "c++", "clang++"):
        if shutil.which(name):
            return [shutil.which(name)]
    raise RuntimeError("No host C++ compiler found. Set CXX to an existing compiler.")


def execute(command, capture=False):
    return subprocess.run(command, cwd=ROOT, text=True, capture_output=capture)


def compile_or_fail(command):
    result = execute(command, capture=True)
    if result.returncode:
        print("Compile command: " + shlex.join(command), file=sys.stderr)
        print(result.stdout + result.stderr, file=sys.stderr)
        raise RuntimeError("Host test compilation failed")
    if result.stderr:
        print(result.stderr, file=sys.stderr, end="")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--compiler", help="compiler command (otherwise CXX, versioned GCC, or system C++)")
    parser.add_argument("--mode", choices=("0", "1"), help="run one hardware mode (default: both)")
    parser.add_argument("--board", choices=("dev", "xiao"), default="dev",
                        help="dev: default C6 pins/touch; xiao: GPIO19/20, touch disabled")
    parser.add_argument("--case", help="run one named integration case (skips math); use --list to see names")
    parser.add_argument("--list", action="store_true", help="compile and list integration case names")
    args = parser.parse_args()
    compiler = compiler_command(args.compiler)
    version = execute(compiler + ["--version"], capture=True)
    if version.returncode:
        raise RuntimeError("Compiler --version failed: " + version.stderr)
    print("Compiler: " + version.stdout.splitlines()[0], flush=True)
    modes = [args.mode] if args.mode is not None else ["0", "1"]
    failures = []
    executed = 0
    with tempfile.TemporaryDirectory(prefix="espanthella-light-host-") as temporary:
        build = Path(temporary)
        flags = ["-std=c++20", "-O2", "-Wall", "-Wextra", "-Wpedantic",
                 "-I" + str(TEST / "support"), "-I" + str(ROOT / "include"), "-I" + str(TEST)]
        if args.board == "xiao":
            flags += ["-DARDUINO_XIAO_ESP32C6", "-DLIGHT_WW_PIN=D8", "-DLIGHT_CW_PIN=D9",
                      "-DLIGHT_TOUCH_ENABLED=0"]
        else:
            flags += ["-DARDUINO_ESP32C6_DEV", "-DLIGHT_CW_PIN=6"]
        # Probe the compiler's actual constexpr support, not just its name.
        probe = build / "gamma_probe.cpp"
        probe.write_text("constexpr float gammaProbe = __builtin_powf(255.0f, 1.9f);\n"
                         "int main() { return gammaProbe > 0 ? 0 : 1; }\n")
        result = execute(compiler + ["-std=c++20", str(probe), "-o", str(build / "gamma_probe")], capture=True)
        source = ROOT / "src" / "light.cpp"
        if result.returncode:
            original = source.read_text()
            target = "static constexpr auto GAMMA_LUT = makeGammaLUT<256>(BRIGHTNESS_GAMMA, PWM_MAX_DUTY);"
            replacement = target.replace("constexpr", "const", 1)
            if original.count(target) != 1:
                raise RuntimeError("Compiler needs runtime gamma, but the expected unique declaration changed; "
                                   "update the host adapter without duplicating firmware logic.")
            source = build / "light_runtime_gamma.cpp"
            source.write_text(original.replace(target, replacement, 1))
            print("Host adaptation: GAMMA_LUT constexpr -> const in a temporary source copy only.", flush=True)
        else:
            print("Compiling original src/light.cpp unchanged (constexpr gamma supported).", flush=True)

        if not args.case and not args.list:
            pairing_binary = build / "matter_pairing_test"
            compile_or_fail(compiler + flags + [str(TEST / "matter_pairing_test.cpp"), "-o", str(pairing_binary)])
            executed += 1
            if execute([str(pairing_binary)]).returncode:
                failures.append("matter_pairing")
            math_binary = build / "light_math_test"
            compile_or_fail(compiler + flags + [str(TEST / "light_math_test.cpp"), "-o", str(math_binary)])
            executed += 1
            if execute([str(math_binary)]).returncode:
                failures.append("light_math")
        matched = False
        for mode in modes:
            if not args.case and not args.list:
                # 1e6/2700 and 1e6/2701 both truncate to 370 mireds.
                invalid_configs = [
                    ("collapsed-mired-range", ["-DLIGHT_WARM_KELVIN=2700", "-DLIGHT_COOL_KELVIN=2701"]),
                    ("warm-kelvin-below-range", ["-DLIGHT_WARM_KELVIN=1999"]),
                    ("cool-kelvin-above-range", ["-DLIGHT_COOL_KELVIN=10001"]),
                    ("reversed-kelvin-range", ["-DLIGHT_WARM_KELVIN=6500", "-DLIGHT_COOL_KELVIN=2700"]),
                    ("default-kelvin-below-range", ["-DLIGHT_DEFAULT_KELVIN=2000"]),
                    ("default-kelvin-above-range", ["-DLIGHT_DEFAULT_KELVIN=7000"]),
                    ("invalid-touch-flag", ["-ULIGHT_TOUCH_ENABLED", "-DLIGHT_TOUCH_ENABLED=2"]),
                ]
                if mode == "1":
                    conflicting_pins = [("unassigned", -1), ("ww", 19 if args.board == "xiao" else 7),
                                        ("button", 9), ("usb-d-minus", 12), ("usb-d-plus", 13),
                                        ("outside-soc", 31)]
                    if args.board != "xiao":
                        conflicting_pins.append(("touch", 4))
                    for label, pin in conflicting_pins:
                        invalid_configs.append(("cw-" + label, ["-ULIGHT_CW_PIN", "-DLIGHT_CW_PIN=" + str(pin)]))
                for label, overrides in invalid_configs:
                    guard = execute(compiler + flags + ["-DLIGHT_TUNABLE_WHITE=" + mode] + overrides +
                                    ["-fsyntax-only", str(TEST / "config_compile_test.cpp")], capture=True)
                    executed += 1
                    name = "config mode=" + mode + " " + label
                    if guard.returncode and "static assertion" in guard.stderr.lower():
                        print("PASS " + name + " rejected", flush=True)
                    else:
                        failures.append(name)
                        print("FAIL " + name + " expected a config static assertion\n" +
                              guard.stdout + guard.stderr, file=sys.stderr)
            binary = build / ("light_module_" + mode)
            compile_or_fail(compiler + flags + ["-DLIGHT_TUNABLE_WHITE=" + mode,
                            str(source), str(TEST / "light_module_test.cpp"), "-o", str(binary)])
            listing = execute([str(binary), "--list"], capture=True)
            if listing.returncode:
                raise RuntimeError("Could not enumerate integration cases: " + listing.stderr)
            names = listing.stdout.splitlines()
            if args.list:
                print("mode=" + mode + ": " + ", ".join(names))
                continue
            if args.case:
                names = [name for name in names if name == args.case]
            matched = matched or bool(names)
            for name in names:
                executed += 1
                if execute([str(binary), name]).returncode:
                    failures.append("mode=" + mode + " " + name)
        if args.case and not matched:
            raise RuntimeError("No matching case for the selected mode(s): " + args.case)
    if args.list:
        return 0
    print("\nResults: " + str(executed - len(failures)) + "/" + str(executed) + " passed.", flush=True)
    for failure in failures:
        print("FAILED: " + failure, file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError) as error:
        print("ERROR: " + str(error), file=sys.stderr)
        sys.exit(2)
