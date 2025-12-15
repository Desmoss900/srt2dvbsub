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
