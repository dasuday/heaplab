/* Writes one byte past the end of a 24-byte block.
   Expected (LD_PRELOAD=build/libmm-debug.so): "heaplab: heap-buffer-overflow ..." when it is freed. */
#include <stdlib.h>
#include <string.h>

int main(void)
{
    char *volatile p = malloc(24);
    memset(p, 'a', 25);          /* bug: 25 bytes into a 24-byte block */
    free(p);
    return 0;
}
