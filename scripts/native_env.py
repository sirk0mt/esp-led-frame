"""Host-specific tweaks for the native test environment (docs/DEVELOPMENT.md, section 6)."""

import sys

Import("env")  # noqa: F821  # SCons injects Import into PlatformIO scripts.

if sys.platform == "win32":
    # FastLED's stub platform uses sockets; MinGW has to link Winsock explicitly.
    env.Append(LIBS=["ws2_32"])  # noqa: F821
