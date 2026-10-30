"""Make serial PlatformIO uploads use Intel HEX and verify the programmed data."""

import subprocess
import sys
from pathlib import Path

Import("env")

sys.path.insert(0, str(Path(env.subst("$PROJECT_DIR")) / "tools"))
from check_firmware import check_firmware

if env.IsIntegrationDump():
    Return()

def build_intel_hex(target, source, env):
    build_dir = Path(env.subst("$BUILD_DIR"))
    program = env.subst("$PROGNAME")
    elf = build_dir / (program + ".elf")
    ihex = build_dir / (program + ".hex")
    objcopy = env.subst("$OBJCOPY")
    check_firmware(elf)
    subprocess.run(
        [objcopy, "-O", "ihex", "-R", ".eeprom", str(elf), str(ihex)],
        check=True,
    )
    print("Prepared Intel HEX image: {}".format(ihex))
    return 0


env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", build_intel_hex)

if env.subst("$UPLOAD_PROTOCOL") == "serial":
    env.AddPreAction("upload", build_intel_hex)
    env.Prepend(UPLOADERFLAGS=["-v"])
    env.Replace(
        UPLOADCMD=(
            '$UPLOADER $UPLOADERFLAGS '
            '"$BUILD_DIR/${PROGNAME}.hex" '
            '"${__configure_upload_port(__env__)}"'
        )
    )
