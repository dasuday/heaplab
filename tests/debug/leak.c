/* Never frees a 4242-byte block.
   Expected at exit: "heaplab: leak: 4242 bytes at ... (allocated from ...)". */
#include <stdlib.h>
#include <string.h>

static void *volatile keep;

int main(void)
{
    keep = malloc(4242);         /* bug: never freed */
    memset(keep, 1, 4242);
    void *fine = malloc(1000);
    free(fine);
    return 0;
}
