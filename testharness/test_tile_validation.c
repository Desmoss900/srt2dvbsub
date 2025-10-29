#include <assert.h>
#include <stdio.h>
#include "../src/render_ass.h"

/* Provide a minimal debug_level for render_ass.c linking. */
int debug_level = 0;
#include "../src/debug.h"

int main(void) {
    /* NULL bitmap */
    assert(render_ass_validate_image_tile(10, 10, 12, NULL) == 0);

    /* Zero dimensions */
    char dummy = 0;
    assert(render_ass_validate_image_tile(0, 10, 10, &dummy) == 0);
    assert(render_ass_validate_image_tile(10, 0, 10, &dummy) == 0);

    /* stride too small */
    assert(render_ass_validate_image_tile(10, 5, 8, &dummy) == 0);

    /* absurdly large tile */
    assert(render_ass_validate_image_tile(1000000, 1000000, 1000000, &dummy) == 0);

    /* reasonable tile */
    assert(render_ass_validate_image_tile(10, 10, 16, &dummy) == 1);

    printf("test_tile_validation: all checks passed\n");
    return 0;
}
