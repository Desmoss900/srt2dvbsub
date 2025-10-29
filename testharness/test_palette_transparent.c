#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include "../src/palette.h"

int main(void) {
    uint32_t pal[16];

    init_palette(pal, "greyscale");
    assert(pal[0] == 0x00000000);

    init_palette(pal, "broadcast");
    assert(pal[0] == 0x00000000);

    init_palette(pal, "ebu-broadcast");
    assert(pal[0] == 0x00000000);

    init_palette(pal, NULL);
    assert(pal[0] == 0x00000000);

    printf("test_palette_transparent: palette[0] is transparent for all modes\n");
    return 0;
}
