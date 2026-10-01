# 0008: comet GPU acceleration via Panthor (backported) and stock Mesa

- Status: accepted (Asbjørn, 2026-10-01: "no quick wins, the very good solution"); in progress
- Date: 2026-10-01
- Amends: [0007](0007-comet-kernel.md), which assumed GPU acceleration would come from libhybris and the Mali blob.

## Context

**Performance on comet today** (outer panel, a terminal scrolling full screen):

| Setup | FPS | CPU |
|---|---|---|
| Hyprland, GL in software (Mesa llvmpipe) | 28 | ~420 % |
| sway with the pixman renderer (CPU compositing, no GL) | 59 (full 60 Hz) | 16 % |

`perf` shows 94 % of Hyprland's time in llvmpipe's JIT-compiled shaders. The CPU and the DRM/KMS path are fast enough. GL emulated on the CPU is not. Hyprland only has a GL renderer, so it needs the GPU to feel right. Asbjørn's guidance is to skip interim shortcuts and aim for the very good solution.

**The GPU, as reported on the device:** Mali-G715 MC7, arch 11.8.10 r1p3, `GPU_ID 0x0B080A02`. This is Valhall v11 with a CSF command stream frontend.
- Android drives it with Arm's kbase (DDK r54p1, CSF UAPI 1.38) plus Google's `mali_pixel`.
- There are two power domains: `pd-g3d` and `pd-embedded_g3d`.
- An S2MPU (`1ee70000.s2mpu_s0_g3d`) sits in front of it.
- The clocks are set through Google's cal-if/ACPM, not through the common clock framework.
- CSF firmware ships as `mali_csffw-r5xpy.bin`.
- kbase loads and brings the GPU up on our kernel.

**Upstream status:**
- **Panthor** is the upstream kernel driver for CSF Malis. It supports G715 since Linux 6.18 (commit 3b1dc21d6d80, "Add support for Mali-Gx15 family"), and linux-firmware ships `arm/mali/arch11.8/mali_csffw.bin`.
- **Mesa** (Panfrost GL and PanVK Vulkan) lists G715 (v11) with GLES 3.1, GL 3.1 and Vulkan 1.4. It is not yet conformance-tested on v11. (Correction, found on the device: this is Mesa main, 26.3-devel. The released Mesa 26.2.3 has no v11.)
- **The gap is only the kernel:** we run 6.1. It lacks `drm_gpuvm` and `drm_exec`, and has an older `drm_sched` and GEM shmem helpers.

## Options

