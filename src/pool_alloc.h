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

/*
 * pool_alloc.h
 *
 * Small thread-safe pool allocator for frequently-requested buffer sizes.
 * Intended for short-lived index/palette buffers used in subtitle conversion.
 */
#ifndef SRT2DVB_POOL_ALLOC_H
#define SRT2DVB_POOL_ALLOC_H

#include <stddef.h>


/*
 * @brief Allocates a memory block of the specified size from a memory pool.
 *
 * This function attempts to allocate a block of memory of at least `size` bytes
 * from an internal memory pool. The memory pool may provide faster allocation
 * and deallocation compared to standard heap allocation.
 *
 * @param size The size in bytes of the memory block to allocate.
 * @return A pointer to the allocated memory block, or NULL if allocation fails.
 */
void *pool_alloc(size_t size);


/*
 * @brief Frees a memory block previously allocated from a pool.
 *
 * This function releases a memory block pointed to by @p ptr of size @p size
 * back to the memory pool. The memory must have been allocated using the
 * corresponding pool allocation function.
 *
 * @param ptr Pointer to the memory block to be freed.
 * @param size Size of the memory block to be freed, in bytes.
 */
void pool_free(void *ptr, size_t size);


/*
 * @brief Destroys the memory pool and releases all allocated resources.
 *
 * This function should be called when the memory pool is no longer needed.
 * After calling this function, any pointers previously allocated from the pool
 * become invalid.
 */
void pool_destroy(void);

#endif /* SRT2DVB_POOL_ALLOC_H */
