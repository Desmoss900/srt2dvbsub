/*
* Copyright (c) 2025 Mark E. Rosche, Capsaworks Project
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
*   Mark E. Rosche, Capsaworks Project
*   Email: license@capsaworks-project.de
*   Website: www.capsaworks-project.de
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
