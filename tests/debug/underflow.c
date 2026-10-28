/* Writes one byte before the start of a block.
   Expected: "heaplab: heap-buffer-overflow ..." when it is freed. */
#include <stdlib.h>

int main(void)
{
    char *volatile p = malloc(40);
    char *volatile q = p - 1;
    *q = 'x';                    /* bug: p[-1] */
    free(p);
    return 0;
}
