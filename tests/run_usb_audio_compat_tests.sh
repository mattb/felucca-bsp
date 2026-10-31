#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Compare against the actual upstream implementation; no new-code expectations.
set -eu
cd "$(dirname "$0")/.."
COMPAT_DIR=$(mktemp -d)
trap 'rm -rf "$COMPAT_DIR"' EXIT HUP INT TERM
mkdir -p "$COMPAT_DIR/old/tests" "$COMPAT_DIR/old/chip"
cp tests/usb_host_test.c tests/usb_audio_compat_test.c "$COMPAT_DIR/old/tests/"
for header in chip/*.h chip/usb_host.c; do cp "$header" "$COMPAT_DIR/old/chip/"; done
git show b0b242801f935000c12b3c418ba4b2e416ccb111:chip/usb.c > "$COMPAT_DIR/old/chip/usb.c"
for playback in 0 1; do
    for tree in . "$COMPAT_DIR/old"; do
        "${CC:-cc}" -std=c11 -O1 -w -fsanitize=address,undefined -DFELUCCA_UAC_OUT=$playback \
            -I"$tree/chip" "$tree/tests/usb_audio_compat_test.c" -o "$COMPAT_DIR/test"
        if [ "$tree" = . ]; then name=new; else name=old; fi
        "$COMPAT_DIR/test" > "$COMPAT_DIR/$name.bin"
    done
    cmp "$COMPAT_DIR/old.bin" "$COMPAT_DIR/new.bin"
    echo "Default stereo vs upstream b0b2428: exact descriptors and PCM match (playback=$playback)"
done
