"""Use the project's Reset_Handler without pulling in the host-style CRT."""
Import("env")

# PlatformIO classifies -nostartfiles in build_flags as a compiler flag.
# It must reach the GCC link driver to suppress crt0/crti/crtn as well.
env.Append(LINKFLAGS=["-nostartfiles"])
