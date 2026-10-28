/* Frees the same block twice.
   Expected: "heaplab: double-free ...". */
#include <stdlib.h>

int main(void)
{
    void *volatile p = malloc(64);
    void *volatile other = malloc(64);
    free(p);
    free(other);
    free(p);                     /* bug */
    return 0;
}
