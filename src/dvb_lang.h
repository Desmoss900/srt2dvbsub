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

#pragma once
#ifndef DVB_LANG_H
#define DVB_LANG_H

#include <ctype.h>
#include <string.h>
#include <strings.h>


/**
 * @struct dvb_lang_entry
 * @brief Represents a DVB language entry with code, English name, and native name.
 *
 * This structure holds information about a language used in DVB (Digital Video Broadcasting).
 * - code: The ISO 639-2 language code (e.g., "eng" for English).
 * - ename: The English name of the language.
 * - native: The native name of the language, as written in its own script.
 */
struct dvb_lang_entry
{
    const char *code;
    const char *ename;
    const char *native;
};


/**
 * @brief External array of DVB language entries.
 *
 * Lookup table mapping DVB three-letter language codes to their English and
 * native-language names.
 *
 * Description:
 *   Static array of `struct dvb_lang_entry` instances used to validate incoming
 *   language identifiers and to present friendly names in help output. Each
 *   element supplies the canonical DVB code, an English label, and a native
 *   script/localized label. The sequence is terminated by a sentinel entry with
 *   all fields set to NULL.
 *
 * Behavior:
 *   - Resides in read-only storage for the lifetime of the program.
 *   - Intended for iteration until the sentinel is encountered.
 *   - Consumers treat the `code` field as case-sensitive three-letter ASCII
 *     strings.
 *
 * Side effects:
 *   - None by itself; serves purely as static data.
 *
 * Thread safety and reentrancy:
 *   - Immutable table; safe for concurrent readers.
 *
 * Error handling:
 *   - Sentinel entry prevents overruns during iteration when code checks rely on
 *     null pointers.
 *
 * Intended use:
 *   - Support language validation, option parsing, and user-facing displays.
 */
extern const struct dvb_lang_entry dvb_langs[];

/**
 * @brief is_valid_dvb_lang Checks if a given language code is a valid DVB language code.
 *
 * Validate a three-letter DVB language code against the compiled-in lookup
 * table.
 *
 * Description:
 *   Confirm that the provided string is non-null, exactly three characters in
 *   length, and composed solely of alphabetic characters. If it passes those
 *   structural checks, a lowercase copy is matched against the entries in
 *   `dvb_langs`, returning success on the first hit.
 *
 * Behavior:
 *   - Rejects null pointers and strings that are not precisely three characters.
 *   - Converts each character to lowercase using the C locale and stores it in
 *     a temporary buffer.
 *   - Performs a linear search through `dvb_langs`, comparing with `strcmp`.
 *   - Returns 1 for a match, 0 otherwise.
 *
 * Side effects:
 *   - None beyond reading `code` and the `dvb_langs` table; no I/O or heap use.
 *
 * Thread safety and reentrancy:
 *   - Read-only operation; safe for concurrent calls when `dvb_langs` remains
 *     immutable.
 *
 * Error handling:
 *   - Treats null inputs, non-alphabetic characters, or incorrect length as
 *     invalid and returns 0.
 *
 * Intended use:
 *   - Validate user-supplied language codes prior to muxing metadata or subtitle
 *     streams.
 * 
 * @param code The language code to validate (must be a 3-letter string).
 * @return 1 if the code is valid, 0 otherwise.
 */
int is_valid_dvb_lang(const char *code);

#endif
