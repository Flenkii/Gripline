#!/bin/sh
# Builds "Gripline Lite Installer.exe" with zig (pip install ziglang==0.15.2).
set -e
cd "$(dirname "$0")"
ZIG=${ZIG:-zig}
python3 embed.py .. files.h
$ZIG cc -target x86_64-windows-gnu -O2 -municode -Wl,--subsystem,windows main.c app.rc \
  -o "Gripline Lite Installer.exe" -lshell32 -luser32
sha256sum "Gripline Lite Installer.exe"
