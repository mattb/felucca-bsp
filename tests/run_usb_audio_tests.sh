#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
cd "$(dirname "$0")/.."
AUDIO_TEST_DIR=$(mktemp -d)
trap 'rm -rf "$AUDIO_TEST_DIR"' EXIT HUP INT TERM
python3 tools/gen_uac_fir.py > "$AUDIO_TEST_DIR/uac_fir.h"
cmp chip/uac_fir.h "$AUDIO_TEST_DIR/uac_fir.h"
for channels in 1 2 6; do
    case "$channels" in
      1) names='"Mono"' ;;
      2) names='"Master L","Master R"' ;;
      6) names='"Master L","Master R","Deck A L","Deck A R","Deck B L","Deck B R"' ;;
    esac
    for half in 128 256; do
      for deep in 0 1; do
       for rate in 0 1; do
        "${CC:-cc}" -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -Wno-unused-function \
            -Wno-unused-variable -Wno-missing-field-initializers \
            -fsanitize=address,undefined -Ichip -DFELUCCA_UAC_CHANNELS=$channels \
            -DHALF_FRAMES=$half -DFELUCCA_UAC_DEEP=$deep \
            "-DFELUCCA_UAC_CHANNEL_NAMES=$names" -DFELUCCA_UAC_48K=$rate \
            tests/usb_audio_test.c -lm -o "$AUDIO_TEST_DIR/audio"
        "$AUDIO_TEST_DIR/audio"
       done
      done
    done
done
# Unconfigured legacy stereo remains valid, including its unnamed spatial L/R descriptor.
"${CC:-cc}" -std=c11 -O1 -ffp-contract=off -w -fsanitize=address,undefined -Ichip tests/usb_audio_test.c -lm -o "$AUDIO_TEST_DIR/default"
"$AUDIO_TEST_DIR/default"
# Compare against a checked-in fixed-point fixture, independent of this generator.
"${CC:-cc}" -std=c11 -O2 -ffp-contract=off -w -fsanitize=address,undefined \
    -Ichip -DFELUCCA_UAC_CHANNELS=6 -DFELUCCA_UAC_48K=1 \
    tests/usb_audio_precision_test.c -o "$AUDIO_TEST_DIR/precision"
"$AUDIO_TEST_DIR/precision"
