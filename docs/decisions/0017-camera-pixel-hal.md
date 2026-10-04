# 0017: Camera through Pixel's camera HAL, with Mesa on the Android side

- Status: accepted (in progress)
- Date: 2026-10-04

## Context

The comet's cameras sit behind Google's image signal processor. It is driven through LWIS,
Google's kernel interface, and Pixel's camera HAL holds the knowledge of the sensors, tuning
and processing. Nobody has reverse-engineered that ISP. A camera pipeline of our own
(libcamera) would mean doing that first.

The Android container (ADR 0014) can run the HAL as the vendor shipped it. On a stock
Pixel the HAL leans on much more than LWIS:
- `android.frameworks.stats.IStats`: it waits for this service at start. On Android
  `system_server` provides it, and the container doesn't run that.
- **Sensors:** the sensor registry, loaded by Pixel's sensor HAL (USF, on the AOC), for the
  gyro. The sensor HAL in turn waits for Pixel's display HAL, if one is declared.
- **The GXP and the TPU:** their libraries live in /vendor and are linked into the HAL's
  vendor APEX.
- **The GPU:**
  - OpenCL, for HDRnet/local tone mapping. The HAL aborts without it. It also uses Arm's
    `clImportMemoryARM` to hand dma-bufs to OpenCL, and looks up the command buffer and
    semaphore extensions.
  - EGL/GLES: Android's libEGL aborts the process when there is no GLES driver.
  - Google ships both in Mali's driver (libGLES_mali), which needs Mali's kernel driver
    (kbase). Mobarchy drives the GPU with panthor and Mesa (ADR 0008).

How this was found: `tools/cl-probe`, an OpenCL driver that only records, lets the HAL
run far enough to show what it asks for.

## Decision

- **The camera runs Pixel's camera HAL** in the Android container, with cameraserver.
  The service definitions are in `devices/comet/.../comet.rc`.
- **The container provides what the HAL expects, as itself, not as Android:**
  - **Stand-ins for Android's own services:** `os/android-services`, libgbinder services
    on the Linux side, registered in the container's service manager. They answer as
    Android does when there is nothing to offer, or with Mobarchy's answer:
    - IStats accepts the HALs' metrics and drops them.
    - IPower, with Pixel's IPowerExt as its binder extension, accepts power hints and
      boosts and drops them, and reports hint sessions and headroom as unsupported.
      Pixel's own power HAL would set CPU, GPU and memory frequencies by Google's rules;
      on Mobarchy the Linux side decides those.
    - ISensorManager has no sensors yet (the camera runs without the gyro). A bridge to
      the container's sensor HAL, which would also give the Linux side its sensors, is
      separate work.
    - system_server's services that cameraserver asks about its clients (activity,
      appops, permission, permission_checker, sensor_privacy, media.camera.proxy,
      processinfo) say: in the foreground, granted, allowed, privacy off, not disabled.
      The container's clients are Mobarchy's own; Mobarchy's camera service on the Linux
      side decides who may use the camera.
    - SurfaceFlinger has no display to offer: cameraserver waits for it, but only paces
      streams consumed by a display to it.
  - **Sensor HAL:** it runs in the container with the USF device nodes.
  - **Display HAL:** the container doesn't run Pixel's display HAL, so the comet overlay
    empties its VINTF declarations, and HALs that use it when present carry on without.
  - **Vendor APEXes:** apex-info-list says which partition each APEX comes from, so that
    linkerconfig gives vendor APEXes their links into /vendor.
  - **The vendor's services the camera uses run in the container** as the vendor's rc
    files have them (comet.rc): the TPU's vendor services, the codecs (Codec 2.0: MFC,
    BigWave, Dolby, Android's software codecs) and Google's radio extension.
  - **What any Android expects of its system:** ashmem (with the boot-id link ueventd
    would make), Android's open-file limit (32768), and tombstoned for crash dumps.
- **GLES and OpenCL come from Mesa, built for Android** (`os/android-mesa`, with the NDK)
  and installed in the container's /odm/lib64:
  - First llvmpipe: GLES and OpenCL (rusticl) on the CPU. This gives a correct camera
    while the GPU path is built.
  - Then panfrost on panthor. It is the same Mesa as the Linux side's (ADR 0008).
  - rusticl gets `cl_arm_import_memory` (dma-buf and host). A driver that imports
    dma-bufs gets the dma-buf; llvmpipe gets a CPU mapping of it. This is a Mesa patch
    in `os/android-mesa/patches`, to be offered upstream.
- **Mali's own drivers stay unused.** The comet overlay points Mali's OpenCL entry at
  Mesa, and the container's properties select Mesa's EGL.

## Consequences

- **Build:** the Android side gets a build of its own: the NDK, then LLVM/Clang and the
  SPIR-V tools for Android, then Mesa. It is cross-compiled on the host.
- **Interim performance:** on llvmpipe the GPU stages run on the CPU. Preview frame rates
  and heat must be measured; panfrost is the target.
- **Fidelity:** whether the HAL's OpenCL programs run correctly on rusticl (Arm and
  Qualcomm extensions, subgroups, fp16) is verified as they come up. The HAL tolerates
  some missing extensions, which it checks for.
- **Mobarchy's side:** a camera app on the Linux side needs a client for cameraserver.
  `tools/camtest` has NDK clients for testing in the container (camlist, camshot); the
  Linux side needs its own way in (binder from Linux, or a service in the container that
  passes frames out as dma-bufs).

## Status (2026-10-04)

- Both cameras are listed (back 4000×3000, front 3840×2600). The back camera takes JPEGs
  with auto exposure and white balance working.
- Both cameras stream and take photos. The GXP faults that crashed the HAL's ML pipeline
  came from firmware of another build than the vendor's; it now comes from the vendor
  partition (ADR 0019).
- The HAL gets the motion sensors (gyro, for stabilisation) through Android's own
  sensorservice and ISensorManager in the container (os/android-sensors).
- Not yet measured: preview frame rate and heat on llvmpipe.
