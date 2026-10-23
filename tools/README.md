# Tools

Host helpers. The containerized ones need Docker, but no sudo and no host packages.

| Tool | What it does |
|---|---|
| `android-ndk` | Fetches the Android NDK (r30, SHA-1 checked) for the Android container's programs and Mesa, to `~/.cache/mobarchy`. |
| `android-images/unpack <image-dir> <out>` | Unpacks a factory image: boot-type images (kernel, ramdisks, dtb, header info) and erofs/ext4 partitions. |
| `kernel-build/extmod --src <dir> --device <codename> --obj <dir> --module <dir>` | Builds an out-of-tree module against a kernel from `kernel-build/build`, for loading from the rootfs. Takes the config from the kernel image (IKCONFIG) and refuses to build if layouts could differ. |
| `pkg-build <name> --out <dir>` | Builds a Mobarchy-patched Arch package (`os/packages/<name>`) for aarch64. Put results in the image with `os/image/build --extra packages=<dir>`. Packages already in `<dir>` can be its dependencies (e.g. libmce-glib for oFono). |
| `cross-cargo <package dir> --work <dir> [--repo <dir>]` | Cross-compiles a Rust (cargo) PKGBUILD for aarch64 on this PC: an Arch Linux ARM sysroot, rust with aarch64's standard library and clang/lld in an x86_64 container, then the PKGBUILD's package() in the aarch64 container. Minutes instead of hours under qemu-user (e.g. `os/packages/spotify-player`). |
| `device/mcp-tunnel [--host mob@172.16.42.1] [--port 18765]` | Forwards a device's MCP server (loopback only on the device) to the host over ssh, at `http://127.0.0.1:18765/mcp`. |
| `kernel-build/build --device <codename> --src <dir> [--stock] [--detach]` | Syncs and builds a Pixel gs-6.1 kernel and all modules from LineageOS sources (Kleaf). Without `--stock` it applies `os/kernel/mobarchy_defconfig`. |

Large inputs and outputs (factory images, kernel trees at ~15 GB, build output) belong on external storage, never in the repo.
