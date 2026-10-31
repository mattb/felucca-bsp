# USB audio

Capture (device to computer) and playback (computer to device) default to two
PCM16 channels with standard left/right positions. Firmware can select each
layout independently, with one to six channels at 44.1 kHz:

```c
#define FELUCCA_UAC_CHANNELS 6
#define FELUCCA_UAC_CHANNEL_NAMES "Master L", "Master R", "A L", "A R", "B L", "B R"
#define FELUCCA_UAC_OUT_CHANNELS 6
#define FELUCCA_UAC_OUT_CHANNEL_NAMES "Input 1", "Input 2", "Input 3", "Input 4", "Input 5", "Input 6"
#define FELUCCA_UAC_48K 0
```

Names are optional. Supply exactly one printable ASCII name per channel, up to
31 characters each. Named channels use non-spatial USB Audio 1.0 clusters, so
hosts can expose stems without interpreting six channels as surround speakers.
Capture names start at USB string index 3; playback names follow the capture
channel range. How applications group channels into pairs is a host decision.
The default unnamed stereo descriptors retain their existing device revision;
custom layouts use a different revision.

Call `uac_render_start()` before rendering. `uac.feed` indicates whether this
block feeds an active capture stream. `uac_tap(samples, frames)` accepts
interleaved signed 24-bit-scale samples, with `FELUCCA_UAC_CHANNELS` per frame,
and rounds and saturates them to PCM16.

`uac_play(buffer, frames, gain_q12)` adds host audio to an interleaved buffer with
`FELUCCA_UAC_OUT_CHANNELS` per frame. The existing fill-controlled interpolation
compensates for the difference between the USB and I2S clocks; every channel uses
the same phase. For application-controlled input, zero a separate buffer and call
it with gain 4096 before rendering. The result is signed 24-bit-scale audio,
including silence while stopped, priming or recovering from an underrun. The
application decides how to route or mix it and clamp its output. Legacy stereo
addition retains its existing gain and clipping behavior.

Storage is fixed. At six channels, capture uses 6,144 bytes for its normal
512-frame ring (12,288 with the existing deep option), and playback uses 24,576
bytes for its 2,048-frame ring. Each multichannel endpoint reserves at most 46
frames, or 552 bytes at six channels. Two such endpoints fit within full-speed
USB's periodic budget: approximately 878 microseconds of the 900-microsecond
allowance using worst-case bit stuffing and a 1-microsecond host delay per
transaction. Other periodic devices can consume that remaining budget; duplex
operation still needs testing on the intended host and hub. Legacy stereo
playback retains its existing 49-frame reservation.

The engine and new multichannel streams run at 44.1 kHz without a capture
resampler. The existing stereo-only 48 kHz capture path remains available to
applications supplying its external `uac_fir.h`; non-stereo capture at 48 kHz is
rejected explicitly. Playback remains fixed at 44.1 kHz.

Run `sh tests/run_usb_audio_tests.sh` for sanitized production-code tests of
named descriptors, endpoint receive order, interpolation, isolation, wrap,
priming, underruns, full-ring rejection and signed sample limits. Configurations
cover one, two and six channels, both ring depths, 128/256-frame render blocks,
and asymmetric capture/playback layouts. `sh tests/run_usb_audio_compat_tests.sh`
compares default stereo descriptors, capture PCM and playback mixing byte-for-byte
with upstream `b0b2428`; it requires that commit in local Git history.
Hardware captures remain necessary to establish names, continuous duplex transfer
and CPU headroom on a particular host and firmware.
