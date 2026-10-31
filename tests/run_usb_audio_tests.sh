#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
cd "$(dirname "$0")/.."
AUDIO_TEST_DIR=$(mktemp -d)
trap 'rm -rf "$AUDIO_TEST_DIR"' EXIT HUP INT TERM
for channels in 1 2 6; do
    case "$channels" in
      1) names='"Mono"' ;;
      2) names='"Master L","Master R"' ;;
      6) names='"Master L","Master R","Deck A L","Deck A R","Deck B L","Deck B R"' ;;
    esac
    for half in 128 256; do
        for deep in 0 1; do
            "${CC:-cc}" -std=c11 -O1 -Wall -Wextra -Werror -Wno-unused-function \
                -Wno-unused-variable -Wno-missing-field-initializers \
                -fsanitize=address,undefined -Ichip -DFELUCCA_UAC_CHANNELS=$channels \
                -DHALF_FRAMES=$half -DFELUCCA_UAC_DEEP=$deep \
                "-DFELUCCA_UAC_CHANNEL_NAMES=$names" -DFELUCCA_UAC_48K=0 \
                tests/usb_audio_test.c -o "$AUDIO_TEST_DIR/audio"
            "$AUDIO_TEST_DIR/audio"
        done
    done
done
# Unconfigured legacy stereo retains its unnamed spatial L/R descriptor.
"${CC:-cc}" -std=c11 -O1 -w -fsanitize=address,undefined -Ichip tests/usb_audio_test.c -o "$AUDIO_TEST_DIR/default"
"$AUDIO_TEST_DIR/default"
# Unsupported channel/rate combinations must be rejected explicitly.
if "${CC:-cc}" -std=c11 -Ichip -DFELUCCA_UAC_CHANNELS=6 -DFELUCCA_UAC_48K=1 \
        -fsyntax-only tests/usb_audio_test.c 2> "$AUDIO_TEST_DIR/rejected"; then
    echo 'ERROR: multichannel 48 kHz unexpectedly accepted' >&2
    exit 1
fi
grep -q 'Multichannel USB capture requires FELUCCA_UAC_48K=0' "$AUDIO_TEST_DIR/rejected"