1. **Panthor backported to our 6.1 kernel, with stock Mesa.** Hard: a kernel port plus Pixel power and clock glue. Upstream driver, upstream firmware, distro Mesa untouched. Improves for free as Mesa and Panthor improve.
2. **Mesa on kbase** (a kbase backend for Mesa's `pan_kmod`; no kernel changes). Medium. There is no upstream backend. The existing forks are Vulkan-only, target CSF UAPI 1.21 rather than our 1.38, and are beta or abandoned. We would maintain a Mesa fork forever and chase Google's DDK versions.
3. **libhybris and the Android blob (Halium).** Medium-hard. It is proprietary and fragile, and Hyprland is unproven on it (it needs libgbm-hybris). It drags the Android userspace into the OS.
4. **Mainline kernel for zumapro.** Very hard and long-term. There is no upstream SoC support today. Even Pixel 6 has open work on power domains, IOMMU and Mali.

## Decision

**Option 1.** Build Panthor (from the 6.18+ upstream tree) as a **self-contained backport module** for our 6.1 kernel:

- The module bundles what 6.1 lacks: `drm_gpuvm`, `drm_exec`, and the newer `drm_sched` and shmem helpers, renamed so they cannot clash with the core. The GKI core and `boot.img` stay unchanged. The module lives in the rootfs and loads from `/lib/modules`, so it can be iterated over ssh without flashing.
- **The Pixel glue lives in the device layer** (`devices/comet/kernel/`). It covers:
  - matching the existing DT node (`arm,malit6xx`), so no DT or dtbo changes are needed at first;
  - attaching both genpd power domains;
  - setting the GPU clock through cal-if (exported `cal_dfs_*`);
  - keeping the S2MPU configured, as kbase's platform code does.
- **Firmware:** upstream linux-firmware `arch11.8/mali_csffw.bin`, with the Pixel blobs as a fallback if Google's GPU needs them.
- **kbase stays the default** until Panthor works. The switch is made by blacklisting `mali_kbase` and loading `panthor`, which is reversible.
- **Userspace:** stock Mesa: Mesa main until a release has v11, then Arch's Mesa unchanged. Hyprland renders on the Panthor render node and scans out on exynos-drm. Both use dma-buf from the system heap; patch 0003 cleans the CPU cache before scanout.

## Plan (each step is verifiable on its own)

1. Build the backport module against our kernel tree, then load and probe it with kbase not loaded. Done when Panthor reads `GPU_ID` and its MMU and IRQs come up.
2. Power, clock and firmware glue. Done when the CSF firmware boots and Panthor logs the firmware version.
3. Mesa. Done when `eglinfo`/`vulkaninfo` show Panfrost/PanVK on G715, and the GL/Vulkan smoke tests pass (`kmscube`, `vkcube`).
4. Hyprland on the GPU. Done when the scroll benchmark and real usage both run at 60 Hz without stutter (120 Hz later).
5. Hardening: GPU DVFS (cal-if levels), suspend/resume with the panels, thermal, and an upstream-style write-up.

## Progress

- **2026-10-01, step 1 done.**
  - The backport module builds and loads: `os/kernel/panthor` assembles Panthor and the bundled helpers from v6.18.54; `tools/kernel-build/extmod` builds it against our kernel.
  - `panthor_pixel` (`devices/comet/kernel/panthor-pixel`) provides the cal-if clock, the OPPs, the power domain and S2MPU links, and the driver override.
  - Panthor reads `GPU_ID 0xb8a2` r1p3 with `shader_present 0x1110055`.
- **2026-10-01, step 2 done.**
  - The CSF firmware boots (upstream linux-firmware `arch11.8/mali_csffw.bin`, interface v2.8.0).
  - The missing pieces were Pixel-specific: the GPU master's bus protection must be set by a secure monitor call (`SMC_PROTECTION_SET`, G3D) after every power-up and cleared before every power-down, exactly as kbase does. Without the first, every GPU access is a bus error. Without the second, powering off the domain hard-locks the SoC.
  - Also needed: the L2 slice hash from DT and a soft reset before the first boot.
  - Runtime suspend/resume and module reload work.
- **2026-10-01, steps 3 and 4 done.**
  - Mesa main (`os/mesa/build` into `/opt/mesa-main`) gives `OpenGL ES 3.1 Mesa 26.3.0-devel` on `Mali-G715 MC7 (Panfrost)`.
  - Hyprland renders on Panthor through kmsro (display on exynos-drm `card0`), with linux-dmabuf for clients. That needed PRIME hooks the 6.1 core requires (patch 0005).
  - Scroll benchmark: **58 FPS at 26 % CPU** (llvmpipe: 28 FPS at 420 %).
  - At boot, `mobarchy-gpu.service` loads the modules with a crash guard and switches the session's environment; software rendering remains the fallback.
  - PanVK (Vulkan) is still gated by Mesa for v11.
- (Before step 3: Mesa was the blocker.) Arch's Mesa 26.2.3 has no Valhall v11 (G615/G715). Mesa main (26.3-devel) has it: `PAN_PROD_ID(11, 8, 2)`, variant 4. `os/mesa/build` builds Mesa main for the device into `/opt/mesa-main`, next to the distribution's Mesa.

Hardening list (step 5), found along the way:
- Custom io-pgtable allocators: Panthor's page table pre-reservation. 6.1 lacks the hooks.
- fdinfo memory stats.
- Fence deadlines.
- `disable_work_sync` semantics (mapped to `cancel_work_sync`).

## Consequences

- This is weeks of kernel work, not days. Software rendering stays the fallback the whole time, so the phone remains usable.
- We own a driver backport and must follow upstream Panthor for fixes. Keeping it as one module with a clear list of bundled helpers keeps that manageable. If we later move comet to a newer kernel, the backport layer goes away.
- A GPU fault cannot brick the device: the worst case is a hang or reboot, with kbase one module swap away. No new partitions are written. The module and firmware ship in the rootfs.
- The device-agnostic part (Panthor plus stock Mesa) should carry over to other CSF-Mali phones. Only the power and clock glue is comet-specific.
