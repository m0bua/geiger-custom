"""Use the project's Reset_Handler without pulling in the host-style CRT."""
Import("env")

# PlatformIO classifies -nostartfiles and -specs in build_flags as compiler
# flags. Pass them to the link driver too: use our startup and compact libc,
# matching tools/build_firmware.py.
env.Append(LINKFLAGS=["-nostartfiles", "-specs=nano.specs", "-specs=nosys.specs"])
