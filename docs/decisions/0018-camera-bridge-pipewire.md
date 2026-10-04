# 0018: The cameras on the Linux side: a bridge from cameraserver to PipeWire

- Status: accepted
- Date: 2026-10-04

## Context

The cameras work through Pixel's camera HAL and cameraserver in the Android container
(ADR 0017). Linux programs can't use them yet:
- Android's camera API is binder and C++ (or the NDK's C API), and runs in Android's
  processes.
- Linux programs expect a camera from PipeWire or V4L2:
  - browsers (WebRTC, through the camera portal);
  - GStreamer and OBS;
  - Mobarchy's own camera app.

## Decision

- **A camera bridge in the container** (`os/camera/camerad`, `mobarchy-camerad`): a small
  native service built with the NDK.
  - It uses the NDK camera API, as an app would.
  - It serves the Linux side on a Unix socket in the container's /data, which the host
    reaches directly.
  - It opens a camera on request and streams its frames (YUV) through shared memory
    (memfd, passed with SCM_RIGHTS).
  - It takes stills as JPEG, at full quality, through the HAL's own pipeline.
- **PipeWire video sources on the Linux side** (`os/camera/source`, `mobarchy-camera-source`): one PipeWire
  node per camera ("Back camera", "Front camera").
  - A node streams while something uses it.
  - Any PipeWire client gets the cameras: browsers through the camera portal,
    GStreamer's `pipewiresrc`, and Mobarchy's camera app.
- **Mobarchy's camera app** (Qt Quick, Mobarchy's theme) shows the stream and takes photos
  through the bridge, so that photos are the HAL's full-quality JPEGs. Photos go to
  `~/Pictures`.
- **The agent gets a tool to take a photo** (and look at it), with the same confirmation
  as its other device actions.

V4L2 loopback was not chosen. It would need an out-of-tree kernel module for every
device, and it offers less than PipeWire: no portal, and no way to know when a reader
wants frames.

## Consequences

- Frames are copied once, into shared memory. That is fine for preview and video calls
  (1080p at 30 fps is about 90 MB/s). Zero-copy (dma-buf straight from the HAL's
  buffers) can come later, without changing the PipeWire side.
- Who may use the camera is decided on the Linux side, where Mobarchy's portal and
  PipeWire's permissions apply. The container's answers to cameraserver say "allowed"
  (ADR 0017).
- The flash, focus taps and zoom between the lenses go through the bridge as requests
  (Android's capture request keys).
- A photo's orientation is set from the device's rotation, which the client passes with
  the photo. The bridge sets ACAMERA_JPEG_ORIENTATION, and the JPEG says it in EXIF.
- The device's state goes through the bridge too. The camera source reads the lid switch
  (SW_LID: a foldable's hinge) and sends folded or unfolded; camerad tells cameraserver,
  as system_server does on Android (notifyDeviceStateChange). The HAL picks the physical
  camera by it: on comet the front camera is the outer one when folded and the inner one
  when open.
- Each new capture session reconfigures the HAL's whole pipeline, which is slow. So a
  stream's session also carries a JPEG output (a photo while streaming is one request),
  and a stream PipeWire pauses briefly keeps its session.
