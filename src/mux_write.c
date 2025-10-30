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
*   Email: [license@chili-iptv.info]  
*   Website: [www.chili-iptv.info]
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
