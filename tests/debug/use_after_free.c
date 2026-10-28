/* Writes into a block after freeing it.
   Expected: "heaplab: use-after-free ..." (when the block leaves quarantine, or at exit). */
#include <stdlib.h>

int main(void)
{
    char *volatile p = malloc(100);
    free(p);
    p[50] = 'x';                 /* bug */
    return 0;
}
