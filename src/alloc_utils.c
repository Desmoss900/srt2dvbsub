/*  
* Copyright (c) 2025 Mark E. Rosche, Chili IPTV Systems
* All rights reserved.
*
* This software is licensed under the "Personal Use License" described below.
*
* ────────────────────────────────────────────────────────────────
* PERSONAL USE LICENSE
* ────────────────────────────────────────────────────────────────
* Permission is hereby granted, free of charge, to any individual person
* using this software for personal, educational, or non-commercial purposes,
* to use, copy, modify, merge, publish, and/or build upon this software,
* provided that this copyright and license notice appears in all copies
* or substantial portions of the Software.
*
* ────────────────────────────────────────────────────────────────
* COMMERCIAL USE
* ────────────────────────────────────────────────────────────────
* Commercial use of this software, including but not limited to:
*   • Incorporation into a product or service sold for profit,
*   • Use within an organization or enterprise in a revenue-generating activity,
*   • Modification, redistribution, or hosting as part of a commercial offering,
* requires a separate **Commercial License** from the copyright holder.
*
* To obtain a commercial license, please contact:
*   [Mark E. Rosche | Chili-IPTV Systems]
*   Email: [license@chili-iptv.de]  
*   Website: [www.chili-iptv.de]
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
* Summary:
*   ✓ Free for personal, educational, and hobbyist use.
*   ✗ Commercial use requires a paid license.
* ────────────────────────────────────────────────────────────────
*/

/* alloc_utils.c
 *
 * Centralized implementations for small allocation helpers.
 */
#define _POSIX_C_SOURCE 200809L
#include "alloc_utils.h"
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <libavutil/mem.h>

/*
 * Allocates a zero-initialized memory block for an array.
 *
 * This function safely allocates memory for an array of `nmemb` elements,
 * each of size `elsize`, using `av_mallocz`. It returns a pointer to the
 * allocated memory, or NULL if either `nmemb` or `elsize` is zero, or if
 * the allocation would overflow `SIZE_MAX`.
 *
 * Parameters:
 *   nmemb  - Number of elements to allocate.
 *   elsize - Size of each element in bytes.
 *
 * Returns:
 *   Pointer to the allocated zero-initialized memory, or NULL on failure.
 */
void *safe_av_mallocz_array(size_t nmemb, size_t elsize) {
    if (nmemb == 0 || elsize == 0)
        return NULL;
    if (elsize != 0 && nmemb > SIZE_MAX / elsize)
        return NULL;
    return av_mallocz(nmemb * elsize);
}
