/* SPDX-License-Identifier: MPL-2.0
 * Reuse the register stand-ins; exercise the production descriptors and capture
 * functions, not a replacement packetizer/resampler. No USB hardware is used. */
#include <math.h>
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
            assert((d[p+4] | d[p+5]<<8) == UAC_EP_MAXP); endpoint++;
        }
    }
    assert(terminal == 1 && format && endpoint == 1);
    assert(sizeof ep4tx == UAC_MAXP); /* reservation never grows the DMA payload */
    if (UA_CHANNELS <= 2) assert(UAC_EP_MAXP == UAC_MAXP);
    else assert(UAC_EP_MAXP * 6 >= UAC_MAXP * 7 && UAC_EP_MAXP % 4 == 0);
    if (UA_CHANNELS == 6 && FELUCCA_UAC_48K) assert(UAC_EP_MAXP == 688);
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
#if FELUCCA_UAC_48K
static void resampling(void)
{
    int32_t input[147 * UA_CHANNELS];
    for (unsigned selected = 0; selected < UA_CHANNELS; selected++) {
        reset_capture(1); uint32_t begin = ua_w;
        memset(input,0,sizeof input);
        for (unsigned i = 0; i < 147; i++) input[i*UA_CHANNELS+selected] = 4000*256;
        uac_tap(input,147);
        assert(ua_w-begin == 160); /* actual device-side 160/147 conversion */
        for (unsigned i = 50; i < 160; i++) for (unsigned c = 0; c < UA_CHANNELS; c++) {
            int v = ua_ring[(begin+i)&(UA_N-1)][c];
            assert(c == selected ? (v >= 3999 && v <= 4001) : v == 0);
        }
    }
}
static void full_render_blocks(void)
{
    int32_t input[HALF_FRAMES * UA_CHANNELS];
    reset_capture(1);
    /* Retain real priming on the first block to exercise capacity admission. */
    ua_r = 0;
    uint32_t start = ua_w;
    for (unsigned i = 0; i < HALF_FRAMES; i++) for (unsigned c = 0; c < UA_CHANNELS; c++)
        input[i*UA_CHANNELS+c] = (int32_t)(1000*(c+1))*256;
    for (unsigned block = 0; block < 40; block++) {
        uint32_t begin = ua_w;
        uac_tap(input, HALF_FRAMES);
        assert(!uac.overruns);
        assert(ua_w-start == ((block+1)*HALF_FRAMES*160u+146u)/147u);
        for (uint32_t i = begin + (block ? 0u : 50u); i < ua_w; i++)
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

static void resampler_signal(void)
{
    /* Packet/ring wrap and arbitrary app block boundaries must not change PCM. */
    int32_t input[147 * UA_CHANNELS];
    int16_t reference[160 * UA_CHANNELS];
    for (unsigned i = 0; i < 147; i++) for (unsigned c = 0; c < UA_CHANNELS; c++)
        input[i*UA_CHANNELS+c] = (int32_t)(12000*sin(i*.37+c))*256;
    reset_capture(1); uint32_t begin = ua_w;
    uac_tap(input,147);
    for (unsigned i = 0; i < 160; i++) for (unsigned c = 0; c < UA_CHANNELS; c++)
        reference[i*UA_CHANNELS+c] = ua_ring[(begin+i)&(UA_N-1)][c];
    reset_capture(1); ua_r = ua_w = UA_N-20; begin = ua_w;
    for (unsigned i = 0; i < 147; i++) uac_tap(input+i*UA_CHANNELS,1);
    assert(ua_w-begin == 160);
    for (unsigned i = 0; i < 160; i++) for (unsigned c = 0; c < UA_CHANNELS; c++)
        assert(reference[i*UA_CHANNELS+c] == ua_ring[(begin+i)&(UA_N-1)][c]);

    /* Measure the production resampler: 4.5 kHz tone and its 8.4 kHz image
     * (44.1-4.5 folded into 48k). Use whole periods after filter settling. */
    double real = 0, imag = 0, image_real = 0, image_imag = 0;
    reset_capture(1);
    for (unsigned block = 0; block < 21; block++) {
        for (unsigned i = 0; i < 147; i++) for (unsigned c = 0; c < UA_CHANNELS; c++)
            input[i*UA_CHANNELS+c] = (int32_t)(12000*sin(2*3.141592653589793*4500*(block*147+i)/44100))*256;
        begin = ua_w; uac_tap(input,147);
        assert(ua_w-begin == 160);
        if (block) for (unsigned i = 0; i < 160; i++) {
            unsigned index = (block-1)*160+i;
            double sample = ua_ring[(begin+i)&(UA_N-1)][0];
            double phase = 2*3.141592653589793*index/48000;
            real += sample*cos(4500*phase); imag += sample*sin(4500*phase);
            image_real += sample*cos(8400*phase); image_imag += sample*sin(8400*phase);
        }
        ua_r = ua_w;
    }
    double amplitude = hypot(real,imag)*2/3200;
    assert(amplitude > 11990 && amplitude < 12010);
    assert(hypot(image_real,image_imag)/hypot(real,imag) < .001); /* image below -60 dB */
}

#endif
int main(void)
{
    descriptors(); interleaving();
#if FELUCCA_UAC_48K
    resampling(); resampler_signal(); full_render_blocks();
#endif
    printf("USB audio %d channels, 48k=%d, half=%u, deep=%d: descriptors, names, interleave, underrun, isolation OK\n", UA_CHANNELS, FELUCCA_UAC_48K, HALF_FRAMES, FELUCCA_UAC_DEEP);
    return 0;
}
