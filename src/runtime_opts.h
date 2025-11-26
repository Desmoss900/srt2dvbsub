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

#include <stdint.h>

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

/**
 * @brief Comma-separated list of custom PIDs for DVB subtitle tracks.
 *
 * This external string variable specifies the PIDs (Program IDs) to be assigned
 * to subtitle tracks. It can be a single value (e.g., "150") for auto-increment,
 * or a comma-separated list (e.g., "150,151,152") for explicit PID assignment.
 * If NULL or empty, the default auto-assignment behavior is used.
 */
extern char *pid_list;

/**
 * @brief Override bitrate for MPEG-TS output in bits per second.
 *
 * This external 64-bit integer variable specifies a custom bitrate (muxrate)
 * for the output MPEG-TS file. If set to a value greater than 0, it overrides
 * the auto-calculated bitrate. If 0 or negative, the auto-calculated bitrate
 * from the input file is used. Typical values range from 1000000 (1 Mbps) to
 * 50000000 (50 Mbps) depending on content requirements.
 */
extern int64_t ts_bitrate;

/**
 * @brief Enable PNG-only output mode without MPEG-TS generation.
 *
 * When set to a non-zero value, the application renders subtitles to PNG files
 * in the directory specified by --png-dir without generating an MPEG-TS output file.
 * This mode is useful for visual inspection and quality control of rendered subtitles.
 * The --png-dir flag MUST be specified when using this mode.
 */
extern int png_only;

/**
 * @brief Canvas positioning enumeration for subtitle placement.
 *
 * Defines 9 cardinal and intercardinal positions on the video canvas.
 * Positions are arranged in a 3x3 grid:
 *   1=top-left,    2=top-center,    3=top-right
 *   4=mid-left,    5=mid-center,    6=mid-right
 *   7=bot-left,    8=bot-center,    9=bot-right (DEFAULT)
 */
typedef enum {
    SUB_POS_TOP_LEFT = 1,        /**< Top-left corner */
    SUB_POS_TOP_CENTER = 2,      /**< Top-center (horizontal middle, top edge) */
    SUB_POS_TOP_RIGHT = 3,       /**< Top-right corner */
    SUB_POS_MID_LEFT = 4,        /**< Middle-left (vertical middle, left edge) */
    SUB_POS_MID_CENTER = 5,      /**< Center of canvas */
    SUB_POS_MID_RIGHT = 6,       /**< Middle-right (vertical middle, right edge) */
    SUB_POS_BOT_LEFT = 7,        /**< Bottom-left corner */
    SUB_POS_BOT_CENTER = 8,      /**< Bottom-center (DEFAULT: horizontal middle, bottom edge) */
    SUB_POS_BOT_RIGHT = 9        /**< Bottom-right corner */
} SubtitlePosition;

/**
 * @brief Per-track subtitle positioning configuration.
 *
 * Stores positioning and margin settings for a single subtitle track.
 * Margins are percentages of canvas size (0-50% typical range).
 */
typedef struct {
    SubtitlePosition position;   /**< Canvas position (1-9) */
    double margin_top;           /**< Top margin as % of canvas height */
    double margin_left;          /**< Left margin as % of canvas width */
    double margin_bottom;        /**< Bottom margin as % of canvas height */
    double margin_right;         /**< Right margin as % of canvas width */
} SubtitlePositionConfig;

/**
 * @brief Comma-separated subtitle positioning specification.
 *
 * Format: "position[,margin_top,margin_left,margin_bottom,margin_right];position[,...]"
 * Example: "bottom-center,5.0;top-left,3.0,2.0"
 * If not specified, defaults to "bottom-center,5.0" for all tracks.
 */
extern char *sub_position_spec;

/**
 * @brief Array of per-track positioning configurations.
 *
 * Maximum 8 subtitle tracks supported. Indexed by track number.
 * Populated from sub_position_spec during initialization.
 */
extern SubtitlePositionConfig sub_pos_configs[8];

#endif /* SRT2DVB_RUNTIME_OPTS_H */
