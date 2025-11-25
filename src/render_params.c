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

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include "render_params.h"

/**
 * Helper: Check if character is valid hex digit
 */
static int is_hex_digit(char c)
{
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

/**
 * validate_fontsize
 * 
 * Parse font size with robust error handling. Accepts 0 (adaptive) or 6-200 (fixed).
 * Uses strtol() instead of atoi() to detect parsing errors.
 */
int validate_fontsize(const char *fontsize_str, int *out_fontsize, char *errmsg)
{
    if (!fontsize_str || !out_fontsize || !errmsg)
        return -1;
    
    /* Empty string */
    if (fontsize_str[0] == '\0') {
        snprintf(errmsg, 256, "Font size cannot be empty");
        return -1;
    }
    
    /* Attempt to parse with strtol */
    errno = 0;
    char *endptr = NULL;
    long val = strtol(fontsize_str, &endptr, 10);
    
    /* Check for conversion errors */
    if (errno != 0) {
        snprintf(errmsg, 256, "Font size parsing error: %s", strerror(errno));
        return -1;
    }
    
    /* Check that entire string was consumed (no leftover characters) */
    if (*endptr != '\0') {
        snprintf(errmsg, 256, "Font size must be numeric (got: %s)", fontsize_str);
        return -1;
    }
    
    /* Verify value is within acceptable range */
    if (val == 0) {
        /* Special case: 0 means adaptive sizing based on display height */
        *out_fontsize = 0;
        return 0;
    }
    
    if (val < 6) {
        snprintf(errmsg, 256, "Font size must be 0 (adaptive) or 6-200 points (got: %ld)", val);
        return -1;
    }
    
    if (val > 200) {
        snprintf(errmsg, 256, "Font size must be 0 (adaptive) or 6-200 points (got: %ld, max: 200)", val);
        return -1;
    }
    
    *out_fontsize = (int)val;
    return 0;
}

/**
 * validate_color
 * 
 * Validate color string format. Accepts #RRGGBB (6-char) or #AARRGGBB (8-char) only.
 * No silent fallback; invalid formats are reported to user.
 */
int validate_color(const char *color_str, char *errmsg)
{
    if (!color_str || !errmsg)
        return -1;
    
    /* Empty string */
    if (color_str[0] == '\0') {
        snprintf(errmsg, 256, "Color cannot be empty");
        return -1;
    }
    
    /* Must start with # */
    if (color_str[0] != '#') {
        snprintf(errmsg, 256, "Color must be in #RRGGBB or #AARRGGBB format (got: %s)", color_str);
        return -1;
    }
    
    size_t len = strlen(color_str);
    
    /* Check length: must be 7 (#RRGGBB) or 9 (#AARRGGBB) */
    if (len != 7 && len != 9) {
        snprintf(errmsg, 256, 
                 "Color must be 7 characters (#RRGGBB) or 9 characters (#AARRGGBB) (got %zu: %s)",
                 len, color_str);
        return -1;
    }
    
    /* Validate all hex digits after # */
    for (size_t i = 1; i < len; i++) {
        if (!is_hex_digit(color_str[i])) {
            snprintf(errmsg, 256, "Color must contain valid hex digits (got: %s)", color_str);
            return -1;
        }
    }
    
    /* Valid color format */
    return 0;
}

/**
 * get_fontsize_usage
 * 
 * Return formatted usage string for font size.
 */
const char *get_fontsize_usage(void)
{
    return "0 (adaptive) or 6-200 (fixed points)";
}

/**
 * get_color_usage
 * 
 * Return formatted usage string for color.
 */
const char *get_color_usage(void)
{
    return "#RRGGBB or #AARRGGBB format (hex RGB or ARGB)";
}
