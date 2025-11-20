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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <limits.h>

#include "png_path.h"

/**
 * Static module state: configured PNG output directory
 */
static char dbg_png_output_dir[PATH_MAX] = "pngs/";

/**
 * Helper: Check if directory exists and is writable
 */
static int is_dir_writable(const char *path)
{
    if (!path || path[0] == '\0')
        return 0;
    
    if (access(path, F_OK) != 0)
        return 0;  /* Does not exist */
    
    if (access(path, W_OK) != 0)
        return 0;  /* Not writable */
    
    struct stat st;
    if (stat(path, &st) != 0)
        return 0;  /* Cannot stat */
    
    return S_ISDIR(st.st_mode);  /* Is a directory */
}

/**
 * Helper: Attempt to create directory with mkdir, creating parents if needed
 * Returns 0 on success (directory exists and is writable), -1 on failure
 */
static int ensure_directory(const char *path, char *errmsg)
{
    if (!path || path[0] == '\0') {
        snprintf(errmsg, 256, "Directory path is empty");
        return -1;
    }
    
    /* Make a copy so we can modify it */
    char path_copy[PATH_MAX];
    if (strlen(path) >= sizeof(path_copy)) {
        snprintf(errmsg, 256, "Directory path too long (max %zu)", sizeof(path_copy) - 1);
        return -1;
    }
    strcpy(path_copy, path);
    
    /* Remove trailing slash if present */
    size_t len = strlen(path_copy);
    if (len > 0 && path_copy[len - 1] == '/') {
        path_copy[len - 1] = '\0';
    }
    
    /* Check if already exists and writable */
    if (is_dir_writable(path_copy)) {
        return 0;  /* Already exists and writable */
    }
    
    /* Try to create the directory */
    if (mkdir(path_copy, 0755) == 0) {
        return 0;  /* Successfully created */
    }
    
    if (errno == EEXIST) {
        /* Directory exists; check if writable */
        if (is_dir_writable(path_copy)) {
            return 0;
        }
        snprintf(errmsg, 256, "Directory exists but not writable: %s", path_copy);
        return -1;
    }
    
    /* mkdir failed */
    snprintf(errmsg, 256, "Cannot create directory %s: %s", path_copy, strerror(errno));
    return -1;
}

/**
 * init_png_path
 *
 * Initialize PNG output directory with fallback to /tmp if needed
 */
int init_png_path(const char *custom_path, char *errmsg)
{
    if (!errmsg) {
        return -1;
    }
    
    const char *target_path = custom_path ? custom_path : "pngs/";
    char tmp_errmsg[256] = {0};
    
    /* Try to initialize with requested path */
    if (ensure_directory(target_path, tmp_errmsg) == 0) {
        /* Success: use requested path */
        if (strlen(target_path) >= sizeof(g_png_output_dir)) {
            snprintf(errmsg, 256, "PNG path too long (max %zu)", sizeof(g_png_output_dir) - 1);
            return -1;
        }
        strcpy(g_png_output_dir, target_path);
        return 0;
    }
    
    /* Primary path failed; attempt fallback to /tmp */
    if (custom_path) {  /* Only fall back if user specified a path and it failed */
        char fallback_path[PATH_MAX];
        snprintf(fallback_path, sizeof(fallback_path), "/tmp/srt2dvbsub.%d.pngs/", (int)getpid());
        
        if (ensure_directory(fallback_path, errmsg) == 0) {
            /* Use format specifier %.50s to guarantee max 50 chars per path in error message */
            snprintf(errmsg, 256, "PNG path %.50s failed, falling back to %.50s", 
                     target_path, fallback_path);
            strcpy(g_png_output_dir, fallback_path);
            return 0;  /* Fallback succeeded */
        }
        
        /* Even fallback failed - save the error first, then format the combined message */
        char tmp_err[256];
        strncpy(tmp_err, errmsg, sizeof(tmp_err) - 1);
        tmp_err[sizeof(tmp_err) - 1] = '\0';
        snprintf(errmsg, 256, "PNG path initialization failed: %.200s", tmp_err);
        return -1;
    }
    
    /* Default path failed; try fallback */
    char fallback_path[PATH_MAX];
    snprintf(fallback_path, sizeof(fallback_path), "/tmp/srt2dvbsub.%d.pngs/", (int)getpid());
    
    if (ensure_directory(fallback_path, errmsg) != 0) {
        /* Use format specifier %.50s to guarantee max 50 chars */
        snprintf(errmsg, 256, "Default PNG path failed, using fallback %.50s", fallback_path);
        strcpy(g_png_output_dir, fallback_path);
        return 0;
    }
    
    /* All paths failed */
    snprintf(errmsg, 256, "PNG path initialization failed: %s", tmp_errmsg);
    return -1;
}

/**
 * get_png_output_dir
 *
 * Return configured PNG output directory
 */
const char *get_png_output_dir(void)
{
    return dbg_png_output_dir;
}

/**
 * make_png_filename
 *
 * Generate safe PNG filename with full path
 */
int make_png_filename(char *output, size_t output_len,
                      int sequence, int track, int cue)
{
    if (!output || output_len == 0)
        return -1;
    
    /* Clamp values to safe ranges */
    int safe_seq = sequence % 1000;  /* Wrap at 1000 */
    int safe_track = track;
    if (safe_track < 0) safe_track = 0;
    if (safe_track > 7) safe_track = 7;
    
    int safe_cue = cue;
    if (safe_cue < 0) safe_cue = 0;
    if (safe_cue > 999) safe_cue = 999;
    
    /* Build filename with configured directory */
    const char *dir = get_png_output_dir();
    
    /* Ensure directory path ends with / */
    size_t dir_len = strlen(dir);
    if (dir_len > 0 && dir[dir_len - 1] != '/') {
        /* Directory path doesn't end with /; add it */
        int ret = snprintf(output, output_len, "%s/srt_%03d_t%02d_c%03d.png",
                          dir, safe_seq, safe_track, safe_cue);
        return (ret < 0 || (size_t)ret >= output_len) ? -1 : 0;
    } else {
        /* Directory path already ends with / */
        int ret = snprintf(output, output_len, "%ssrt_%03d_t%02d_c%03d.png",
                          dir, safe_seq, safe_track, safe_cue);
        return (ret < 0 || (size_t)ret >= output_len) ? -1 : 0;
    }
}

/**
 * cleanup_png_path
 *
 * Free PNG path resources
 */
void cleanup_png_path(void)
{
    /* Currently no dynamic allocations; module uses static storage */
    /* Reset to default for next initialization */
    strcpy(g_png_output_dir, "pngs/");
}

/**
 * get_png_path_usage
 *
 * Return usage string for PNG path configuration
 */
const char *get_png_path_usage(void)
{
    return "Relative path (./pngs) or absolute path (/tmp/debug)";
}
