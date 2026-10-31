/* SPDX-License-Identifier: MPL-2.0
 * Production float resampler vs the separately retained integer coefficient
 * design and integer convolution. Reference header is extracted by the runner
 * from the pre-float commit; never regenerated from the code under test. */
#define TEST_UAC 1
#define TEST_HOST 0
#define main usb_host_model_main
#include "usb_host_test.c"
#undef main
#include "uac_fir_reference.h"
int main(void)
{
    int16_t history[UA_CHANNELS][32] = {{0}};
    uint32_t phase = 0, seed = 7, compared = 0, changed = 0;
    int max_delta = 0;
    usb.config = 1; uac_stream(1); uac_rate_set(48000);
    uac.flowing = 1; uac_render_start(); ua_r = ua_w;
    for (unsigned frame = 0; frame < 250000; frame++) {
        int32_t input[UA_CHANNELS];
        for (unsigned c = 0; c < UA_CHANNELS; c++) {
            seed = seed * 1664525u + 1013904223u;
            int sample = (int)(seed >> 16) - 32768;
            if (frame < 256) sample = 32767;
            else if (frame < 512) sample = -32768;
            else if (frame < 768) sample = frame & 1 ? 32767 : -32768;
            history[c][frame & 31u] = sample;
            input[c] = sample * 256;
        }
        uint32_t begin = ua_w, outputs = 0;
        uac_tap(input, 1);
        do {
            for (unsigned c = 0; c < UA_CHANNELS; c++) {
                int64_t sum = 0;
                for (unsigned tap = 0; tap < 32; tap++) {
                    int coefficient = phase < 80 ? UAC_FIR_REFERENCE[phase][tap]
                        : UAC_FIR_REFERENCE[159-phase][31-tap];
                    sum += (int64_t)coefficient * history[c][(frame+1+tap)&31u];
                }
                int reference = (int)((sum + (1<<29)) >> 30);
                if (reference > 32767) reference = 32767;
                if (reference < -32768) reference = -32768;
                int actual = ua_ring[(begin+outputs)&(UA_N-1)][c];
                int delta = actual-reference; if (delta < 0) delta = -delta;
                if (delta > max_delta) max_delta = delta;
                changed += delta != 0; compared++;
                assert(delta <= 1);
            }
            outputs++; phase += 147;
        } while (phase < 160);
        phase -= 160;
        assert(ua_w-begin == outputs && !uac.overruns);
        ua_r = ua_w;
    }
    printf("Float vs pre-float integer FIR: %u samples, max difference %d PCM16 count, %u changed\n", compared, max_delta, changed);
    return 0;
}
