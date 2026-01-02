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

/*
 * batch_encode.c
 * ---------------
 * The batch workflow discovers .ts inputs under a root directory, mirrors
 * the directory structure into an output root, resolves subtitle templates
 * to SRT files (from both a shared SRT tree and alongside the TS), and
 * invokes the encoder for every matched file. This keeps the orchestration
 * in-process and portable, avoiding shell dependencies.
 */

#include "batch_encode.h"

#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <libgen.h>
#include <limits.h>
#include <stdint.h>
#include <regex.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "debug.h"

/* Single-file pipeline entrypoint defined in srt2dvbsub.c */
int srt2dvbsub_run_cli(int argc, char **argv);
int srt2dvbsub_stop_requested(void);

extern char **environ;

/**
 * StrVec - Dynamic string vector
 * 
 * A growable array of string pointers with automatic capacity management.
 * Strings are owned by the vector and freed upon destruction.
 */
typedef struct StrVec {
	char **data;  /**< Array of string pointers */
	size_t len;   /**< Number of elements currently stored */
	size_t cap;   /**< Current capacity (allocated slots) */
} StrVec;

/**
 * EpisodeMeta - Parsed episode metadata
 * 
 * Extracted information from TV episode filenames following common patterns
 * like "Show_S01_E05" or "Show.1x05".
 */
typedef struct EpisodeMeta {
	char *show;       /**< Show name (dynamically allocated) */
	char season[3];   /**< Two-digit season number (zero-padded) */
	char episode[3];  /**< Two-digit episode number (zero-padded) */
} EpisodeMeta;

/* --------------------- String/vector helpers --------------------- */

/**
 * strvec_init - Initialize an empty string vector
 * @sv: Vector to initialize
 * 
 * Zeroes out the structure, preparing it for use. Must be called before
 * any other strvec_* operations.
 */
static void strvec_init(StrVec *sv)
{
	memset(sv, 0, sizeof(*sv));
}

/**
 * strvec_free - Free all resources owned by the vector
 * @sv: Vector to free
 * 
 * Frees all strings stored in the vector and the array itself. The vector
 * is reset to an empty state and can be reused after this call.
 */
static void strvec_free(StrVec *sv)
{
	if (!sv) return;
	for (size_t i = 0; i < sv->len; i++)
		free(sv->data[i]);
	free(sv->data);
	sv->data = NULL;
	sv->len = sv->cap = 0;
}

/**
 * strvec_reserve - Ensure the vector has capacity for at least 'need' elements
 * @sv: Vector to resize
 * @need: Minimum required capacity
 * 
 * Grows the vector capacity using a doubling strategy if necessary.
 * 
 * Return: 0 on success, -1 on allocation failure
 */
static int strvec_reserve(StrVec *sv, size_t need)
{
	if (need <= sv->cap)
		return 0;
	size_t new_cap = sv->cap ? sv->cap * 2 : 8;
	while (new_cap < need)
		new_cap *= 2;
	char **tmp = realloc(sv->data, new_cap * sizeof(char *));
	if (!tmp)
		return -1;
	sv->data = tmp;
	sv->cap = new_cap;
	return 0;
}

/**
 * strvec_push - Add a string to the vector (takes ownership)
 * @sv: Vector to append to
 * @value: String pointer to add (ownership transferred)
 * 
 * The vector takes ownership of the pointer and will free it on destruction.
 * 
 * Return: 0 on success, -1 on allocation failure
 */
static int strvec_push(StrVec *sv, char *value)
{
	if (strvec_reserve(sv, sv->len + 1) != 0)
		return -1;
	sv->data[sv->len++] = value;
	return 0;
}

/**
 * strvec_push_dup - Duplicate and add a string to the vector
 * @sv: Vector to append to
 * @value: String to duplicate and add
 * 
 * Creates a copy of the string before adding it to the vector.
 * 
 * Return: 0 on success, -1 if value is NULL or allocation fails
 */
static int strvec_push_dup(StrVec *sv, const char *value)
{
	if (!value)
		return -1;
	char *dup = strdup(value);
	if (!dup)
		return -1;
	return strvec_push(sv, dup);
}

/**
 * strvec_push_null - Append a NULL pointer to the vector
 * @sv: Vector to append to
 * 
 * Useful for creating NULL-terminated arrays (e.g., for execv).
 * 
 * Return: 0 on success, -1 on allocation failure
 */
static int strvec_push_null(StrVec *sv)
{
	if (strvec_reserve(sv, sv->len + 1) != 0)
		return -1;
	sv->data[sv->len] = NULL;
	return 0;
}

/**
 * strvec_detach - Transfer ownership of the vector's data
 * @sv: Vector to detach from
 * @out_data: Output pointer to receive the string array
 * @out_len: Output pointer to receive the array length
 * 
 * Transfers ownership of the internal array to the caller. The vector
 * is reset to empty. The caller is responsible for freeing the array
 * and its contents.
 */
static void strvec_detach(StrVec *sv, char ***out_data, size_t *out_len)
{
	if (out_data)
		*out_data = sv->data;
	if (out_len)
		*out_len = sv->len;
	sv->data = NULL;
	sv->len = sv->cap = 0;
}

/**
 * strvec_pop - Remove and optionally return the last element
 * @sv: Vector to pop from
 * @out: Optional output pointer to receive the removed string
 * 
 * Removes the last element without freeing it. If @out is non-NULL,
 * ownership of the string is transferred to the caller.
 * 
 * Return: 0 on success, -1 if the vector is empty
 */
static int strvec_pop(StrVec *sv, char **out)
{
	if (!sv || sv->len == 0)
		return -1;
	sv->len--;
	if (out)
		*out = sv->data[sv->len];
	return 0;
}

/**
 * strvec_join - Join all strings in the vector with a delimiter
 * @sv: Vector to join
 * @delim: Delimiter character (use '\0' for direct concatenation)
 * 
 * Creates a new string containing all vector elements separated by the
 * delimiter. The delimiter is not added after the last element.
 * 
 * Return: Newly allocated string on success, NULL on allocation failure
 */
static char *strvec_join(const StrVec *sv, char delim)
{
	if (!sv || sv->len == 0)
		return strdup("");

	size_t total = 1; /* terminating NUL */
	for (size_t i = 0; i < sv->len; i++) {
		total += strlen(sv->data[i]);
		if (delim != '\0' && i + 1 < sv->len)
			total += 1;
	}

	char *out = malloc(total);
	if (!out)
		return NULL;

	char *p = out;
	for (size_t i = 0; i < sv->len; i++) {
		size_t len = strlen(sv->data[i]);
		memcpy(p, sv->data[i], len);
		p += len;
		if (delim != '\0' && i + 1 < sv->len)
			*p++ = delim;
	}
	*p = '\0';
	return out;
}

/* --------------------- Path helpers --------------------- */

/**
 * mkdir_p - Create a directory and all parent directories (like mkdir -p)
 * @path: Full path to create
 * 
 * Creates all directories in the path if they don't already exist. Intermediate
 * directories are created with mode 0755. If a directory already exists, it is
 * not considered an error.
 * 
 * Return: 0 on success, -1 on failure (invalid path, path too long, or mkdir error)
 */
