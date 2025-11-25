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

#ifndef SRT2DVB_RUNTIME_OPTS_H
#define SRT2DVB_RUNTIME_OPTS_H

/**
 * @file runtime_opts.h
 * @brief Global runtime-configurable options used by the application.
 *
 * These globals are populated (with defaults) in `runtime_opts.c` and may be
 * overridden by command-line parsing in `main.c`. They are intentionally
 * simple globals to keep option access convenient across the codebase.
 *
 * Thread-safety: the variables are read-mostly after initialization. Any
 * runtime mutation must be synchronized by the caller.
 */

/**
 * @brief Number of encoder threads to be used.
 *
 * This external variable specifies how many threads should be allocated
 * for encoding operations. It can be set to optimize performance based
 * on available hardware resources.
 */
 extern int enc_threads;

/**
 * @brief Number of threads used for rendering operations.
 *
 * This external integer variable specifies how many threads
 * are allocated for rendering tasks. It can be set to optimize
 * performance based on available system resources.
 */
extern int render_threads;

/**
 * @brief Overrides the default SSAA (Super-Sampling Anti-Aliasing) setting.
 *
 * This external integer variable can be used to force a specific SSAA configuration
 * at runtime, bypassing the default or configured value.
 *
 * @note The exact effect depends on how this variable is used in the implementation.
 */
extern int ssaa_override;

/**
 * @brief Global flag to disable the unsharp filter.
 *
 * When set to a non-zero value, the unsharp filter will be disabled in the runtime.
 * This variable is typically set via command-line options or configuration files.
 */
extern int no_unsharp;

/**
 * @brief Global variable to control the level of debug output.
 *
 * The value of debug_level determines the verbosity of debug messages
 * throughout the application. Higher values enable more detailed logging.
 */
extern int debug_level;

/**
 * Indicates whether ASS (Advanced SubStation Alpha) subtitle support is enabled.
 * 
 * When set to a non-zero value, the application will use ASS subtitles.
 * When set to zero, ASS subtitles are disabled.
 */
extern int use_ass;

/**
 * @brief External variable representing the width of the video.
 *
 * This variable is declared as an external integer and is expected to be
 * defined elsewhere in the program. It typically holds the width (in pixels)
 * of the video being processed or displayed.
 */
extern int video_w;

/**
 * @brief Height of the video frame in pixels.
 *
 * This external integer variable specifies the vertical resolution (height)
 * of the video frame. It is typically set during runtime configuration and
 * used throughout the application wherever video frame dimensions are required.
 */
extern int video_h;

#endif /* SRT2DVB_RUNTIME_OPTS_H */
