/*
 * api_test.c - week 3A and 3C checks of mm.c, linked directly (no LD_PRELOAD).   (GIVEN)
 * Prints one PASS or FAIL line per check; grade.py counts them.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memlib.h"
#include "mm.h"

static int failures;
static char why[256];

#define REQUIRE(cond, ...) do { if (!(cond)) { snprintf(why, sizeof why, __VA_ARGS__); return false; } } while (0)

static void run(const char *name, bool (*fn)(void))
{
    mem_reset_brk();
    mm_init();
    why[0] = '\0';
    bool ok = fn();
    if (ok && !mm_checkheap(__LINE__)) {
        ok = false;
        snprintf(why, sizeof why, "mm_checkheap failed afterwards");
    }
    printf("%s: %-40s %s\n", ok ? "PASS" : "FAIL", name, ok ? "" : why);
    failures += !ok;
}

/* ---------------------------------------------------------------- 3A */
static bool t_memalign(void)
{
    void *keep[64];
    int n = 0;
    size_t sizes[] = {1, 100, 5000, 300000};
    for (size_t a = 16; a <= 65536; a *= 2)
        for (int s = 0; s < 4; s++) {
            char *p = mm_memalign(a, sizes[s]);
            REQUIRE(p != NULL, "mm_memalign(%zu, %zu) returned NULL", a, sizes[s]);
            REQUIRE((uintptr_t)p % a == 0, "mm_memalign(%zu, %zu) = %p is not aligned", a, sizes[s], (void *)p);
            REQUIRE(mm_usable_size(p) >= sizes[s], "usable size of a memalign block is too small");
            memset(p, (int)a, sizes[s]);
            if (n < 64) keep[n++] = p;
            else mm_free(p);
        }
    for (int i = 0; i < n; i++)
        mm_free(keep[i]);
    return true;
}

static bool t_memalign_bad(void)
{
    REQUIRE(mm_memalign(24, 10) == NULL, "alignment 24 should be rejected");
    REQUIRE(mm_memalign(0, 10) == NULL, "alignment 0 should be rejected");
    REQUIRE(mm_memalign(8, 10) == NULL, "alignment 8 (< 16) should be rejected");
    return true;
}

static bool t_usable(void)
{
    char *p[200];
    size_t u[200];
    for (int i = 0; i < 200; i++) {
        size_t n = 1 + (size_t)(i * 37 % 700);
        p[i] = mm_malloc(n);
        REQUIRE(p[i] != NULL, "mm_malloc failed");
        u[i] = mm_usable_size(p[i]);
        REQUIRE(u[i] >= n, "mm_usable_size(%p) = %zu < requested %zu", (void *)p[i], u[i], n);
        memset(p[i], i & 0xff, u[i]);           /* the caller may use every usable byte */
    }
    for (int i = 0; i < 200; i++)
        for (size_t j = 0; j < u[i]; j++)
            REQUIRE((unsigned char)p[i][j] == (i & 0xff), "writing all usable bytes of one block corrupted another");
    for (int i = 0; i < 200; i++)
        mm_free(p[i]);
    REQUIRE(mm_usable_size(NULL) == 0, "mm_usable_size(NULL) should be 0");
    return true;
}

static bool t_calloc(void)
{
    REQUIRE(mm_calloc(SIZE_MAX / 2, 4) == NULL, "mm_calloc must detect nmemb * size overflow");
    REQUIRE(mm_calloc((size_t)1 << 33, (size_t)1 << 33) == NULL, "mm_calloc must detect nmemb * size overflow");
    for (int round = 0; round < 3; round++) {          /* recycled memory must be zeroed again */
        unsigned char *p = mm_calloc(1000, 3);
        REQUIRE(p != NULL, "mm_calloc failed");
        for (int i = 0; i < 3000; i++)
            REQUIRE(p[i] == 0, "mm_calloc block not zeroed at %d", i);
        memset(p, 0xee, 3000);
        mm_free(p);
    }
    return true;
}

static bool t_large_mapped(void)
{
    size_t base = mem_mapped_bytes(), heap0 = mem_heapsize();
    char *p = mm_malloc(1 << 20);
    REQUIRE(p != NULL, "mm_malloc(1 MiB) failed");
    memset(p, 1, 1 << 20);
    REQUIRE(mem_mapped_bytes() >= base + (1 << 20), "a 1 MiB block should come from mem_map");
    REQUIRE(mem_heapsize() < heap0 + (1 << 19), "a 1 MiB block should not grow the heap");
    mm_free(p);
    REQUIRE(mem_mapped_bytes() == base, "freeing a mapped block should mem_unmap it");
    return true;
}

