# Aeon build base

A container that compiles native games against exactly what the Aeon console runs: Debian 13, glibc 2.41,
GCC 14, SDL2 2.32, SDL2_mixer 2.8, the Vulkan 1.4 loader and headers, libcurl 8.14 / OpenSSL 3.5, Wayland 1.23.
A binary built here cannot pick up a symbol the console lacks — the cause of "works on my machine, won't launch
on the console".

    docker pull ghcr.io/dwizzle2009/aeon-buildbase

## Build in it

    # CMake project
    docker run --rm -v "$PWD":/src -w /src ghcr.io/dwizzle2009/aeon-buildbase cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    docker run --rm -v "$PWD":/src -w /src ghcr.io/dwizzle2009/aeon-buildbase cmake --build build

    # or a shell
    docker run --rm -it -v "$PWD":/src -w /src ghcr.io/dwizzle2009/aeon-buildbase

Podman works the same way. On Windows and macOS, Docker Desktop runs the image in a Linux VM; the output is a
Linux binary, which is the point.

## What is inside

- `gcc`/`g++` 14, `cmake`, `ninja`, `make`, `pkg-config`, `git`, `python3`, `glslc`, `glslangValidator`, `spirv-*`
- `-dev` packages **only** for libraries the console has at runtime: SDL2, SDL2_mixer, Vulkan loader, libcurl,
  OpenSSL, Wayland client, xkbcommon. Anything else your game needs, bundle beside the executable with
  `-Wl,-rpath,'$ORIGIN'` (or `$ORIGIN/lib`).
- `libaeon` prebuilt from source at `/opt/aeon/sdk`: `-I/opt/aeon/sdk -L/opt/aeon/sdk -laeon -ldl -lpthread`,
  and ship `/opt/aeon/sdk/libaeon.so` beside your executable.
- `aeon-pack` on `PATH`, with the package spec at `/opt/aeon/PACKAGE.md`.
- `cat /etc/aeon-buildbase` prints the versions the image was built with.

## Engines

Godot and Unreal already produce portable Linux binaries from their own toolchains; this image is for engines and
games built from C/C++ source. The console's full specification is on the developer portal under Docs → Platform.

## Rebuilds

The image is rebuilt on every push and weekly (Debian security updates). `verify.sh` builds an SDL2+Vulkan+libaeon
program and refuses to publish if it would need a newer glibc than the console's.
