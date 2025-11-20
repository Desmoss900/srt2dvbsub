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

/* Public declaration for the global debug level used across the
 * srt2dvb codebase. Define the storage (int debug_level = 0;) in a
 * single C file (e.g. srt2dvbsub.c) and include this header where the
 * symbol is referenced instead of declaring 'extern' inline.
 */
#ifndef SRT2DVB_DEBUG_H
#define SRT2DVB_DEBUG_H

#include <stdio.h>

/* Global debug/verbosity level. Defined in a single translation unit. */
extern int debug_level;

/*
 * LOG macro
 * ---------
 * Use in source files for debug/diagnostic output. Each source file
 * may define DEBUG_MODULE as a string literal before including this
 * header to control the module prefix. If not defined, the default
 * module name is "srt2dvb".
 *
 * Example:
 *   #define DEBUG_MODULE "dvb_sub"
 *   #include "debug.h"
 *
 * Then use:
 *   LOG(1, "allocation failed: %s", msg);
 */
#ifndef DEBUG_MODULE
#define DEBUG_MODULE "srt2dvb"
#endif

#include <stdarg.h>

/* Internal helper: thread-safe write into stderr using stdio locking. */
static inline void srt2dvb_log_write(int level, const char *module, const char *fmt, ...)
{
	if (debug_level < level) return;
	va_list ap;
	va_start(ap, fmt);
#if defined(__unix__) || defined(__APPLE__)
	/* POSIX: use flockfile/funlockfile to serialize stdio writes */
	flockfile(stderr);
	fprintf(stderr, "[%s] ", module);
	vfprintf(stderr, fmt, ap);
	funlockfile(stderr);
#else
	/* Fallback: no stdio locking available; best-effort write */
	fprintf(stderr, "[%s] ", module);
	vfprintf(stderr, fmt, ap);
#endif
	va_end(ap);
}

#define LOG(level, fmt, ...) \
	do { srt2dvb_log_write(level, DEBUG_MODULE, fmt, ##__VA_ARGS__); } while (0)

#endif /* SRT2DVB_DEBUG_H */
