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

#define _POSIX_C_SOURCE 200809L
#include <stddef.h>
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

/* Comma-separated list of custom PIDs for DVB subtitle tracks.
 * NULL/empty means use default auto-assignment. Single value means auto-increment.
 * Multiple values should be comma-separated. */
char *pid_list = NULL;

/* Controls how the muxer bitrate should be configured (default: leave unset). */
TsBitrateMode ts_bitrate_mode = TS_BITRATE_MODE_UNSPECIFIED;

/* Override bitrate for MPEG-TS output (muxrate) in bits per second when the
 * mode is TS_BITRATE_MODE_FIXED. Example values: 12000000 (12 Mbps), 8000000
 * (8 Mbps), etc. */
int64_t ts_bitrate = 0;

/* Enable PNG-only mode: render subtitles to PNG files without MPEG-TS output.
 * 0 = disabled (normal MPEG-TS generation), 1 = PNG-only mode.
 * When enabled, no MPEG-TS file is produced; only PNG images are saved. */
int png_only = 0;

/* Subtitle positioning specification: comma-separated per-track positioning configs.
 * Format: "position[,margins];position[,margins];..."
 * Example: "bottom-center,5.0;top-left,3.0,2.0"
 * NULL means use default (bottom-center with 5% margin for all tracks) */
char *sub_position_spec = NULL;

/* Per-track subtitle positioning configurations (max 8 tracks).
 * Initialized with defaults and populated from sub_position_spec during setup. */
SubtitlePositionConfig sub_pos_configs[8] = {
    [0 ... 7] = {
        .position = SUB_POS_BOT_CENTER,
        .margin_top = 3.5,
        .margin_left = 2.0,
        .margin_bottom = 3.5,
        .margin_right = 2.0
    }
};
