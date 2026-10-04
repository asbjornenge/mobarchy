# The build environment for os/android-mesa/build (x86_64): Mesa's build tools, and Rust
# with Android's target for rusticl. The NDK and the work directory are mounted in; the
# build runs as the calling user.
FROM archlinux:latest
RUN pacman -Syu --noconfirm --needed base-devel git cmake ninja meson pkgconf python-mako \
        python-ply python-yaml python-packaging bison flex glslang rustup rust-bindgen libclc \
        llvm spirv-llvm-translator spirv-tools spirv-headers libdrm expat zlib zstd \
        clang && pacman -Scc --noconfirm
ENV RUSTUP_HOME=/opt/rust CARGO_HOME=/opt/rust PATH=/opt/rust/bin:$PATH
RUN rustup default stable && rustup target add aarch64-linux-android && chmod -R a+rX /opt/rust
