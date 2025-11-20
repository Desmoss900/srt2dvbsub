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

#define _POSIX_C_SOURCE 200809L
#include "runtime_opts.h"

/*
 * runtime_opts.c
 * ----------------
 * Provide default values for the small set of process-global runtime
 * configuration options used by the application. These defaults are
 * intentionally conservative and chosen to produce good visual quality on
 * modern x86_64 desktops while remaining usable on embedded devices.
 *
 * The variables are declared `extern` in `runtime_opts.h` and live for the
 * lifetime of the process. Command-line parsing in `main.c` may override
 * these values early in program startup.
 */

/* Number of encoder threads to use. 0 delegates thread count to the
 * underlying encoder (libavcodec) default. Increasing this can improve
 * encode throughput on multi-socket/multicore systems but may consume
 * significant CPU resources. */
int enc_threads = 0;

/* Number of renderer worker threads. This controls the parallelism used by
 * the rendering pool. Typical values are between 1 and the number of CPU
 * cores; values much larger than the core count may increase context
 * switching and memory pressure. */
int render_threads = 8;

/* Supersampling factor applied when rasterizing text with Pango/Cairo.
 * Higher values reduce aliasing and preserve small glyph details at the
 * cost of CPU and memory. A value of 1 disables supersampling. */
int ssaa_override = 4; /* default to 4x supersampling for smoother edges */

/* Disable the unsharp mask pass when non-zero. Useful for debugging or on
 * platforms where the unsharp kernel causes unacceptable haloing. */
int no_unsharp = 0;

/* Global variable to control the verbosity of debug output.
 * A higher value increases the amount of debug information printed.
 * Default is 0 (no debug output).
 */
int debug_level = 0;

/* 
 * Flag indicating whether ASS (Advanced SubStation Alpha) subtitle support is enabled.
 * 0 means ASS support is disabled; non-zero values enable ASS support.
 */
int use_ass = 0;

/* Width of the video in pixels. Default value is set to 720. */
int video_w = 720;

/* Height of the video in pixels. Default value is set to 480*/
int video_h = 480;
