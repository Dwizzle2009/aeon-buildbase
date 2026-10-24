#!/usr/bin/env bash
# Runs inside the image. Builds the smoke program, checks it needs nothing newer than the console's glibc,
# and runs aeon-pack's self-check on a template manifest.
set -euo pipefail
cat /etc/aeon-buildbase
gcc -O2 -o /tmp/smoke verify/smoke.c -I/opt/aeon/sdk $(pkg-config --cflags --libs sdl2 vulkan) -L/opt/aeon/sdk -laeon -Wl,-rpath,/opt/aeon/sdk -ldl -lpthread
/tmp/smoke
need=$(objdump -T /tmp/smoke /opt/aeon/sdk/libaeon.so | grep -o 'GLIBC_[0-9.]*' | sort -V | tail -n1)
echo "highest glibc symbol needed: $need (console has 2.41)"
[ "$(printf '%s\n' "$need" GLIBC_2.41 | sort -V | tail -n1)" = "GLIBC_2.41" ] || { echo "needs newer glibc than the console"; exit 1; }
mkdir -p /tmp/g && cd /tmp/g && aeon-pack init . >/dev/null && echo "aeon-pack init ok"
echo "verify: ok"
