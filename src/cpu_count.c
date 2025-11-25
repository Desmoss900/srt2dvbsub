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
#include "cpu_count.h"
#include <unistd.h>

/*
 * get_cpu_count
 * -------------
 * Return the number of online logical processors available to the OS.
 *
 * The function is intentionally conservative: it prefers platform APIs
 * that report currently-online CPUs (e.g., sysconf(_SC_NPROCESSORS_ONLN)
 * on POSIX). If querying is unavailable or fails the function returns
 * 1 as a safe fallback. Callers should treat this value as a hint and
 * allow configuration overrides.
 */
int get_cpu_count(void) {
    /*
     * POSIX path: attempt to read the number of online processors via
     * sysconf(_SC_NPROCESSORS_ONLN). This value reflects CPUs the OS
     * currently considers online (excludes hotplugged-offline CPUs).
     */
#ifdef _SC_NPROCESSORS_ONLN
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n > 0) {
        /* Successful query: return the result as an int. */
        return (int)n;
    }
#endif

    /*
     * Fallback: if we couldn't query the platform, return 1 so callers
     * have a sensible default. This keeps behavior predictable on
     * unusual or constrained platforms.
     */
    return 1;
}
