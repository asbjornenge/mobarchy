# Tools

Containerized helpers. They need Docker, but no sudo and no host packages.

| Tool | What it does |
|---|---|
| `android-images/unpack <image-dir> <out>` | Unpacks a factory image: boot-type images (kernel, ramdisks, dtb, header info) and erofs/ext4 partitions. |
| `kernel-build/build --device <codename> --src <dir> [--stock] [--detach]` | Syncs and builds a Pixel gs-6.1 kernel and all modules from LineageOS sources (Kleaf). Without `--stock` it applies `os/kernel/mobarchy_defconfig`. |

Large inputs and outputs (factory images, kernel trees at ~15 GB, build output) belong on external storage, never in the repo.
