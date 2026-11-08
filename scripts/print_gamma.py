"""PlatformIO extra script — 'printgamma' target.

Extracts makeGammaLUT from light.cpp, compiles a host program, and prints
the resulting table. Uses the actual project source — no mirrored logic.

Usage:  pio run -t printgamma [-e default]
"""

Import("env")

import os
import re
import subprocess
import tempfile


def print_gamma(source, target, env):
    project_dir = env.subst("$PROJECT_DIR")
    light_cpp = os.path.join(project_dir, "src", "light.cpp")

    with open(light_cpp) as f:
        src = f.read()

    # extract makeGammaLUT template (from "template" to closing "}")
    m = re.search(
        r"(template\s*<.*?>\s*\n"
        r"static\s+constexpr\s+std::array.*?makeGammaLUT\(.*?\)\s*\{.*?^\})",
        src, re.DOTALL | re.MULTILINE,
    )
    if not m:
        print("Error: could not find makeGammaLUT in light.cpp")
        env.Exit(1)

    func = m.group(1)
    # make host-compilable: drop static, constexpr → const, builtins → stdlib
    func = func.replace("static constexpr", "const")
    func = func.replace("__builtin_powf", "powf")
    func = func.replace("__builtin_roundf", "roundf")

    # extract the instantiation line
    m2 = re.search(r"(static\s+constexpr\s+auto\s+GAMMA_LUT\s*=\s*makeGammaLUT.*?;)", src)
    if not m2:
        print("Error: could not find GAMMA_LUT instantiation in light.cpp")
        env.Exit(1)

    inst = m2.group(1).replace("static constexpr", "const")

    host_src = f"""\
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <array>
#include "config.h"

{func}

{inst}

int main() {{
    printf("Gamma LUT — gamma=%.1f, steps=%zu, resolution=%u\\n\\n",
           (double)BRIGHTNESS_GAMMA, GAMMA_LUT.size(), PWM_MAX_DUTY);
    for (size_t i = 0; i < GAMMA_LUT.size(); i++) {{
        printf("%4u", GAMMA_LUT[i]);
        if (i + 1 < GAMMA_LUT.size()) printf(",");
        if ((i + 1) % 16 == 0) printf("\\n");
    }}
    return 0;
}}
"""

    with tempfile.NamedTemporaryFile(suffix=".cpp", mode="w", delete=False) as f:
        f.write(host_src)
        tmp_cpp = f.name

    tmp_bin = tmp_cpp.replace(".cpp", "")
    include_dir = os.path.join(project_dir, "include")

    try:
        ret = subprocess.run(
            ["g++", "-std=c++17", "-O2", "-I", include_dir, "-o", tmp_bin, tmp_cpp, "-lm"],
            capture_output=True, text=True,
        )
        if ret.returncode != 0:
            print("Compilation failed:")
            print(ret.stderr)
            env.Exit(1)

        ret = subprocess.run([tmp_bin], capture_output=True, text=True)
        print(ret.stdout)
    finally:
        for f in [tmp_cpp, tmp_bin]:
            if os.path.exists(f):
                os.unlink(f)


env.AddCustomTarget(
    name="printgamma",
    dependencies=None,
    actions=print_gamma,
    title="Print Gamma LUT",
    description="Compile and print the gamma correction table from light.cpp",
)
