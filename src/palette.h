#pragma once
#include <stdint.h>

// Initialize one of the predefined palettes (broadcast, greyscale, ebu-broadcast).
// pal must be at least 16 entries long.
void init_palette(uint32_t *pal, const char *mode);

// Find nearest palette index to a given ARGB color.
int nearest_palette_index(uint32_t *palette, int npal, uint32_t argb);