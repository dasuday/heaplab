/*
 * threads.c - many threads calling malloc/free/realloc/memalign at once.   (GIVEN)
 * Run under LD_PRELOAD=build/libmm.so. Every block is filled with a per-thread pattern and
 * checked before it is freed, so lost updates in the allocator show up as corrupted blocks
 * (or as a crash). Exit status 0 means success.
 */
#define _GNU_SOURCE
#include <malloc.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NTHREADS 8
#define NOPS     40000
#define SLOTS    512

static int failed;

static uint64_t rnd(uint64_t *s)
{
    *s ^= *s << 13; *s ^= *s >> 7; *s ^= *s << 17;
    return *s;
}

static void *worker(void *arg)
{
    int t = (int)(intptr_t)arg;
    unsigned char *slot[SLOTS] = {0};
    size_t size[SLOTS] = {0};
    uint64_t s = 0x9e3779b97f4a7c15ULL * (uint64_t)(t + 1);
    unsigned char pat = (unsigned char)(0x11 * (t + 1));

    for (int i = 0; i < NOPS && !failed; i++) {
        int k = (int)(rnd(&s) % SLOTS);
        if (slot[k]) {
            for (size_t j = 0; j < size[k]; j++)
                if (slot[k][j] != pat) {
                    fprintf(stderr, "thread %d: block %p corrupted at offset %zu\n", t, (void *)slot[k], j);
                    failed = 1;
                    break;
                }
            if (rnd(&s) % 4 == 0) {                  /* realloc instead of free */
                size_t n = 1 + rnd(&s) % 2000;
                slot[k] = realloc(slot[k], n);
                if (!slot[k]) { failed = 1; break; }
                memset(slot[k], pat, n);
                size[k] = n;
                continue;
            }
            free(slot[k]);
            slot[k] = NULL;
        } else {
            size_t n = 1 + rnd(&s) % (rnd(&s) % 16 == 0 ? 300000 : 1024);
            if (rnd(&s) % 8 == 0) {
                void *p = NULL;
                if (posix_memalign(&p, 64, n) != 0 || ((uintptr_t)p & 63)) { failed = 1; break; }
                slot[k] = p;
            } else {
                slot[k] = malloc(n);
            }
            if (!slot[k]) { failed = 1; break; }
            memset(slot[k], pat, n);
            size[k] = n;
        }
    }
    for (int k = 0; k < SLOTS; k++)
        free(slot[k]);
    return NULL;
}

int main(void)
{
    pthread_t th[NTHREADS];
    for (int t = 0; t < NTHREADS; t++)
        pthread_create(&th[t], NULL, worker, (void *)(intptr_t)t);
    for (int t = 0; t < NTHREADS; t++)
        pthread_join(th[t], NULL);
    printf("%s\n", failed ? "FAIL" : "OK");
    return failed;
}
