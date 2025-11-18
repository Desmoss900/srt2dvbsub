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

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include <libavutil/avutil.h>

#include "progress.h"



/*
 * emit_progress
 *
 * Helper function to emit progress line respecting debug_level and
 * common throttle. Consolidates duplicate progress formatting code
 * from two reporting sites (packet-count and subtitle-count events)
 * into a single function, reducing code duplication and making it
 * easier to tune frequency/format globally without flicker.
 *
 * Returns: 1 if progress was emitted, 0 if throttled or debug_level != 0.
 *
 * Progress line format:
 *   - With duration: "Progress: %5.1f%% subs=%ld elapsed=%02d:%02d ETA=%02d:%02d"
 *   - Without duration (pkt variant): "Progress: pkt=%ld subs=%ld elapsed=%02d:%02d"
 *   - Without duration (subs variant): "Progress: subs=%ld elapsed=%02d:%02d"
 *
 * Note: Uses 80-char line with carriage returns (\r%s\r) to overwrite
 * previous progress; padding with spaces ensures no terminal artifacts
 * from variable-length strings.
 *
 * @param use_pkt_count: If 1, include pkt count in fallback format; if 0, omit it.
 */
int emit_progress(int debug_level,
                        time_t now,
                        time_t prog_start_time,
                        time_t *last_progress_time,
                        long pkt_count,
                        long subs_emitted,
                        int64_t total_duration_pts90,
                        int64_t input_start_pts90,
                        int64_t last_valid_cur90,
                        int use_pkt_count)
{
    if (debug_level != 0)
        return 0; /* only emit when debug_level == 0 */

    if (now - *last_progress_time < 1)
        return 0; /* throttle to 1-second intervals */

    double elapsed = difftime(now, prog_start_time);
    int mins = (int)(elapsed / 60.0);
    int secs = (int)(elapsed) % 60;

    char line[81];
    int n = 0;

    if (total_duration_pts90 != AV_NOPTS_VALUE && total_duration_pts90 > 0 && last_valid_cur90 != AV_NOPTS_VALUE)
    {
        double pct = (double)(last_valid_cur90 - input_start_pts90) / (double)total_duration_pts90;
        if (pct < 0.0) pct = 0.0;
        if (pct > 1.0) pct = 1.0;
        double eta = 0.0;
        if (pct > 0.001)
        {
            double total_est = elapsed / pct;
            eta = total_est - elapsed;
        }
        int eta_m = (int)(eta / 60.0);
        int eta_s = (int)(eta) % 60;

        n = snprintf(line, sizeof(line), "Progress: %5.1f%% subs=%ld elapsed=%02d:%02d ETA=%02d:%02d", 
                     pct * 100.0, subs_emitted, mins, secs, eta_m, eta_s);
    }
    else
    {
        if (use_pkt_count)
            n = snprintf(line, sizeof(line), "Progress: pkt=%ld subs=%ld elapsed=%02d:%02d", 
                         pkt_count, subs_emitted, mins, secs);
        else
            n = snprintf(line, sizeof(line), "Progress: subs=%ld elapsed=%02d:%02d", 
                         subs_emitted, mins, secs);
    }

    if (n < 0)
        n = 0;
    if (n >= (int)sizeof(line))
        n = (int)sizeof(line) - 1;

    /* Pad with spaces to erase previous content, ensuring no terminal artifacts */
    memset(line + n, ' ', sizeof(line) - n - 1);
    line[sizeof(line) - 1] = '\0';

    fprintf(stdout, "\r%s\r", line);
    fflush(stdout);

    *last_progress_time = now;
    return 1;
}