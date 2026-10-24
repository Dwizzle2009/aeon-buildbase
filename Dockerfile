# Aeon build base -- compile native games against exactly what the console runs.
#
# The console is Debian 13 (trixie): glibc 2.41, libstdc++ from GCC 14, SDL2 2.32, SDL2_mixer 2.8,
# Vulkan 1.4 loader, libcurl 8.14 / OpenSSL 3.5, Wayland 1.23. Building inside this image means a binary
# cannot pick up a symbol the console lacks. Only libraries that exist on the console at runtime have
# their -dev packages here; anything else your game needs, bundle beside the executable with RPATH $ORIGIN.
#
#   docker run --rm -v "$PWD":/src -w /src ghcr.io/dwizzle2009/aeon-buildbase cmake -B build -G Ninja && \
#   docker run --rm -v "$PWD":/src -w /src ghcr.io/dwizzle2009/aeon-buildbase cmake --build build
#
# libaeon is prebuilt at /opt/aeon/sdk (aeon.h, libaeon.so, libaeon.a): -I/opt/aeon/sdk -L/opt/aeon/sdk -laeon
# aeon-pack is on PATH.
FROM debian:trixie-slim

LABEL org.opencontainers.image.title="Aeon build base" \
      org.opencontainers.image.description="Toolchain and the console's exact runtime library versions for building native Aeon games" \
      org.opencontainers.image.source="https://github.com/Dwizzle2009/aeon-buildbase"

ENV DEBIAN_FRONTEND=noninteractive
# One package per line so a renamed package names itself in the build log. libvulkan-dev carries the headers on Debian.
RUN apt-get update && for p in \
      build-essential gcc g++ cmake ninja-build make pkg-config git ca-certificates \
      python3 zip unzip xz-utils file binutils \
      libsdl2-dev libsdl2-mixer-dev \
      libvulkan-dev glslang-tools glslc spirv-tools \
      libcurl4-openssl-dev libssl-dev \
      libwayland-dev wayland-protocols libxkbcommon-dev; do \
      apt-cache show "$p" >/dev/null 2>&1 || { echo "no such package in this Debian: $p" >&2; exit 1; }; done \
    && apt-get install -y --no-install-recommends \
      build-essential gcc g++ cmake ninja-build make pkg-config git ca-certificates \
      python3 zip unzip xz-utils file binutils \
      libsdl2-dev libsdl2-mixer-dev \
      libvulkan-dev glslang-tools glslc spirv-tools \
      libcurl4-openssl-dev libssl-dev \
      libwayland-dev wayland-protocols libxkbcommon-dev \
    && rm -rf /var/lib/apt/lists/*

# libaeon, built here so it carries this glibc's symbol versions and nothing newer
COPY sdk/libaeon /opt/aeon/sdk-src
RUN make -C /opt/aeon/sdk-src && mkdir -p /opt/aeon/sdk \
    && cp /opt/aeon/sdk-src/aeon.h /opt/aeon/sdk-src/libaeon.so /opt/aeon/sdk-src/libaeon.a /opt/aeon/sdk/ \
    && rm -rf /opt/aeon/sdk-src
COPY sdk/aeon-pack /usr/local/bin/aeon-pack
COPY sdk/PACKAGE.md /opt/aeon/PACKAGE.md
RUN chmod +x /usr/local/bin/aeon-pack

# what this image is, for `cat /etc/aeon-buildbase`
RUN { echo "aeon-buildbase $(. /etc/os-release && echo "$PRETTY_NAME")"; \
      echo "glibc $(ldd --version | head -n1 | sed 's/.*) //')"; \
      echo "gcc $(gcc -dumpfullversion)"; \
      echo "sdl2 $(pkg-config --modversion sdl2)  sdl2_mixer $(pkg-config --modversion SDL2_mixer)"; \
      echo "vulkan $(pkg-config --modversion vulkan)"; \
      echo "curl $(curl-config --version | awk '{print $2}')  openssl $(openssl version | awk '{print $2}')"; \
      echo "wayland $(pkg-config --modversion wayland-client)"; } > /etc/aeon-buildbase && cat /etc/aeon-buildbase

WORKDIR /src
CMD ["bash"]
