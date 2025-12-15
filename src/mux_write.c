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

/* Thread-safe wrapper implementation for av_interleaved_write_frame. */
#define _POSIX_C_SOURCE 200809L
#include "mux_write.h"
#include <pthread.h>
#include <libavformat/avformat.h>

#ifndef ENABLE_THREAD_SAFE_MUX
#define ENABLE_THREAD_SAFE_MUX 1
#endif

#if ENABLE_THREAD_SAFE_MUX
static pthread_mutex_t write_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

/*
 * Writes an AVPacket to the given AVFormatContext in a thread-safe manner.
 * This function locks a mutex before calling av_interleaved_write_frame to ensure
 * that only one thread can write to the context at a time, preventing race conditions.
 *
 * Parameters:
 *   s   - Pointer to the AVFormatContext where the packet will be written.
 *   pkt - Pointer to the AVPacket to be written.
 *
 * Returns:
 *   The return value from av_interleaved_write_frame, indicating success or failure.
 */
int safe_av_interleaved_write_frame(AVFormatContext *s, AVPacket *pkt) {
    int ret;
#if ENABLE_THREAD_SAFE_MUX
    pthread_mutex_lock(&write_mutex);
#endif
    ret = av_interleaved_write_frame(s, pkt);
#if ENABLE_THREAD_SAFE_MUX
    pthread_mutex_unlock(&write_mutex);
#endif
    return ret;
}
