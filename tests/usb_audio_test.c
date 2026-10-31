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

static void playback(void)
{
    const uint8_t *d; uint16_t size; unsigned terminal = 0, endpoint = 0;
    assert(get_desc(0x200, &d, &size));
    for (unsigned p = 0; p < size; p += d[p]) {
        if (d[p] == 12 && d[p+1] == 0x24 && d[p+2] == 2 && d[p+3] == 3) {
            assert(d[p+7] == UO_CHANNELS && d[p+8] == UO_CHANNEL_CONFIG && d[p+10] == UO_CHANNEL_STRING); terminal++;
        }
        if (d[p+1] == 5 && d[p+2] == 4) {
            assert((d[p+4] | d[p+5]<<8) == UO_MAXP); endpoint++;
        }
    }
    assert(terminal == 1 && endpoint == 1);
#ifdef FELUCCA_UAC_OUT_CHANNEL_NAMES
    for (unsigned c = 0; c < UO_CHANNELS; c++) {
        assert(get_desc(0x300 + UO_CHANNEL_STRING + c, &d, &size));
        assert(size == 2 + 2*strlen(uo_channel_names[c]));
        for (unsigned j = 0; j < strlen(uo_channel_names[c]); j++)
            assert(d[2+j*2] == uo_channel_names[c][j] && d[3+j*2] == 0);
    }
#endif
    memset(&uo, 0, sizeof uo); usb.config = 1; uo.alt = 1;
    /* Exercise the real endpoint receive path over the ring boundary. */
    uo_w = uo_r = UO_N-20;
    for (unsigned i = 0; i < 45; i++) for (unsigned c = 0; c < UO_CHANNELS; c++)
        ep4rx[i*UO_CHANNELS+c] = (int16_t)(i+1000*c);
    regs[4][S_RXCSR1] = 1; regs[4][S_RXCSR2] = 0;
    regs[4][S_RXCOUNT1] = (45*UO_FRAME_BYTES)&255;
    regs[4][S_RXCOUNT2] = (45*UO_FRAME_BYTES)>>8;
    uo_service();
    assert(uo_w == UO_N+25 && uo.frames == 45);
    for (unsigned i = 0; i < 45; i++) for (unsigned c = 0; c < UO_CHANNELS; c++)
        assert(uo_ring[(UO_N-20+i)&(UO_N-1)][c] == (int16_t)(i+1000*c));
    regs[4][S_RXCSR1] = 1; uo_r = uo_w-UO_N;
    uo_service(); assert(uo.overruns == 1 && uo_w == UO_N+25);
    /* Shared half-frame interpolation: one active input at a time, across wrap. */
    for (unsigned active = 0; active < UO_CHANNELS; active++) {
        memset(uo_ring, 0, sizeof uo_ring);
        uo_r = UO_N-20; uo_w = uo_r+UO_TGT+13;
        uo.go = 1; uo.pos = 32768; uo.err = 13; /* step exactly 65536 */
        for (unsigned i = 0; i < UO_TGT+13; i++)
            uo_ring[(uo_r+i)&(UO_N-1)][active] = (int16_t)(-16000+100*i);
        int32_t output[64*UO_CHANNELS] = {0};
        uac_play(output, 64, 4096);
        for (unsigned i = 0; i < 64; i++) for (unsigned c = 0; c < UO_CHANNELS; c++)
            assert(output[i*UO_CHANNELS+c] == (c == active ? (-16000+(int)i*100+50)*256 : 0));
        assert(uo_r == UO_N+44 && uo.pos == 32768);
    }
    /* Unity callback input remains within signed24bit at both PCM16 rails. */
    for (unsigned rail = 0; rail < 2; rail++) {
        uo_r = 0; uo_w = UO_TGT+13; uo.go = 1; uo.pos = 0; uo.err = 13;
        int16_t sample = rail ? 32767 : -32768;
        for (unsigned i = 0; i < UO_N; i++) for (unsigned c = 0; c < UO_CHANNELS; c++)
            uo_ring[i][c] = sample;
        int32_t output[64*UO_CHANNELS] = {0}; uac_play(output,64,4096);
        for (unsigned i = 0; i < 64*UO_CHANNELS; i++) assert(output[i] == (int32_t)sample*256);
    }
    int32_t silence[64*UO_CHANNELS] = {0};
    uo_w = uo_r; uac_play(silence, 64, 4096);
    assert(!uo.go && uo.underruns == 1);
    for (unsigned i = 0; i < 64*UO_CHANNELS; i++) assert(silence[i] == 0);
}

int main(void)
{
    descriptors(); interleaving(); full_render_blocks(); playback();
    printf("USB audio %d capture/%d playback, half=%u, deep=%d: descriptors, names, routing, isolation, wrap, underrun OK\n", UA_CHANNELS, UO_CHANNELS, HALF_FRAMES, FELUCCA_UAC_DEEP);
    return 0;
}
