/* SPDX-License-Identifier: MPL-2.0
 * Compile unchanged against both the upstream and current usb.c. Emit the
 * complete default descriptors and deterministic stereo packet stream. */
#define TEST_UAC 1
#define TEST_HOST 0
#define main usb_host_model_main
#include "usb_host_test.c"
#undef main
int main(void)
{
    const uint8_t *d; uint16_t length;
    const unsigned ids[] = {0x100, 0x200, 0x300, 0x301, 0x302};
    for (unsigned cdc = 0; cdc < 2; cdc++) {
        usb_cdc_on = cdc;
        for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
            assert(get_desc(ids[i], &d, &length));
            fwrite(&length, sizeof length, 1, stdout); fwrite(d, 1, length, stdout);
        }
    }
    memset(&uac, 0, sizeof uac); ua_w = ua_r = 0;
    usb.config = 1; uac_stream(1); uac_rate_set(44100);
    uac.flowing = 1; uac_render_start();
    uint32_t seed = 7;
    for (unsigned block = 0; block < 200; block++) {
        int32_t samples[256];
        for (unsigned i = 0; i < 256; i++) {
            seed = seed * 1664525u + 1013904223u;
            samples[i] = (int32_t)(seed & 0x1ffffffu) - 0xffffff;
        }
        uac_tap(samples, 128);
        /* Include fill correction, overflow, wrap, silence, partial/empty reads. */
        uac.fill_min = block % 3 == 0 ? 0 : block % 3 == 1 ? UA_HI+1 : UA_LO;
        for (unsigned packet = 0; packet < block % 5; packet++) {
            uint32_t n = uac_packet((void *)ep4tx);
            fwrite(&n, sizeof n, 1, stdout); fwrite(ep4tx, 4, n, stdout);
        }
    }
    fwrite(&uac.underruns, sizeof uac.underruns, 1, stdout);
    fwrite(&uac.overruns, sizeof uac.overruns, 1, stdout);
    return ferror(stdout) ? 1 : 0;
}
