/* Frees the address of a local variable.
   Expected: "heaplab: invalid-free ...". */
#include <stdlib.h>

int main(void)
{
    int local = 5;
    int *volatile p = &local;
    free(p);                     /* bug: not from malloc at all */
    return 0;
}
