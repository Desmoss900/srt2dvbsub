/*
* Copyright (c) 2025 Mark E. Rosche, Chili IPTV Systems
* All rights reserved.
*
* PERSONAL USE LICENSE - NON-COMMERCIAL ONLY
* ────────────────────────────────────────────────────────────────
* This software is provided for personal, educational, and non-commercial
* use only. You are granted permission to use, copy, and modify this
* software for your own personal or educational purposes, provided that
* this copyright and license notice appears in all copies or substantial
* portions of the software.
*
* PERMITTED USES:
*   ✓ Personal projects and experimentation
*   ✓ Educational purposes and learning
*   ✓ Non-commercial testing and evaluation
*   ✓ Individual hobbyist use
*
* PROHIBITED USES:
*   ✗ Commercial use of any kind
*   ✗ Incorporation into products or services sold for profit
*   ✗ Use within organizations or enterprises for revenue-generating activities
*   ✗ Modification, redistribution, or hosting as part of any commercial offering
*   ✗ Licensing, selling, or renting this software to others
*   ✗ Using this software as a foundation for commercial services
*
* No commercial license is available. For inquiries regarding any use not
* explicitly permitted above, contact:
*   Mark E. Rosche, Chili IPTV Systems
*   Email: license@chili-iptv.de
*   Website: www.chili-iptv.de
*
* ────────────────────────────────────────────────────────────────
* DISCLAIMER
* ────────────────────────────────────────────────────────────────
* THIS SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
* OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
* DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
* ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*
* ────────────────────────────────────────────────────────────────
* By using this software, you agree to these terms and conditions.
* ────────────────────────────────────────────────────────────────
*/

#pragma once
#include <stdint.h>

/**
 * @file palette.h
 * @brief Palette utilities for subtitle rendering (ARGB32 palettes).
 *
 * The rendering pipeline uses 32-bit ARGB palette entries (0xAARRGGBB) when
 * converting RGBA render targets into constrained broadcast palettes. This
 * header exposes helpers to initialize common palettes and to find the
 * nearest palette index for an arbitrary ARGB color.
 *
 * Palettes are arrays of `uint32_t` entries in host endianness. Many DVB
 * modes require at least 16 entries; callers should allocate sufficient
 * space for the intended mode.
 *
 * Palette contract (important): index 0 is reserved for transparency.
 * The implementation of `init_palette()` guarantees that `pal[0]` will
 * have an alpha value of 0 (fully transparent, e.g. 0x00RRGGBB). Callers
 * should rely on this convention when treating index 0 as transparent
 * when composing or encoding DVB bitmaps.
 *
 * Example:
 * @code
 *   uint32_t pal[16];
 *   init_palette(pal, "broadcast");
 *   int idx = nearest_palette_index(pal, 16, 0xFF112233);
 * @endcode
 */

/**
 * Initialize a predefined palette into `pal`.
 *
 * @param pal  Writable pointer to an array of 32-bit ARGB entries. Must be
 *             large enough for the chosen palette (minimum 16 entries).
 * @param mode Case-insensitive palette mode string (e.g. "broadcast",
 *             "greyscale", "ebu-broadcast"). If NULL or unrecognized,
 *             a sensible default palette is chosen.
 *
 * Notes:
 *  - This function does not return an error code. Callers must allocate
 *    sufficient storage for the expected palette size.
 *
 * Contract:
 *  - `pal` must be writable and have space for at least 16 entries for
 *    common DVB palettes. `init_palette()` will set `pal[0]` to a fully
 *    transparent ARGB entry (alpha == 0) so callers may treat index 0
 *    as transparent without additional checks.
 */
void init_palette(uint32_t *pal, const char *mode);

/**
 * Find the nearest palette index for an ARGB color.
 *
 * @param palette Pointer to `npal` ARGB palette entries.
 * @param npal    Number of entries in `palette`. Must be > 0.
 * @param argb    Input color in 0xAARRGGBB format.
 * @return Index in range [0, npal-1] of the nearest palette entry, or -1
 *         when `npal` <= 0.
 *
 * The distance metric accounts for renderer expectations (premultiplied or
 * linear RGB) and handles alpha correctly; callers may pass a non-premultiplied
 * color and the implementation will perform the appropriate comparison.
 */
int nearest_palette_index(uint32_t *palette, int npal, uint32_t argb);