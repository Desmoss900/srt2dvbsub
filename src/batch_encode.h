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
 * batch_encode.h
 * ---------------
 * Prtovides a small orchestration layer that discovers .ts inputs, resolves subtitle
 * templates to concrete SRT paths, mirrors the directory structure into an
 * output tree, and invokes the encoder for each file. All heavy lifting is
 * still handled by the main srt2dvbsub pipeline; this module only builds the
 * per-file argument vectors and performs filesystem bookkeeping.
 */

#ifndef SRT2DVB_BATCH_ENCODE_H
#define SRT2DVB_BATCH_ENCODE_H

#include <stddef.h>

typedef struct BatchEncodeTemplate {
	char *pattern;  /* Template string, e.g. "${BASENAME}.en.srt" */
	char lang[4];   /* 3-letter DVB language code, NUL-terminated */
} BatchEncodeTemplate;

typedef struct BatchEncodeConfig {
	char *input_dir;         /* Root containing .ts inputs (required) */
	char *output_dir;        /* Root for encoded outputs (required) */
	char *srt_dir;           /* Root to mirror SRT lookup (required) */

	BatchEncodeTemplate *templates;
	size_t template_count;

	char **forward_args;     /* Non-batch CLI args to forward verbatim */
	size_t forward_count;

	int dry_run;             /* Non-zero => print actions only */
} BatchEncodeConfig;

/**
 * batch_encode_requested - Check if batch encoding mode was requested.
 * @argc: Argument count from main()
 * @argv: Argument vector from main()
 *
 * Scans the command-line arguments to determine whether the user requested
 * batch encoding mode (typically via a --batch flag or similar).
 *
 * Return: Non-zero if batch mode was requested, 0 otherwise.
 */
int batch_encode_requested(int argc, char **argv);

/**
 * batch_encode_init_defaults - Initialize a BatchEncodeConfig with default values.
 * @cfg: Pointer to the configuration structure to initialize
 *
 * Sets all pointers to NULL, counts to 0, and flags to their default state.
 * Must be called before using a BatchEncodeConfig structure.
 *
 * Return: 0 on success, non-zero on error.
 */
int batch_encode_init_defaults(BatchEncodeConfig *cfg);

/**
 * batch_encode_free - Release all resources held by a BatchEncodeConfig.
 * @cfg: Pointer to the configuration structure to free
 *
 * Frees all dynamically allocated memory associated with the configuration,
 * including input/output/srt directory paths, encoder path, templates, and
 * forwarded arguments. After calling this function, the structure should not
 * be used unless reinitialized with batch_encode_init_defaults().
 *
 * It is safe to call this function on a zeroed or default-initialized structure.
 */
void batch_encode_free(BatchEncodeConfig *cfg);

/**
 * batch_encode_add_template - Add a subtitle template to the batch configuration.
 * @cfg: Pointer to the configuration structure
 * @entry: Template entry string in the format "pattern:lang" (e.g., "${BASENAME}.en.srt:eng")
 *
 * Parses the template entry and adds it to the configuration's template array.
 * The pattern may contain placeholders like ${BASENAME} which will be replaced
 * during batch processing. The language code should be a 3-letter DVB language code.
 *
 * Return: 0 on success, non-zero on error (e.g., invalid format, memory allocation failure).
 */
int batch_encode_add_template(BatchEncodeConfig *cfg, const char *entry);

/**
 * batch_encode_parse_cli - Parse command-line arguments for batch encoding mode.
 * @argc: Argument count from main()
 * @argv: Argument vector from main()
 * @cfg: Pointer to the configuration structure to populate
 *
 * Parses command-line arguments specific to batch encoding mode and populates
 * the provided configuration structure. This function handles batch-specific
 * options such as input directory, output directory, SRT directory, encoder path,
 * subtitle templates, and dry-run flag. Arguments not recognized as batch-specific
 * are collected in the forward_args array to be passed to individual encode operations.
 *
 * The configuration structure should be initialized with batch_encode_init_defaults()
 * before calling this function.
 *
 * Return: 0 on success, non-zero on error (e.g., missing required arguments,
 *         invalid options, memory allocation failure).
 */
int batch_encode_parse_cli(int argc, char **argv, BatchEncodeConfig *cfg);

/**
 * batch_encode_run - Execute batch encoding operation.
 * @cfg: Pointer to the fully configured batch encoding configuration
 * @argv0: Original argv[0] from main, used as default encoder 
 *
 * Performs the batch encoding operation by:
 * 1. Discovering all .ts files in the input directory tree
 * 2. For each .ts file, resolving subtitle template patterns to concrete SRT paths
 * 3. Mirroring the directory structure from input to output tree
 * 4. Invoking the encoder for each file with the appropriate arguments
 *
 * The function respects the dry_run flag in the configuration. If set, it will
 * only print the actions that would be performed without actually executing them.
 *
 * All actual encoding work is delegated to the main srt2dvbsub pipeline; this
 * function only handles file discovery, path resolution, and process orchestration.
 *
 * Return: 0 on success, non-zero on error (e.g., directory access failure,
 *         encoder execution failure, I/O errors).
 */
int batch_encode_run(const BatchEncodeConfig *cfg, const char *argv0);

#endif /* SRT2DVB_BATCH_ENCODE_H */
