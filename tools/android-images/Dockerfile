FROM debian:trixie
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    python3 erofs-utils android-sdk-libsparse-utils mkbootimg lz4 cpio file e2fsprogs xz-utils gzip binutils device-tree-compiler kmod zstd fdisk ca-certificates git python3-pip \
    && rm -rf /var/lib/apt/lists/*