static int mkdir_p(const char *path)
{
	if (!path || *path == '\0')
		return -1;

	char tmp[PATH_MAX];
	if (strlen(path) >= sizeof(tmp))
		return -1;
	strcpy(tmp, path);

	for (char *p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
				return -1;
			*p = '/';
		}
	}
	if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
		return -1;
	return 0;
}

/**
 * path_has_ext - Check if a path ends with a specific extension
 * @path: File path to check
 * @ext: Extension to match (including the dot, e.g., ".ts")
 * 
 * Performs a case-insensitive comparison of the path's suffix against
 * the provided extension string.
 * 
 * Return: 1 if the path ends with the extension, 0 otherwise
 */
static int path_has_ext(const char *path, const char *ext)
{
	size_t lp = strlen(path);
	size_t le = strlen(ext);
	return (lp >= le && strcasecmp(path + lp - le, ext) == 0);
}

/**
 * path_join3 - Join up to three path components with proper separator handling
 * @a: First path component (required, base path)
 * @b: Second path component (optional, can be NULL or empty)
 * @c: Third path component (optional, can be NULL or empty)
 * 
 * Intelligently joins path components, automatically handling separator insertion
 * and avoiding duplicate slashes. If @b is NULL or empty, it is skipped. Leading
 * slashes in @c are stripped to prevent absolute path interpretation.
 * 
 * Return: Newly allocated joined path string on success, NULL on allocation failure
 */
static char *path_join3(const char *a, const char *b, const char *c)
{
	const char *seg_b = (b && *b) ? b : NULL;
	const char *seg_c = c ? c : "";

	size_t la = strlen(a);
	size_t lb = seg_b ? strlen(seg_b) : 0;
	size_t lc = strlen(seg_c);
	/* Guard against overflow in length calculation */
	if (la > SIZE_MAX - lb - lc - 3)
		return NULL;
	size_t total = la + lb + lc + 3; /* room for up to two separators + NUL */

	char *out = malloc(total);
	if (!out)
		return NULL;

	size_t pos = 0;
	memcpy(out + pos, a, la);
	pos += la;

	int need_sep_before_b = seg_b && pos > 0 && out[pos - 1] != '/';
	int need_sep_before_c = (!seg_b && lc > 0 && pos > 0 && out[pos - 1] != '/' && seg_c[0] != '/');

	if (need_sep_before_b)
		out[pos++] = '/';
	if (seg_b) {
		memcpy(out + pos, seg_b, lb);
		pos += lb;
		if (pos > 0 && out[pos - 1] != '/' && lc > 0 && seg_c[0] != '/')
			out[pos++] = '/';
	} else if (need_sep_before_c) {
		out[pos++] = '/';
	}

	if (lc > 0) {
		const char *c_start = seg_c;
		size_t lc_adj = lc;
		if (seg_c[0] == '/') {
			c_start++;
			lc_adj--;
		}
		memcpy(out + pos, c_start, lc_adj);
		pos += lc_adj;
	}

	out[pos] = '\0';
	return out;
}

/**
 * file_exists - Check if a regular file exists at the given path
 * @path: Path to check
 * 
 * Uses stat() to verify that the path exists and refers to a regular file
 * (not a directory, symlink, device, etc.).
 * 
 * Return: 1 if the path exists and is a regular file, 0 otherwise
 */
