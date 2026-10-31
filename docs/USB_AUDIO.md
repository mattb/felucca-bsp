# USB audio capture

Capture defaults to two PCM16 channels with the standard left/right channel
positions. Playback, when enabled, remains stereo. A firmware may instead set:

```c
#define FELUCCA_UAC_CHANNELS 6
#define FELUCCA_UAC_CHANNEL_NAMES "Master L", "Master R", "A L", "A R", "B L", "B R"
```

The channel count is fixed at build time, from 1 through 6. Names are optional;
provide exactly one printable ASCII name per channel, up to 31 characters each.
Named channels use a non-spatial USB Audio 1.0 cluster and consecutive USB string
indexes starting at 3. This lets hosts expose stem names without interpreting
six channels as surround speakers. How applications group them into pairs is a
host decision. The default unnamed stereo descriptors keep their existing
layout and device revision; custom channel layouts use a different revision.

Call `uac_render_start()` before rendering. `uac.feed` indicates whether this
block will feed an active capture stream. Pass `uac_tap(samples, frames)` an
interleaved buffer with `FELUCCA_UAC_CHANNELS` signed 24-bit-scale samples per
frame. The driver rounds and saturates them to PCM16. Channel selection and
analog gain belong to the caller; the driver does not infer stems from stereo.

`FELUCCA_UAC_48K=1` advertises both 44.1 and 48 kHz. The render source remains
44.1 kHz. A host's 48 kHz request selects a shared-phase, independent-channel
160/147 FIR resampler in the device. Rate changes reset and prime the stream.
The included 32-tap Blackman-windowed sinc has exact Q30 unity DC gain per phase,
a 19.845 kHz cutoff, and a rolloff near the source Nyquist frequency. The
checked-in table is generated with Python's standard library:

```sh
python3 tools/gen_uac_fir.py > chip/uac_fir.h
sh tests/run_usb_audio_tests.sh
```

There is no dynamic allocation. At six channels, the normal 512-frame capture
ring occupies 6,144 bytes; the optional deep 1,024-frame ring occupies 12,288
bytes. The largest 48 kHz endpoint packet is 588 bytes (49 frames), below the
full-speed isochronous limit. For more than two channels the descriptor reserves
7/6 of the payload rounded to four bytes (688 bytes for six channels at 48 kHz),
while packet buffers and transmitted byte counts stay unchanged. This is extra
USB 2 hub scheduling headroom, not additional audio or padding. The USB standard
already accounts for worst-case bit stuffing in bus allocation; this conservative
reservation is a compatibility measure requiring hardware verification.
Resampler history occupies 768 bytes and its shared
coefficient table 10,240 bytes. FIR processing scales with the channel count;
measure render headroom with the application's DSP on real hardware.

The sanitized host tests exercise production descriptors and capture code for
one, two and six channels, with each rate option and the default stereo build.
They verify names, interleaving, underrun hold, channel isolation, 160/147 frame
counts, unity gain, ring wrap, block-boundary invariance and a 4.5 kHz tone's
resampling image below -60 dB. They also verify coefficient regeneration.
Hardware captures remain necessary to establish USB enumeration, channel names,
continuous transfer and CPU headroom on a particular host and firmware.
