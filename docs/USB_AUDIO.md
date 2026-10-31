# USB audio capture

Capture defaults to two PCM16 channels with the standard left/right channel
positions. Playback, when enabled, remains stereo. A firmware may instead set:

```c
#define FELUCCA_UAC_CHANNELS 6
#define FELUCCA_UAC_CHANNEL_NAMES "Master L", "Master R", "A L", "A R", "B L", "B R"
#define FELUCCA_UAC_48K 0
```

Named multichannel capture runs at 44.1 kHz. The channel count is fixed at build
time, from 1 through 6. Names are optional; provide exactly one printable ASCII
name per channel, up to 31 characters each. Named channels use a non-spatial USB
Audio 1.0 cluster and consecutive USB string indexes starting at 3. This lets
hosts expose stem names without interpreting six channels as surround speakers.
How applications group them into pairs is a host decision. The default unnamed
stereo descriptors keep their existing layout and device revision; custom
channel layouts use a different revision.

Call `uac_render_start()` before rendering. `uac.feed` indicates whether this
block will feed an active capture stream. Pass `uac_tap(samples, frames)` an
interleaved buffer with `FELUCCA_UAC_CHANNELS` signed 24-bit-scale samples per
frame. The driver rounds and saturates them to PCM16. Channel selection and
analog gain belong to the caller; the driver does not infer stems from stereo.

There is no dynamic allocation. At six channels, the normal 512-frame capture
ring occupies 6,144 bytes; the existing optional deep 1,024-frame ring occupies
12,288 bytes. The largest endpoint packet is 552 bytes (46 frames), below the
full-speed isochronous limit. The endpoint descriptor advertises that same
maximum payload size. Capture uses the engine's 44.1 kHz samples directly.
The existing stereo-only 48 kHz path remains available to applications that
supply its external `uac_fir.h`; non-stereo 48 kHz builds are rejected explicitly.

Run `sh tests/run_usb_audio_tests.sh` for sanitized tests of production
descriptors and capture code: one, two and six channels, both existing ring
depths, 128/256-frame render blocks, and the default stereo build. Tests verify
names, interleaving, underrun hold, channel isolation, ring wrap and whole-block
rejection when the ring is full. `sh tests/run_usb_audio_compat_tests.sh` compares
default stereo descriptors and PCM byte-for-byte with upstream `b0b2428`; this
comparison requires that commit in the local Git history.
Hardware captures remain necessary to establish USB enumeration, channel names,
continuous transfer and CPU headroom on a particular host and firmware.
