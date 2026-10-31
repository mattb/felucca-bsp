/* SPDX-License-Identifier: MPL-2.0
 * Reuse the register stand-ins; exercise the production descriptors and capture
 * functions, not a replacement packetizer. No USB hardware is used. */
#define TEST_UAC 1
#define TEST_HOST 0
#define main usb_host_model_main
#include "usb_host_test.c"
#undef main

static void descriptors(void)
{
    const uint8_t *d; uint16_t n;
    unsigned terminal = 0, format = 0, endpoint = 0;
    assert(get_desc(0x200, &d, &n));
    for (unsigned p = 0; p < n; p += d[p]) {
        assert(d[p] >= 2 && p + d[p] <= n);
        if (d[p] == 12 && d[p+1] == 0x24 && d[p+2] == 2 && d[p+3] == 1) {
            assert(d[p+7] == UA_CHANNELS && d[p+8] == UA_CHANNEL_CONFIG && d[p+10] == UA_CHANNEL_STRING); terminal++;
        }
        if (d[p+1] == 0x24 && d[p+2] == 2 && d[p+3] == 1 && d[p] == FMT_LEN) {
            /* Playback format is always stereo; only inspect our capture format. */
            if (d[p+4] == UA_CHANNELS && d[p+7] == (FELUCCA_UAC_48K ? 2 : 1)) format++;
        }
        if (d[p+1] == 5 && d[p+2] == 0x84) {
            assert((d[p+4] | d[p+5]<<8) == UAC_MAXP); endpoint++;
        }
    }
    assert(terminal == 1 && format && endpoint == 1);
    assert(sizeof ep4tx == UAC_MAXP);
#ifdef FELUCCA_UAC_CHANNEL_NAMES
    for (unsigned c = 0; c < UA_CHANNELS; c++) {
        assert(get_desc(0x300 + UA_CHANNEL_STRING + c, &d, &n));
        assert(n == 2 + 2 * strlen(ua_channel_names[c]) && d[1] == 3);
        for (unsigned j = 0; j < strlen(ua_channel_names[c]); j++)
            assert(d[2+j*2] == ua_channel_names[c][j] && d[3+j*2] == 0);
    }
#endif
}
static void reset_capture(int rate48)
{
    memset(&uac, 0, sizeof uac); ua_w = ua_r = 0;
    usb.config = 1; uac_stream(1); uac_rate_set(rate48 ? 48000 : 44100);
    uac.flowing = 1; uac_render_start();
    assert(uac.go && uac.ring48 == rate48);
    /* Discard the intentional initial silence so checks start at test input. */
    ua_r = ua_w;
}
static void interleaving(void)
{
    int32_t input[128 * UA_CHANNELS];
    reset_capture(0);
    for (unsigned i = 0; i < 128; i++) for (unsigned c = 0; c < UA_CHANNELS; c++)
        input[i*UA_CHANNELS+c] = (int32_t)(1000 * (c+1) + i) * 256;
    uac_tap(input, 128); uac.fill_min = (UA_LO + UA_HI)/2;
    for (unsigned p = 0; p < 2; p++) {
        uint32_t r = ua_r, n = uac_packet(ep4tx);
        assert(n == 44 || n == 45);
        for (unsigned i = 0; i < n; i++) for (unsigned c = 0; c < UA_CHANNELS; c++)
            assert(ep4tx[i*UA_CHANNELS+c] == (int32_t)(1000*(c+1) + r + i - UA_PRIME));
    }
    /* Every channel repeats its own last sample on underrun. */
    ua_r = ua_w; uac_packet(ep4tx);
    for (unsigned i = 0; i < 44; i++) for (unsigned c = 0; c < UA_CHANNELS; c++)
        assert(ep4tx[i*UA_CHANNELS+c] == uac.last[c]);
    assert(uac.underruns == 1);
}
static void full_render_blocks(void)
{
    int32_t input[HALF_FRAMES * UA_CHANNELS];
    reset_capture(0);
    /* Retain real priming on the first block to exercise capacity admission. */
    ua_r = 0;
    uint32_t start = ua_w;
    for (unsigned i = 0; i < HALF_FRAMES; i++) for (unsigned c = 0; c < UA_CHANNELS; c++)
        input[i*UA_CHANNELS+c] = (int32_t)(1000*(c+1))*256;
    for (unsigned block = 0; block < 40; block++) {
        uint32_t begin = ua_w;
        uac_tap(input, HALF_FRAMES);
        assert(!uac.overruns);
        assert(ua_w-start == (block+1)*HALF_FRAMES);
        for (uint32_t i = begin; i < ua_w; i++)
            for (unsigned c = 0; c < UA_CHANNELS; c++)
                assert(ua_ring[i&(UA_N-1)][c] == (int32_t)(1000*(c+1)));
        ua_r = ua_w;
    }
    /* Insufficient capacity drops the entire block without partial publication. */
    uint32_t before = ua_w;
    ua_r = ua_w - UA_N;
    uac_tap(input, HALF_FRAMES);
    assert(ua_w == before && uac.overruns == 1);
}

int main(void)
{
    descriptors(); interleaving(); full_render_blocks();
    printf("USB audio %d channels, 48k=%d, half=%u, deep=%d: descriptors, names, interleave, underrun, isolation OK\n", UA_CHANNELS, FELUCCA_UAC_48K, HALF_FRAMES, FELUCCA_UAC_DEEP);
    return 0;
}
