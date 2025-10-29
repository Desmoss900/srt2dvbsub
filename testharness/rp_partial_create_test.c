#include <stdio.h>
#include <stdlib.h>
#include "../src/render_pool.h"

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("partial-create-test: attempting init(4)\n");
    int r = render_pool_init(4);
    if (r == 0) {
        printf("partial-create-test: render_pool_init succeeded (pool active)\n");
        render_pool_shutdown();
        return 0;
    }
    printf("partial-create-test: render_pool_init failed as expected (r=%d)\n", r);
    /* ensure shutdown is safe to call even after partial failure */
    render_pool_shutdown();
    return 0;
}
