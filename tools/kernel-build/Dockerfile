# Build environment for Android (Kleaf/Bazel) kernels. Kleaf ships its own hermetic
# toolchain, but a few actions (e.g. DDK UAPI headers building scripts/unifdef via make's
# implicit rules) link against the host C runtime, hence build-essential.
FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential git git-lfs python3 python-is-python3 curl ca-certificates rsync bc bison flex \
    libssl-dev libelf-dev cpio kmod zip unzip xz-utils lz4 zstd openssl file locales \
    && rm -rf /var/lib/apt/lists/* \
    && curl -sSL https://storage.googleapis.com/git-repo-downloads/repo -o /usr/local/bin/repo \
    && chmod +x /usr/local/bin/repo && locale-gen en_US.UTF-8
ENV LANG=en_US.UTF-8
