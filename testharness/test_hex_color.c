#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../src/render_ass.h"
/* Test harness provides a minimal debug_level for render_ass logging. */
int debug_level = 0;

int main(void) {
    char out[32];

    render_ass_hex_to_ass_color(NULL, out, sizeof(out));
    assert(strcmp(out, "&H00FFFFFF") == 0);

    render_ass_hex_to_ass_color("#112233", out, sizeof(out));
    /* #RRGGBB -> inv alpha 0xFF, BB GG RR -> &HFF332211 */
    assert(strcmp(out, "&HFF332211") == 0);

    render_ass_hex_to_ass_color("#80112233", out, sizeof(out));
    /* #AARRGGBB: a=0x80 -> inv=0x7F ; output &H7F332211 */
    assert(strcmp(out, "&H7F332211") == 0);

    render_ass_hex_to_ass_color("#aBcDeF", out, sizeof(out));
    /* lowercase/uppercase mix -> &HFFEFCDAB (R=AB G=CD B=EF) */
    assert(strcmp(out, "&HFFEFCDAB") == 0);

    /* malformed -> fallback opaque white */
    render_ass_hex_to_ass_color("#GGHHII", out, sizeof(out));
    assert(strcmp(out, "&H00FFFFFF") == 0);

    printf("test_hex_color: all checks passed\n");
    return 0;
}