static int file_exists(const char *path)
{
	struct stat st;
	return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

/**
 * dir_exists - Check if a directory exists at the given path
 * @path: Path to check
 * 
 * Uses stat() to verify that the path exists and refers to a directory.
 * 
 * Return: 1 if the path exists and is a directory, 0 otherwise
 */
static int dir_exists(const char *path)
{
	struct stat st;
	return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

/* --------------------- Metadata helpers --------------------- */

/**
 * trim_trailing_delims - Remove trailing delimiters and whitespace from a string
 * @s: String to modify (modified in place)
 * 
 * Removes any trailing characters that are dots (.), underscores (_), hyphens (-),
 * or whitespace from the end of the string. The string is modified in place by
 * null-terminating it at the appropriate position. This is commonly used to clean
 * up show names extracted from filenames.
 * 
 * Safe to call with NULL (no operation performed).
 */
static void trim_trailing_delims(char *s)
{
	if (!s) return;
	size_t len = strlen(s);
	while (len > 0) {
		char c = s[len - 1];
		if (c == '.' || c == '_' || c == '-' || isspace((unsigned char)c)) {
			s[len - 1] = '\0';
			len--;
		} else {
			break;
		}
	}
}

/**
 * path_is_safe_relative - Check if a path is safe for use as a relative path
 * @path: Path to validate
 * 
 * Validates that a path is safe to use as a relative path by ensuring it:
 * - Is not NULL or empty
 * - Does not start with '/' (not absolute)
 * - Does not contain ".." segments (no directory traversal)
 * - Does not contain "." segments (no current directory references)
 * 
 * This function is used to prevent path traversal attacks when resolving
 * subtitle files from user-supplied templates.
 * 
 * Return: 1 if the path is safe to use, 0 if it should be rejected
 */
static int path_is_safe_relative(const char *path)
{
	if (!path || *path == '\0')
		return 0;
	if (path[0] == '/')
		return 0;
	const char *p = path;
	while (*p) {
		while (*p == '/') p++;
		const char *seg_start = p;
		while (*p && *p != '/') p++;
		size_t seg_len = (size_t)(p - seg_start);
		if (seg_len == 0)
			continue;
		if (seg_len == 2 && seg_start[0] == '.' && seg_start[1] == '.')
			return 0;
		if (seg_len == 1 && seg_start[0] == '.')
			return 0;
	}
	return 1;
}

/**
 * fill_two_digit - Format an integer as a zero-padded two-digit string
 * @val: Integer value to format (clamped to 0-99 range)
 * @out: Output buffer (must be at least 3 bytes for "XX\0")
 * 
 * Converts an integer to a two-digit zero-padded string. Values below 0
 * are clamped to 0, and values above 99 are clamped to 99. This ensures
 * consistent formatting for season and episode numbers in filename parsing.
 * 
 * Examples:
 *   5  -> "05"
 *   42 -> "42"
 *   -1 -> "00"
 *   100 -> "99"
 */
static void fill_two_digit(int val, char out[3])
{
	if (val < 0) val = 0;
	if (val > 99) val = 99;
	snprintf(out, 3, "%02d", val);
}

/**
 * parse_episode_meta - Extract show, season, and episode information from filename
 * @basename: Filename (without path) to parse
 * @meta: Output structure to populate with extracted metadata
 * 
 * Attempts to parse TV episode metadata from a filename using multiple common
 * naming patterns. Supports various delimiter styles (dots, underscores, spaces,
 * hyphens) and season/episode formats (S01E05, 1x05, S01 E05, etc.).
 * 
 * The function tries patterns in order of specificity and stops at the first match.
 * If no pattern matches, the metadata structure is left with empty fields.
 * 
 * Supported patterns include:
 * - Underscore-separated: SHOW_S01_E05
 * - Dot-separated: SHOW.S01E05, SHOW.S01.E05
 * - x-separator: SHOW.1x05, SHOW_1x05, SHOW-1x05
 * - Space-separated: SHOW S01E05, SHOW S01 E05, SHOW 1x05
 * - Mixed delimiters: SHOW[._-]S01[._-]E05
 * 
 * The show name is extracted and cleaned by removing trailing delimiters
 * and whitespace. Season and episode numbers are zero-padded to two digits.
 * 
 * The @meta structure is always zeroed before parsing. If parsing succeeds,
 * meta->show is dynamically allocated and must be freed by the caller.
 */
static void parse_episode_meta(const char *basename, EpisodeMeta *meta)
{
	memset(meta, 0, sizeof(*meta));

	const char *patterns[] = {
		/* Underscore-separated: SHOW_S01_E01 */
		"^(.*)_S([0-9]+)_E([0-9]+)$",
		"^(.*)_S([0-9]+)_E([0-9]+).*$",
		/* Dot-separated: SHOW.S01E01 or SHOW.S01.E01 */
		"^(.*)\\.S([0-9]+)\\.E([0-9]+)$",
		"^(.*)\\.S([0-9]+)\\.E([0-9]+).*$",
		"^(.*)\\.S([0-9]+)E([0-9]+)$",
		"^(.*)\\.S([0-9]+)E([0-9]+).*$",
		/* x-separator with dot: SHOW.1x01 */
		"^(.*)\\.([0-9]+)x([0-9]+)$",
		"^(.*)\\.([0-9]+)x([0-9]+).*$",
		/* x-separator with underscore or hyphen: SHOW_1x01, SHOW-1x01 */
		"^(.*)[_-]([0-9]+)x([0-9]+)$",
		"^(.*)[_-]([0-9]+)x([0-9]+).*$",
		/* Mixed delimiters: SHOW[._-]S01[._-]E01 */
		"^(.*)[._-]S([0-9]+)[._-]E([0-9]+)$",
		"^(.*)[._-]S([0-9]+)[._-]E([0-9]+).*$",
		/* Space-separated: SHOW S01E01 or SHOW S01 E01 */
		"^(.*) S([0-9]+)E([0-9]+)$",
		"^(.*) S([0-9]+)E([0-9]+).*$",
		"^(.*) S([0-9]+) E([0-9]+)$",
		"^(.*) S([0-9]+) E([0-9]+).*$",
		/* Space with x: SHOW 1x01 or SHOW 01x01 */
		"^(.*) ([0-9]+)x([0-9]+)$",
		"^(.*) ([0-9]+)x([0-9]+).*$"
	};

	for (size_t i = 0; i < sizeof(patterns)/sizeof(patterns[0]); i++) {
		regex_t re;
		if (regcomp(&re, patterns[i], REG_ICASE | REG_EXTENDED) != 0)
			continue;

		regmatch_t m[4];
		int rc = regexec(&re, basename, 4, m, 0);
		if (rc == 0) {
			size_t show_len = (size_t)(m[1].rm_eo - m[1].rm_so);
			if (show_len > 0) {
				meta->show = strndup(basename + m[1].rm_so, show_len);
				trim_trailing_delims(meta->show);
			}
			int season = atoi(basename + m[2].rm_so);
			int episode = atoi(basename + m[3].rm_so);
			fill_two_digit(season, meta->season);
			fill_two_digit(episode, meta->episode);
			regfree(&re);
			return;
		}
		regfree(&re);
	}
}

/**
 * substitute_template - Expand template variables in a pattern string
 * @pattern: Template pattern containing ${VAR} placeholders
 * @meta: Episode metadata for variable substitution (can be NULL)
 * @basename: Base filename without extension (can be NULL)
 * 
 * Expands variable placeholders in a template pattern by replacing them with
 * actual values from the provided metadata and basename. This function is used
 * to resolve subtitle file paths from user-defined templates.
 * 
 * Supported placeholders:
 * - ${BASENAME}: Replaced with the @basename parameter (or empty string if NULL)
 * - ${SHOW}: Replaced with meta->show (or empty string if meta is NULL or show is NULL)
 * - ${SEASON}: Replaced with meta->season (or empty string if meta is NULL or season is empty)
 * - ${EPISODE}: Replaced with meta->episode (or empty string if meta is NULL or episode is empty)
 * 
 * All other characters in the pattern are copied verbatim. Unknown placeholders
 * are treated as literal text (copied character by character).
 * 
 * Examples:
 *   pattern: "${BASENAME}.en.srt"
 *   basename: "MyShow_S01_E05"
 *   result: "MyShow_S01_E05.en.srt"
 * 
 *   pattern: "${SHOW}/${SEASON}/${EPISODE}.de.srt"
 *   meta: {show="MyShow", season="01", episode="05"}
 *   result: "MyShow/01/05.de.srt"
 * 
 * Return: Newly allocated string with all variables expanded, or NULL on allocation failure.
 *         The caller is responsible for freeing the returned string.
 */
static char *substitute_template(const char *pattern, const EpisodeMeta *meta, const char *basename)
{
	const char *p = pattern;
	StrVec chunks;
	strvec_init(&chunks);

	while (*p) {
		if (strncmp(p, "${BASENAME}", 11) == 0) {
			strvec_push_dup(&chunks, basename ? basename : "");
			p += 11;
			continue;
		}
		if (strncmp(p, "${SHOW}", 7) == 0) {
			strvec_push_dup(&chunks, (meta && meta->show) ? meta->show : "");
			p += 7;
			continue;
		}
		if (strncmp(p, "${SEASON}", 9) == 0) {
			strvec_push_dup(&chunks, (meta && meta->season[0]) ? meta->season : "");
			p += 9;
			continue;
		}
		if (strncmp(p, "${EPISODE}", 10) == 0) {
			strvec_push_dup(&chunks, (meta && meta->episode[0]) ? meta->episode : "");
			p += 10;
			continue;
		}
		char buf[2] = { *p, '\0' };
		strvec_push_dup(&chunks, buf);
		p++;
	}

	char *joined = strvec_join(&chunks, '\0');
	strvec_free(&chunks);
	return joined;
}

/* --------------------- Template management --------------------- */

/**
 * add_template_internal - Add a subtitle template to the configuration
 * @cfg: Configuration to modify
 * @pattern: Template pattern string with optional ${VAR} placeholders
 * @lang: Three-letter ISO 639-2 language code (e.g., "eng", "deu")
 * 
 * Internal helper that adds a new subtitle resolution template to the batch
 * configuration. The template pattern can contain placeholders like ${BASENAME},
 * ${SHOW}, ${SEASON}, and ${EPISODE} that will be expanded when resolving
 * subtitle files for each input video.
 * 
 * The @pattern string is duplicated and owned by the configuration. The @lang
 * parameter must be exactly 3 characters long and will be truncated if longer.
 * 
 * This function grows the templates array by reallocating it to accommodate
 * the new entry. If reallocation fails, the configuration is left unchanged.
 * 
 * Return: 0 on success, -1 if parameters are invalid or allocation fails
 */
static int add_template_internal(BatchEncodeConfig *cfg, const char *pattern, const char *lang)
{
	if (!cfg || !pattern || !lang)
		return -1;
	if (strlen(lang) != 3)
		return -1;

	BatchEncodeTemplate *tmp = realloc(cfg->templates, (cfg->template_count + 1) * sizeof(BatchEncodeTemplate));
	if (!tmp)
		return -1;
	cfg->templates = tmp;
	BatchEncodeTemplate *slot = &cfg->templates[cfg->template_count];
	slot->pattern = strdup(pattern);
	if (!slot->pattern)
		return -1;
	snprintf(slot->lang, sizeof(slot->lang), "%.3s", lang);
	cfg->template_count++;
	return 0;
}

/**
 * batch_encode_add_template - Add a subtitle template from a formatted entry string
 * @cfg: Configuration to modify
 * @entry: Template entry in format "pattern|lang" where lang is a 3-letter ISO 639-2 code
 * 
 * Parses a template entry string and adds it to the batch configuration. The entry
 * must follow the format "pattern|lang", where:
 * - pattern: Template string with optional ${VAR} placeholders (before the '|')
 * - lang: Three-letter ISO 639-2 language code (after the '|')
 * 
 * The pipe character ('|') acts as the delimiter between the pattern and language code.
 * If the entry does not contain a pipe or the language code is not exactly 3 characters,
 * the function returns an error.
 * 
 * Examples of valid entries:
 *   "${BASENAME}.en.srt|eng"
 *   "${SHOW}/${SEASON}/${EPISODE}.de.srt|deu"
 *   "subtitles/${BASENAME}.fr.srt|fre"
 * 
 * This is the public API for adding templates and is commonly used during CLI parsing.
 * It wraps add_template_internal() after parsing the entry string.
 * 
 * Return: 0 on success, -1 if parameters are invalid, format is incorrect, or allocation fails
 */
int batch_encode_add_template(BatchEncodeConfig *cfg, const char *entry)
{
	if (!cfg || !entry)
		return -1;
	const char *sep = strchr(entry, '|');
	if (!sep)
		return -1;

	size_t plen = (size_t)(sep - entry);
	char *pattern = strndup(entry, plen);
	const char *lang = sep + 1;
	int rc = add_template_internal(cfg, pattern, lang);
	free(pattern);
	return rc;
}

/**
 * batch_encode_init_defaults - Initialize a batch configuration with default templates
 * @cfg: Configuration structure to initialize
 * 
 * Initializes a BatchEncodeConfig structure to a clean state and populates it with
 * a default set of subtitle resolution templates. This function should be called
 * before using the configuration or parsing command-line arguments.
 * 
 * The configuration is zeroed out, setting all pointers to NULL, counts to 0, and
 * dry_run to 0 (disabled). Four default templates are then added, covering common
 * English and German subtitle file naming conventions:
 * 
 * 1. "${BASENAME}.en.subtitles.srt|eng" - English subtitles
 * 2. "${BASENAME}.en.srt|eng" - English subtitles (simple format)
 * 
 * These templates are evaluated in order during subtitle resolution, with the first
 * matching file being selected for each template. This allows fallback behavior
 * (e.g., try closed captions first, then regular subtitles).
 * 
 * After initialization, the caller can modify the configuration by:
 * - Setting input_dir, output_dir, and srt_dir (required for batch processing)
 * - Adding more templates via batch_encode_add_template()
 * - Clearing templates with --batch-clear-templates and adding custom ones
 * - Setting dry_run mode
 * - Adding forward_args to pass to the encoder
 * 
 * The configuration must be freed with batch_encode_free() when no longer needed.
 * 
 * Return: 0 on success, -1 if cfg is NULL or template initialization fails
 */
int batch_encode_init_defaults(BatchEncodeConfig *cfg)
{
	if (!cfg)
		return -1;
	memset(cfg, 0, sizeof(*cfg));

	cfg->dry_run = 0;

	const char *defaults[] = {
		"${BASENAME}.en.subtitles.srt|eng",
		"${BASENAME}.en.srt|eng"
	};
	for (size_t i = 0; i < sizeof(defaults)/sizeof(defaults[0]); i++) {
		if (batch_encode_add_template(cfg, defaults[i]) != 0)
			return -1;
	}
	return 0;
}

/**
 * batch_encode_free - Free all resources owned by a batch configuration
 * @cfg: Configuration structure to free
 * 
 * Releases all dynamically allocated memory associated with the batch configuration,
 * including:
 * - Input, output, and SRT directory paths
 * - Forwarded command-line arguments array and individual argument strings
 * - Subtitle resolution templates array and individual pattern strings
 * 
 * After freeing all resources, the configuration structure is zeroed out, returning
 * it to a clean state. The structure itself is not freed (caller-owned), allowing it
 * to be reused with batch_encode_init_defaults() if needed.
 * 
 * This function is safe to call multiple times on the same configuration and safe
 * to call on a configuration that has been partially initialized or already freed.
 * 
 * Safe to call with NULL (no operation performed).
 */
void batch_encode_free(BatchEncodeConfig *cfg)
{
	if (!cfg)
		return;
	free(cfg->input_dir);
	free(cfg->output_dir);
	free(cfg->srt_dir);

	if (cfg->forward_args) {
		for (size_t i = 0; i < cfg->forward_count; i++)
			free(cfg->forward_args[i]);
		free(cfg->forward_args);
	}

	for (size_t i = 0; i < cfg->template_count; i++)
		free(cfg->templates[i].pattern);
	free(cfg->templates);

	memset(cfg, 0, sizeof(*cfg));
}

/* --------------------- CLI parsing --------------------- */

/**
 * batch_encode_requested - Check if batch encoding mode was requested
 * @argc: Argument count from main()
 * @argv: Argument vector from main()
 * 
 * Scans the command-line arguments to determine if the --batch-encode flag
 * is present. This function is called early during CLI parsing to decide
 * whether to invoke the batch workflow or the single-file pipeline.
 * 
 * This is a simple presence check that does not validate other required
 * batch arguments or parse their values. Full validation is performed later
 * by batch_encode_parse_cli().
 * 
 * Return: 1 if --batch-encode flag is found, 0 otherwise
 */
int batch_encode_requested(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--batch-encode") == 0)
			return 1;
	}
	return 0;
}

/**
 * match_eq_prefix - Check if an argument matches a key with '=' syntax
 * @arg: Command-line argument to check
 * @key: Key prefix to match (e.g., "--batch-input")
 * 
 * Tests whether @arg begins with @key followed immediately by an equals sign ('=').
 * This enables support for arguments in the form "--key=value" as an alternative
 * to the space-separated "--key value" format.
 * 
 * If the argument matches the pattern, a pointer to the value portion (after the '=')
 * is returned. Otherwise, NULL is returned if:
 * - The argument doesn't start with the key
 * - The key is present but not followed by '='
 * 
 * This function is used during CLI parsing to support both argument styles:
 *   --batch-input=/path/to/input    (returns "/path/to/input")
 *   --batch-input /path/to/input    (returns NULL, handled separately)
 * 
 * Examples:
 *   match_eq_prefix("--batch-input=/foo", "--batch-input")  -> "/foo"
 *   match_eq_prefix("--batch-input", "--batch-input")       -> NULL
 *   match_eq_prefix("--batch-output=/bar", "--batch-input") -> NULL
 * 
 * Return: Pointer to the value portion after '=' on match, NULL otherwise
 */
static const char *match_eq_prefix(const char *arg, const char *key)
{
	size_t klen = strlen(key);
	if (strncmp(arg, key, klen) != 0)
		return NULL;
	if (arg[klen] == '=')
		return arg + klen + 1;
	return NULL;
}

/**
 * batch_encode_parse_cli - Parse command-line arguments for batch encoding mode
 * @argc: Argument count from main()
 * @argv: Argument vector from main()
 * @cfg: Configuration structure to populate with parsed values
 * 
 * Parses command-line arguments to extract batch-specific options and separate
 * them from arguments that should be forwarded to the underlying encoder. This
 * function processes both batch control flags and encoder passthrough arguments.
 * 
 * Recognized batch-specific arguments:
 * - --batch-encode: Enables batch mode (required)
 * - --batch-input=PATH or --batch-input PATH: Input directory containing .ts files
 * - --batch-output=PATH or --batch-output PATH: Output directory for processed files
 * - --batch-srt=PATH or --batch-srt PATH: SRT directory for subtitle file resolution
 * - --batch-template=PATTERN|LANG or --batch-template PATTERN|LANG: Add subtitle template
 * - --batch-clear-templates: Remove all existing templates before adding new ones
 * - --batch-dry-run: Enable dry-run mode (show commands without executing)
 * - --batch-sequential: Placeholder for future parallel processing control (currently no-op)
 * 
 * All arguments not matching batch-specific patterns are collected and stored in
 * cfg->forward_args for passing to the encoder pipeline. This allows users to specify
 * encoder options (like --page-id, --resolution, --font-*) alongside batch options.
 * 
 * Both "--key=value" and "--key value" syntaxes are supported for arguments that
 * take values. Template entries must follow the format "pattern|lang" where lang
 * is a three-letter ISO 639-2 language code.
 * 
 * The function validates that required arguments (--batch-input, --batch-output,
 * --batch-srt) are present when --batch-encode is specified. It does not validate
 * the existence of paths or the correctness of forwarded arguments.
 * 
 * Return: -1 if --batch-encode was not found (not a batch invocation),
 *         0 on successful parse (should not reach here due to logic),
 *         1 on error (missing required arguments, invalid template format, or allocation failure)
 */
int batch_encode_parse_cli(int argc, char **argv, BatchEncodeConfig *cfg)
{
	int saw_batch = 0;
	StrVec forward;
	strvec_init(&forward);

	for (int i = 1; i < argc; i++) {
		const char *arg = argv[i];

		/* Handle batch flags (consume their arguments) */
		if (strcmp(arg, "--batch-encode") == 0) {
			saw_batch = 1;
			continue;
		}

		const char *val = NULL;
		if ((val = match_eq_prefix(arg, "--batch-input")) != NULL || strcmp(arg, "--batch-input") == 0) {
			if (!val) {
				if (i + 1 >= argc) return 1;
				val = argv[++i];
			}
			free(cfg->input_dir);
			cfg->input_dir = strdup(val);
			continue;
		}
		if ((val = match_eq_prefix(arg, "--batch-output")) != NULL || strcmp(arg, "--batch-output") == 0) {
			if (!val) {
				if (i + 1 >= argc) return 1;
				val = argv[++i];
			}
			free(cfg->output_dir);
			cfg->output_dir = strdup(val);
			continue;
		}
		if ((val = match_eq_prefix(arg, "--batch-srt")) != NULL || strcmp(arg, "--batch-srt") == 0) {
			if (!val) {
				if (i + 1 >= argc) return 1;
				val = argv[++i];
			}
			free(cfg->srt_dir);
			cfg->srt_dir = strdup(val);
			continue;
		}
		if ((val = match_eq_prefix(arg, "--batch-template")) != NULL || strcmp(arg, "--batch-template") == 0) {
			if (!val) {
				if (i + 1 >= argc) return 1;
				val = argv[++i];
			}
			if (batch_encode_add_template(cfg, val) != 0) {
				LOG(0, "Invalid --batch-template entry; expected 'pattern|lang'\n");
				strvec_free(&forward);
				return 1;
			}
			continue;
		}
		if (strcmp(arg, "--batch-clear-templates") == 0) {
			for (size_t t = 0; t < cfg->template_count; t++)
				free(cfg->templates[t].pattern);
			free(cfg->templates);
			cfg->templates = NULL;
			cfg->template_count = 0;
			continue;
		}
		if (strcmp(arg, "--batch-dry-run") == 0) {
			cfg->dry_run = 1;
			continue;
		}
		if (strcmp(arg, "--batch-sequential") == 0) {
			/* Placeholder for future parallelisation; currently sequential. */
			continue;
		}

		/* Everything else is forwarded verbatim */
		if (strvec_push_dup(&forward, arg) != 0) {
			strvec_free(&forward);
			return 1;
		}
	}

	if (!saw_batch) {
		strvec_free(&forward);
		return -1; /* Not a batch invocation */
	}

	if (!cfg->input_dir || !cfg->output_dir || !cfg->srt_dir) {
		LOG(0, "--batch-input, --batch-output, and --batch-srt are required with --batch-encode\n");
		strvec_free(&forward);
		return 1;
	}

	strvec_detach(&forward, &cfg->forward_args, &cfg->forward_count);
	return -1;
}

/* --------------------- Subtitle resolution --------------------- */

/**
 * resolve_subtitles_for_ts - Resolve subtitle files for a given transport stream file
 * @cfg: Batch encoding configuration containing templates and directory paths
 * @rel_dir: Relative directory path from input root to the TS file's directory (may be empty)
 * @ts_dir: Absolute path to the directory containing the TS file
 * @basename: Base filename of the TS file without extension
 * @srts: Output vector to populate with resolved subtitle file paths (ownership transferred)
 * @langs: Output vector to populate with corresponding language codes (ownership transferred)
 * 
 * Attempts to resolve subtitle files for a transport stream by evaluating all configured
 * templates against two potential locations: the shared SRT directory (cfg->srt_dir) and
 * alongside the TS file itself (ts_dir). This allows for both centralized subtitle
 * repositories and per-video subtitle files.
 * 
 * For each template in the configuration:
 * 1. Parse episode metadata from the basename (show name, season, episode numbers)
 * 2. Expand template variables (${BASENAME}, ${SHOW}, ${SEASON}, ${EPISODE})
 * 3. Validate that the resulting path is safe (no directory traversal)
 * 4. Search for the subtitle file in the SRT directory under rel_dir
 * 5. If not found, search alongside the TS file in ts_dir
 * 6. If found, add the path to srts and the language code to langs
 * 
 * The search stops after finding one subtitle file per template. If a template resolves
 * to an unsafe path (containing ".." or absolute path), it is skipped with a warning.
 * 
 * The caller is responsible for initializing the srts and langs vectors before calling
 * this function. On success, the vectors will contain one entry per matched template,
 * with subtitle paths and their corresponding ISO 639-2 language codes.
 * 
 * Example resolution flow for template "${BASENAME}.en.srt|eng":
 * - basename: "Show_S01_E05"
 * - rel_dir: "TV/Show"
 * - Template expands to: "Show_S01_E05.en.srt"
 * - Search locations (in order):
 *   1. cfg->srt_dir/TV/Show/Show_S01_E05.en.srt
 *   2. ts_dir/Show_S01_E05.en.srt
 * 
 * Return: 0 if at least one subtitle file was resolved, 1 if no subtitles were found,
 *         or 1 on allocation failure
 */
static int resolve_subtitles_for_ts(const BatchEncodeConfig *cfg,
							const char *rel_dir,
							const char *ts_dir,
							const char *basename,
							StrVec *srts,
							StrVec *langs)
{
	EpisodeMeta meta;
	parse_episode_meta(basename, &meta);

	for (size_t i = 0; i < cfg->template_count; i++) {
		const BatchEncodeTemplate *tpl = &cfg->templates[i];
		char *resolved = substitute_template(tpl->pattern, &meta, basename);
		if (!resolved)
			continue;
		if (!path_is_safe_relative(resolved)) {
			LOG(0, "Skipping unsafe subtitle path '%s' for %s\n", resolved, basename);
			free(resolved);
			continue;
		}

		char *srt_path = NULL;
		if (rel_dir && *rel_dir) {
			srt_path = path_join3(cfg->srt_dir, rel_dir, resolved);
		} else {
			srt_path = path_join3(cfg->srt_dir, "", resolved);
		}

		if (srt_path && file_exists(srt_path)) {
			if (strvec_push(srts, srt_path) != 0 || strvec_push_dup(langs, tpl->lang) != 0) {
				free(srt_path);
				free(resolved);
				free(meta.show);
				return 1;
			}
			free(resolved);
			continue;
		}
		free(srt_path);

		srt_path = path_join3(ts_dir, "", resolved);
		if (srt_path && file_exists(srt_path)) {
			if (strvec_push(srts, srt_path) != 0 || strvec_push_dup(langs, tpl->lang) != 0) {
				free(srt_path);
				free(resolved);
				free(meta.show);
				return 1;
			}
			free(resolved);
			continue;
		}
		free(srt_path);
		free(resolved);
	}

	free(meta.show);
	return (srts->len > 0) ? 0 : 1;
}

/* --------------------- Directory scanning --------------------- */

/**
 * collect_ts_recursive - Recursively collect all .ts files under a directory
 * @dir_path: Root directory path to start scanning from
 * @files: Output vector to populate with absolute paths to .ts files
 * 
 * Performs a depth-first recursive traversal of the directory tree starting at
 * @dir_path, collecting absolute paths to all regular files with a ".ts" extension.
 * The function uses an iterative approach with an explicit stack to avoid deep
 * recursion and stack overflow issues with deeply nested directory structures.
 * 
 * The traversal follows symbolic links during directory scanning but only collects
 * regular files (not symlinked files) as determined by lstat(). Hidden files and
 * directories (starting with '.') are processed normally, but "." and ".." entries
 * are always skipped.
 * 
 * Files are added to the @files vector in the order they are discovered during
 * traversal, which depends on the filesystem's directory entry ordering. The caller
 * should sort the vector if deterministic ordering is required (see compare_paths()).
 * 
 * The function is robust against directory permission errors and unreadable entries:
 * - If opendir() fails on the root directory, the function returns immediately with error
 * - If lstat() fails on an entry, that entry is silently skipped
 * - If a subdirectory cannot be opened, the entire function fails and returns error
 * 
 * Memory management:
 * - The @files vector is not cleared before adding entries (allows multiple calls)
 * - All paths added to @files are duplicated and owned by the vector
 * - On error, the internal stack is cleaned up, but @files may contain partial results
 * - The caller should call strvec_free() on @files regardless of return value
 * 
 * Return: 0 on successful traversal (even if no .ts files were found),
 *         -1 on allocation failure or if the root directory cannot be opened
 */
static int collect_ts_recursive(const char *dir_path, StrVec *files)
{
	StrVec stack;
	strvec_init(&stack);
	if (strvec_push_dup(&stack, dir_path) != 0)
		return -1;

	while (stack.len > 0) {
		char *current = NULL;
		if (strvec_pop(&stack, &current) != 0) {
			strvec_free(&stack);
			return -1;
		}

		DIR *dir = opendir(current);
		if (!dir) {
			free(current);
			strvec_free(&stack);
			return -1;
		}

		struct dirent *ent = NULL;
		while ((ent = readdir(dir)) != NULL) {
			if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
				continue;

			char *full = path_join3(current, "", ent->d_name);
			if (!full) {
				closedir(dir);
				free(current);
				strvec_free(&stack);
				return -1;
			}

			struct stat st;
			if (lstat(full, &st) != 0) {
				free(full);
				continue;
			}

			if (S_ISDIR(st.st_mode)) {
				if (strvec_push_dup(&stack, full) != 0) {
					free(full);
					closedir(dir);
					free(current);
					strvec_free(&stack);
					return -1;
				}
				free(full);
				continue;
			}

			if (S_ISREG(st.st_mode) && path_has_ext(full, ".ts")) {
				if (strvec_push_dup(files, full) != 0) {
					free(full);
					closedir(dir);
					free(current);
					strvec_free(&stack);
					return -1;
				}
			}
			free(full);
		}

		closedir(dir);
		free(current);
	}

	strvec_free(&stack);
	return 0;
}

/**
 * compare_paths - Comparison function for sorting file paths lexicographically
 * @a: Pointer to first string pointer (const char **)
 * @b: Pointer to second string pointer (const char **)
 * 
 * Standard qsort() comparison function that compares two file path strings
 * lexicographically using strcmp(). This function is used to sort the collected
 * .ts files into a deterministic order before processing, ensuring consistent
 * batch encoding behavior across different filesystems and platforms.
 * 
 * The function follows the qsort() comparison convention:
 * - Returns negative value if path a comes before path b
 * - Returns positive value if path a comes after path b
 * - Returns 0 if the paths are identical
 * 
 * Lexicographic ordering means:
 * - /path/a.ts comes before /path/b.ts
 * - /path/dir1/file.ts comes before /path/dir2/file.ts
 * - Shorter paths come before longer paths with the same prefix
 * 
 * This ordering is particularly useful for TV episodes, ensuring that:
 * - Episodes within a season are processed in numerical order
 * - Seasons are processed in order
 * - Different shows are grouped together
 * 
 * Example sorted order:
 *   /input/Show_S01_E01.ts
 *   /input/Show_S01_E02.ts
 *   /input/Show_S02_E01.ts
 *   /input/subdir/Another_S01_E01.ts
 * 
 * Return: Negative, zero, or positive integer as strcmp() result
 */
static int compare_paths(const void *a, const void *b)
{
	const char *pa = *(const char * const *)a;
	const char *pb = *(const char * const *)b;
	return strcmp(pa, pb);
}

/* --------------------- Main workflow --------------------- */

/**
 * relative_dir - Extract the relative directory path from root to file path
 * @root: Root directory path (base path to make relative from)
 * @path: Full file path to extract relative directory from
 * 
 * Computes the directory portion of the relative path from @root to @path.
 * This function is used to mirror directory structure when processing files
 * in a batch, ensuring that subdirectories under the input root are preserved
 * in the output root.
 * 
 * The function performs the following steps:
 * 1. Verifies that @path starts with @root (path must be under root)
 * 2. Strips the root prefix from the path, handling trailing slash variations
 * 3. Extracts the directory component using dirname()
 * 4. Returns an empty string if the file is directly in the root
 * 
 * Examples:
 *   root="/input", path="/input/dir/file.ts" -> "dir"
 *   root="/input/", path="/input/dir/file.ts" -> "dir"
 *   root="/input", path="/input/file.ts" -> "" (empty string)
 *   root="/input", path="/input/a/b/c/file.ts" -> "a/b/c"
 *   root="/other", path="/input/file.ts" -> NULL (path not under root)
 * 
 * The returned path never has a leading or trailing slash and uses "." 
 * internally during processing (which is converted to an empty string).
 * This format is suitable for use with path_join3() to construct output paths.
 * 
 * Return: Newly allocated relative directory path (may be empty string),
 *         or NULL if path is not under root or on allocation failure.
 *         The caller is responsible for freeing the returned string.
 */
static char *relative_dir(const char *root, const char *path)
{
	size_t root_len = strlen(root);
	if (strncmp(root, path, root_len) != 0)
		return NULL;
	int root_has_trailing_slash = (root_len > 0 && root[root_len - 1] == '/');
	if (!root_has_trailing_slash && path[root_len] != '\0' && path[root_len] != '/')
		return NULL;
	const char *rel = path + root_len;
	if (*rel == '/')
		rel++;
	char *dup = strdup(rel);
	if (!dup)
		return NULL;
	char *dir = dirname(dup);
	char *out = dir ? strdup(dir) : strdup("");
	free(dup);
	if (!out)
		return NULL;
	if (strcmp(out, ".") == 0)
		out[0] = '\0';
	return out;
}

/**
 * basename_no_ext - Extract filename without directory path or extension
 * @path: Full file path to extract basename from
 * 
 * Extracts the base filename from a full path and removes the file extension
 * (everything after the last dot). This is useful for processing filenames
 * when constructing output paths or matching subtitle templates.
 * 
 * The function performs the following operations:
 * 1. Duplicates the input path (basename() may modify its argument)
 * 2. Extracts the filename portion using basename()
 * 3. Locates the last dot character in the filename
 * 4. Truncates at the dot to remove the extension
 * 5. Returns a newly allocated copy of the result
 * 
 * Examples:
 *   "/path/to/file.ts" -> "file"
 *   "/path/to/file.name.ts" -> "file.name" (only last extension removed)
 *   "file.ts" -> "file"
 *   "/path/to/file" -> "file" (no extension to remove)
 *   "/path/to/.hidden" -> "" (dot files become empty after extension removal)
 * 
 * Hidden files (starting with '.') are handled by strrchr() which finds the
 * last dot, so ".config" becomes "" (empty string) after processing.
 * 
 * Return: Newly allocated string containing the basename without extension,
 *         or NULL on allocation failure. The caller is responsible for
 *         freeing the returned string.
 */
static char *basename_no_ext(const char *path)
{
	char *dup = strdup(path);
	if (!dup)
		return NULL;
	char *base = basename(dup);
	char *dot = strrchr(base, '.');
	if (dot)
		*dot = '\0';
	char *out = strdup(base);
	free(dup);
	return out;
}

/**
 * cleanup_file_state - Free all resources associated with processing a single file
 * @srts: String vector containing resolved subtitle file paths
 * @langs: String vector containing corresponding language codes
 * @rel_dir: Relative directory path (dynamically allocated)
 * @ts_dir_dup: Duplicated transport stream directory path (dynamically allocated)
 * @base: Base filename without extension (dynamically allocated)
 * 
 * Releases all temporary resources allocated during the processing of a single
 * transport stream file in the batch workflow. This helper function centralizes
 * cleanup logic to ensure consistent resource management across different error
 * paths and successful completion paths in batch_encode_run().
 * 
 * The function frees:
 * - Both string vectors (srts and langs) and all their contained strings
 * - The relative directory path computed from the input root
 * - The duplicated directory path used for dirname() processing
 * - The extracted base filename without extension
 * 
 * This function is safe to call even if some resources are NULL (free() and
 * strvec_free() handle NULL pointers gracefully). It is typically called:
 * - After successful encoding of a file to clean up temporary state
 * - On error paths when subtitle resolution or path construction fails
 * - Before breaking from the main processing loop on interruption
 * 
 * All parameters are expected to be either valid pointers or NULL. After this
 * function returns, all pointers should be considered invalid (dangling) and
 * must not be accessed.
 */
static void cleanup_file_state(StrVec *srts, StrVec *langs, char *rel_dir, char *ts_dir_dup, char *base)
{
	strvec_free(srts);
	strvec_free(langs);
	free(rel_dir);
	free(ts_dir_dup);
	free(base);
}

/**
 * batch_encode_run - Execute the batch encoding workflow for all files in the input directory
 * @cfg: Batch encoding configuration with input/output paths, templates, and options
 * @argv0: Program name (argv[0]) to use when invoking the encoder subprocess
 * 
 * Main entry point for the batch encoding workflow that orchestrates the complete process
 * of discovering, processing, and encoding multiple transport stream files with their
 * corresponding subtitle files. This function implements a recursive directory traversal,
 * subtitle resolution, and per-file encoder invocation pipeline.
 * 
 * Workflow Overview:
 * 1. Validation: Verify that required directories exist and are accessible
 * 2. Discovery: Recursively scan input directory for all .ts files
 * 3. Sorting: Order files lexicographically for deterministic processing
 * 4. Processing Loop: For each .ts file:
 *    a. Compute relative path from input root to preserve directory structure
 *    b. Resolve subtitle files using configured templates
 *    c. Create mirrored output directory structure
 *    d. Build encoder command with input, output, subtitles, and forwarded args
 *    e. Execute encoder (in-process or dry-run)
 *    f. Track success/failure statistics
 * 5. Summary: Report final processing statistics
 * 
 * Directory Structure Mirroring:
 * The function preserves the directory hierarchy from input_dir to output_dir.
 * Example:
 *   Input:  /input/TV/Show/Season01/Episode01.ts
 *   Output: /output/TV/Show/Season01/Episode01.ts
 * 
 * Subtitle Resolution:
 * For each .ts file, subtitles are resolved by evaluating all templates against:
 * 1. Shared SRT directory: cfg->srt_dir/<relative_path>/<resolved_template>
 * 2. Alongside the TS file: <ts_directory>/<resolved_template>
 * Templates support variable substitution (${BASENAME}, ${SHOW}, ${SEASON}, ${EPISODE}).
 * Files with no matching subtitles are skipped with a warning.
 * 
 * Command Construction:
 * The encoder is invoked with:
 * - Program name from @argv0
 * - All forwarded arguments from cfg->forward_args (user-specified encoder options)
 * - --input <ts_path>
 * - --output <output_path>
 * - --srt <comma-separated-subtitle-paths>
 * - --languages <comma-separated-language-codes>
 * 
 * Dry-Run Mode:
 * When cfg->dry_run is enabled:
 * - Commands are printed to stdout with "[DRY RUN]" prefix
 * - No directories are created (except output_dir is still validated)
 * - No encoder processes are spawned
 * - All files are counted as successfully processed
 * 
 * Error Handling:
 * - Missing directories: Returns 1 immediately
 * - Directory creation failures: Returns 1 immediately (unless dry-run)
 * - File-level errors: Logged, counted in 'failed', processing continues
 * - Memory allocation failures: Logged, counted in 'failed', processing continues
 * - Subtitle resolution failures: File skipped, logged, processing continues
 * 
 * Interruption Handling:
 * The function checks srt2dvbsub_stop_requested() after each file to support
 * graceful shutdown on interrupt signals (SIGINT/SIGTERM). When interrupted:
 * - Current file processing completes
 * - Remaining files are skipped
 * - Summary statistics reflect partial batch
 * - Return value indicates failure (1) if any files failed before interruption
 * 
 * Resource Management:
 * All dynamically allocated resources are carefully managed:
 * - File list is freed on all exit paths
 * - Per-file resources are cleaned up via cleanup_file_state()
 * - Command vectors are freed after each invocation
 * - Memory leaks are prevented even on error paths
 * 
 * Progress Reporting:
 * - Initial: "Batch encoding N file(s)"
 * - Per-file: "Encoding and adding subtitles to -> <relative_path>"
 * - Skipped files: "  Skipping: no subtitles matched"
 * - Errors: Logged via LOG() with severity levels
 * - Interruption: "Interrupt received, stopping batch early (processed=N failed=M)"
 * - Final: "Batch summary: processed=N failed=M"
 * 
 * Concurrency:
 * Currently sequential (one file at a time). The --batch-sequential flag is
 * recognized but has no effect, reserved for future parallel processing support.
 * 
 * Return: 0 if all files were processed successfully (or if no files required processing),
 *         1 if any required directory doesn't exist, directory creation fails,
 *         directory traversal fails, or any file failed to encode
 */
int batch_encode_run(const BatchEncodeConfig *cfg, const char *argv0)
{
	if (!cfg || !cfg->input_dir || !cfg->output_dir || !cfg->srt_dir)
		return 1;

	if (!dir_exists(cfg->input_dir)) {
		LOG(0, "Input directory does not exist: %s\n", cfg->input_dir);
		return 1;
	}
	if (!dir_exists(cfg->srt_dir)) {
		LOG(0, "SRT directory does not exist: %s\n", cfg->srt_dir);
		return 1;
	}
	if (!cfg->dry_run && mkdir_p(cfg->output_dir) != 0) {
		LOG(0, "Failed to create output directory: %s\n", cfg->output_dir);
		return 1;
	}

	StrVec files;
	strvec_init(&files);
	if (collect_ts_recursive(cfg->input_dir, &files) != 0) {
		LOG(0, "Failed to traverse input directory: %s\n", cfg->input_dir);
		strvec_free(&files);
		return 1;
	}

	if (files.len == 0) {
		LOG(0, "No .ts files found under %s\n", cfg->input_dir);
		strvec_free(&files);
		return 0;
	}

	qsort(files.data, files.len, sizeof(char *), compare_paths);

	size_t processed = 0;
	size_t failed = 0;
	int interrupted = 0;

	printf("Batch encoding %zu file(s)\n", files.len);

	size_t input_len = strlen(cfg->input_dir);
	int skip = (cfg->input_dir[input_len - 1] == '/') ? 0 : 1;

	for (size_t i = 0; i < files.len; i++) {
		const char *ts_path = files.data[i];
		if (strncmp(ts_path, cfg->input_dir, input_len) != 0) {
			LOG(0, "Skipping path outside input root: %s\n", ts_path);
			failed++;
			continue;
		}
		const char *rel_path = ts_path + input_len + skip;

		char *rel_dir = relative_dir(cfg->input_dir, ts_path);
		if (!rel_dir) {
			LOG(0, "Could not compute relative directory for %s\n", ts_path);
			failed++;
			continue;
		}

		char *ts_dir_dup = strdup(ts_path);
		if (!ts_dir_dup) { free(rel_dir); failed++; continue; }
		char *ts_dir = dirname(ts_dir_dup);

		char *base = basename_no_ext(ts_path);
		if (!base) { free(rel_dir); free(ts_dir_dup); failed++; continue; }

		printf("Encoding and adding subtitles to -> %s\n\n", rel_path);

		StrVec srts, langs;
		strvec_init(&srts);
		strvec_init(&langs);

		if (resolve_subtitles_for_ts(cfg, rel_dir, ts_dir, base, &srts, &langs) != 0) {
			LOG(1, "  Skipping: no subtitles matched\n");
			cleanup_file_state(&srts, &langs, rel_dir, ts_dir_dup, base);
			continue;
		}

		char *rel_output = path_join3(cfg->output_dir, rel_dir, "");
		if (!rel_output) {
			failed++;
			cleanup_file_state(&srts, &langs, rel_dir, ts_dir_dup, base);
			if (interrupted) break;
			continue;
		}
		if (!cfg->dry_run && mkdir_p(rel_output) != 0) {
			LOG(0, "  Failed to create output dir %s\n", rel_output);
			failed++;
			free(rel_output);
			cleanup_file_state(&srts, &langs, rel_dir, ts_dir_dup, base);
			if (interrupted) break;
			continue;
		}
		free(rel_output);

		char *output_path = path_join3(cfg->output_dir, "", rel_path);
		if (!output_path) {
			failed++;
			cleanup_file_state(&srts, &langs, rel_dir, ts_dir_dup, base);
			if (interrupted) break;
			continue;
		}

		/* Build and run encoder command for this file */
		StrVec cmd;
		strvec_init(&cmd);
		char *srt_list = NULL;
		char *lang_list = NULL;
		int srt_owned = 0;
		int lang_owned = 0;
		int encode_rc = -1;

		do {
			if (strvec_push_dup(&cmd, argv0) != 0)
				break;

			/* Forward all non-batch CLI args exactly as provided by the user */
			for (size_t i = 0; i < cfg->forward_count; i++) {
				if (strvec_push_dup(&cmd, cfg->forward_args[i]) != 0)
					break;
			}
			if (cmd.len != 1 + cfg->forward_count)
				break;

			if (strvec_push_dup(&cmd, "--input") != 0 || strvec_push_dup(&cmd, ts_path) != 0)
				break;
			if (strvec_push_dup(&cmd, "--output") != 0 || strvec_push_dup(&cmd, output_path) != 0)
				break;

            srt_list = strvec_join(&srts, ',');
            lang_list = strvec_join(&langs, ',');
			if (!srt_list || !lang_list)
				break;

			if (strvec_push_dup(&cmd, "--srt") != 0)
				break;
			if (strvec_push(&cmd, srt_list) != 0)
				break;
			srt_owned = 1;
			if (strvec_push_dup(&cmd, "--languages") != 0)
				break;
			if (strvec_push(&cmd, lang_list) != 0)
				break;
			lang_owned = 1;

			if (strvec_push_null(&cmd) != 0)
				break;

			if (cfg->dry_run) {
				printf("[DRY RUN] ");
				for (size_t i = 0; i + 1 < cmd.len; i++)
					printf("%s ", cmd.data[i]);
				printf("\n");
				encode_rc = 0;
				break;
			}

			/* Run the pipeline in-process */
			encode_rc = srt2dvbsub_run_cli((int)cmd.len, cmd.data);
			break;
		} while (0);

		if (!srt_owned)
			free(srt_list);
		if (!lang_owned)
			free(lang_list);
		strvec_free(&cmd);

		if (encode_rc == 0) {
			processed++;
		} else {
			failed++;
		}

		if (srt2dvbsub_stop_requested()) {
			printf("Interrupt received, stopping batch early (processed=%zu failed=%zu)\n", processed, failed);
			interrupted = 1;
		}

		free(output_path);
		cleanup_file_state(&srts, &langs, rel_dir, ts_dir_dup, base);
		if (interrupted)
			break;
	}

	strvec_free(&files);

	printf("Batch summary: processed=%zu failed=%zu\n", processed, failed);
	return (failed == 0) ? 0 : 1;
}

