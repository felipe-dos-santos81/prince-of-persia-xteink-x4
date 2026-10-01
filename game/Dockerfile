# The build fork's assembler (snap) and disk imager (crackle) are 32-bit
# Linux binaries, so the build runs in a 32-bit userland.
FROM i386/debian:bookworm-slim
RUN apt-get update \
    && apt-get install -y --no-install-recommends make patch \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
