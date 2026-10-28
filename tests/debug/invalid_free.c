/* Frees a pointer into the middle of a block.
   Expected: "heaplab: invalid-free ...". */
#include <stdlib.h>

int main(void)
{
    char *volatile p = malloc(64);
    free(p + 16);                /* bug: not the pointer malloc returned */
    return 0;
}
