#include "../firmware/camera/include/hb_raw_frame.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    // A synthetic BGR sensor frame with independent horizontal red / vertical
    // green gradients makes direction and RGB channel order unambiguous.
    enum { W = 64, H = 32, OW = 16, OH = 8 };
    uint8_t sensor[W * H * 3];
    for (unsigned y = 0; y < H; ++y)
        for (unsigned x = 0; x < W; ++x) {
            uint8_t *p = sensor + (y * W + x) * 3;
            p[0] = 17;                // blue
            p[1] = (uint8_t)(y * 7);  // green, top -> bottom
            p[2] = (uint8_t)(x * 4);  // red, left -> right
        }
    uint8_t photo[OW * OH * 3] = {0};
    uint16_t preview[OW * OH] = {0};
    assert(hb_rgb888_thumbnail_clockwise(sensor, W, H, photo, OW, OH));
    assert(hb_rgb888_preview_clockwise(sensor, W, H, preview, OW, OH));
    const unsigned top_left = 0, top_right = (OW - 1) * 3;
    const unsigned bottom_left = (OH - 1) * OW * 3;
    assert(photo[top_left] < photo[top_right]);        // right is redder
    assert(photo[top_left + 1] < photo[bottom_left + 1]); // bottom greener
    assert(photo[2] == 17);                           // BGR -> RGB
    // RGB565 preview must represent the same physical locations as the JPEG.
    const uint16_t first_expected =
        ((photo[0] >> 3) << 11) | ((photo[1] >> 2) << 5) | (photo[2] >> 3);
    assert(preview[0] == first_expected);
    assert((preview[OW - 1] >> 11) > (preview[0] >> 11));
    assert(((preview[(OH - 1) * OW] >> 5) & 63) > ((preview[0] >> 5) & 63));
    assert(!hb_rgb888_preview_clockwise(0, W, H, preview, OW, OH));
    assert(!hb_rgb888_thumbnail_clockwise(sensor, W, H, 0, OW, OH));
    puts("Camera clockwise orientation and color tests passed");
    return 0;
}