static bool t_realloc_threshold(void)
{
    size_t base = mem_mapped_bytes();
    unsigned char *p = mm_malloc(100);
    for (int i = 0; i < 100; i++) p[i] = (unsigned char)i;
    p = mm_realloc(p, 1 << 20);
    REQUIRE(p != NULL, "realloc to 1 MiB failed");
    for (int i = 0; i < 100; i++) REQUIRE(p[i] == (unsigned char)i, "realloc lost data growing to 1 MiB");
    for (int i = 100; i < (1 << 20); i++) p[i] = (unsigned char)(i * 7);
    p = mm_realloc(p, 3 << 20);
    REQUIRE(p != NULL, "realloc to 3 MiB failed");
    for (int i = 100; i < (1 << 20); i++) REQUIRE(p[i] == (unsigned char)(i * 7), "realloc lost data growing to 3 MiB");
    p = mm_realloc(p, 50);
    REQUIRE(p != NULL, "realloc down to 50 bytes failed");
    for (int i = 0; i < 50; i++) REQUIRE(p[i] == (unsigned char)i, "realloc lost data shrinking to 50 bytes");
    mm_free(p);
    REQUIRE(mem_mapped_bytes() == base, "mapped memory left over after freeing everything");
    return true;
}

/* ---------------------------------------------------------------- 3C */
static bool t_heap_walk(void)
{
    char *p[300];
    for (int i = 0; i < 300; i++) {
        p[i] = mm_malloc(16 + (size_t)(i % 13) * 24);
        REQUIRE(p[i] != NULL, "mm_malloc failed");
    }
    for (int i = 0; i < 300; i += 3) { mm_free(p[i]); p[i] = NULL; }
    int seen[300] = {0};
    char *prev = NULL;
    size_t nblocks = 0;
    for (char *b = mm_first_block(); b; b = mm_next_block(b)) {
        REQUIRE(prev == NULL || b > prev, "heap walk is not in increasing address order");
        REQUIRE((uintptr_t)b % 16 == 0, "heap walk returned an unaligned block %p", (void *)b);
        REQUIRE(++nblocks < 100000, "heap walk does not end");
        prev = b;
        for (int i = 0; i < 300; i++)
            if (p[i] == b) {
                seen[i]++;
                REQUIRE(mm_is_allocated(b), "allocated block %p reported as free", (void *)b);
                REQUIRE(mm_block_size(b) >= 16 + (size_t)(i % 13) * 24, "mm_block_size too small for %p", (void *)b);
            }
        if (mm_is_allocated(b)) {
            mm_set_gcbits(b, 3);
            REQUIRE(mm_get_gcbits(b) == 3, "gc bits do not read back");
            mm_set_gcbits(b, 1);
            REQUIRE(mm_get_gcbits(b) == 1, "gc bits do not read back");
        }
    }
    for (int i = 0; i < 300; i++)
        REQUIRE(p[i] == NULL || seen[i] == 1, "allocated block %d seen %d times in the heap walk", i, seen[i]);
    REQUIRE(mm_checkheap(__LINE__), "setting gc bits broke the heap");
    for (int i = 0; i < 300; i++)
        if (p[i]) { mm_set_gcbits(p[i], 0); mm_free(p[i]); }
    return true;
}

int main(int argc, char **argv)
{
    const char *only = argc > 1 ? argv[1] : "all";
    mem_init();
    if (!strcmp(only, "all") || !strcmp(only, "3a")) {
        run("3A memalign alignments and sizes", t_memalign);
        run("3A memalign rejects bad alignments", t_memalign_bad);
        run("3A usable size", t_usable);
        run("3A calloc overflow and zeroing", t_calloc);
        run("3A large blocks use mem_map", t_large_mapped);
        run("3A realloc across the threshold", t_realloc_threshold);
    }
    if (!strcmp(only, "all") || !strcmp(only, "3c"))
        run("3C heap walk and gc bits", t_heap_walk);
    return failures ? 1 : 0;
}
