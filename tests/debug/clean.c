/* A correct program: no "heaplab:" error may be reported, and no leak of the sizes it uses.
   It exercises malloc, calloc, realloc, memalign and large blocks. */
#define _GNU_SOURCE
#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    void *p[300];
    for (int i = 0; i < 300; i++) {
        size_t n = 1 + (size_t)i * 37 % 3001;
        p[i] = (i % 3 == 0) ? calloc(n, 1) : malloc(n);
        memset(p[i], i, n);
        if (i % 5 == 0)
            p[i] = realloc(p[i], n * 2 + 1);
    }
    void *a = memalign(256, 3001);
    void *big = malloc(5 << 20);
    memset(big, 7, 5 << 20);
    big = realloc(big, 6 << 20);
    for (int i = 0; i < 300; i += 2) free(p[i]);
    for (int i = 1; i < 300; i += 2) free(p[i]);
    free(a);
    free(big);
    return ((uintptr_t)a % 256) != 0;
}
