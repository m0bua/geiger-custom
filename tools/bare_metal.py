"""Use the project's Reset_Handler without pulling in the host-style CRT."""
Import("env")

import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(env.subst("$PROJECT_DIR")) / "tools"))
from firmware_build_config import RUNTIME_LINK_FLAGS

# PlatformIO classifies -nostartfiles and -specs in build_flags as compiler
# flags. Pass them to the link driver too: use our startup and compact libc,
# matching tools/build_firmware.py.
env.Append(LINKFLAGS=RUNTIME_LINK_FLAGS)
# PlatformIO passes -flto in build_flags only to the compiler. Keep the link
# driver explicit too, matching the standalone build instead of relying on
# GCC's implicit LTO plugin activation.
if "-flto" in env.get("BUILD_FLAGS", []):
    env.Append(LINKFLAGS=["-flto"])
if "SOURCE_DATE_EPOCH" in os.environ:
    env["ENV"]["SOURCE_DATE_EPOCH"] = os.environ["SOURCE_DATE_EPOCH"]
