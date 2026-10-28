/*
 * mm.c - YOUR ALLOCATOR.                                         (STUDENT FILE: you rewrite this)
 *
 * The starter version is a "bump" allocator: every malloc takes fresh bytes from the end of the
 * heap and free does nothing. It is correct (the driver accepts it) but it never reuses memory,
 * so its space utilization is terrible. Replace it:
 *
 *   Week 1  implicit free list with boundary tags and immediate coalescing (CS:APP 9.9.6-9.9.12)
 *   Week 2  explicit, segregated free lists and a better realloc (CS:APP 9.9.13-9.9.14)
 *   Week 3  memalign, large blocks with mem_map, a debug mode, and heap walking for the GC
 *
 * Replace this comment with a description of YOUR design: block layout (draw it), how free
 * blocks are found, how they are split and coalesced, and what mm_checkheap verifies.
 *
 * Rules: get memory only from memlib (mem_sbrk, and mem_map in week 3); no malloc/calloc/free
 * from libc, no global or static arrays larger than 256 bytes; every returned pointer must be
 * 16-byte aligned.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "memlib.h"
#include "mm.h"

/* Each block is a 16-byte header holding the payload size, followed by the payload. */
#define HDR 16

static size_t round_up(size_t n)
{
    return (n + MM_ALIGNMENT - 1) & ~(size_t)(MM_ALIGNMENT - 1);
}

int mm_init(void)
{
    return 0;
}

void *mm_malloc(size_t size)
{
    if (size == 0 || size > ((size_t)1 << 40))
        return NULL;
    size_t total = HDR + round_up(size);
    char *p = mem_sbrk((intptr_t)total);
    if (p == (void *)-1)
        return NULL;
    *(size_t *)p = round_up(size);
    return p + HDR;
}

void mm_free(void *ptr)
{
    (void)ptr;   /* the bump allocator never reuses memory */
}

void *mm_realloc(void *ptr, size_t size)
{
    if (ptr == NULL)
        return mm_malloc(size);
    if (size == 0) {
        mm_free(ptr);
        return NULL;
    }
    size_t old = *(size_t *)((char *)ptr - HDR);
    if (size <= old)
        return ptr;
    void *q = mm_malloc(size);
    if (q == NULL)
        return NULL;
    memcpy(q, ptr, old);
    mm_free(ptr);
    return q;
}

void *mm_calloc(size_t nmemb, size_t size)
{
    if (nmemb != 0 && size > SIZE_MAX / nmemb)
        return NULL;
    void *p = mm_malloc(nmemb * size);
    if (p != NULL)
        memset(p, 0, nmemb * size);
    return p;
}

bool mm_checkheap(int lineno)
{
    (void)lineno;   /* TODO (week 1): walk the heap and verify every invariant you rely on */
    return true;
}

/* ---------------------------------------------------------------- week 3A */

void *mm_memalign(size_t alignment, size_t size)
{
    (void)alignment; (void)size;   /* TODO (week 3A) */
    return NULL;
}

size_t mm_usable_size(void *ptr)
{
    return ptr ? *(size_t *)((char *)ptr - HDR) : 0;
}

/* ---------------------------------------------------------------- week 3B (compiled with -DMM_DEBUG) */

#ifdef MM_DEBUG
void *mm_debug_site;

void mm_debug_at_exit(void)
{
    /* TODO (week 3B): report leaks and writes into freed blocks */
}
#endif

/* ---------------------------------------------------------------- week 3C */

void *mm_first_block(void)            { return NULL; }   /* TODO (week 3C) */
void *mm_next_block(void *bp)         { (void)bp; return NULL; }
size_t mm_block_size(void *bp)        { (void)bp; return 0; }
bool mm_is_allocated(void *bp)        { (void)bp; return false; }
unsigned mm_get_gcbits(void *bp)      { (void)bp; return 0; }
void mm_set_gcbits(void *bp, unsigned bits) { (void)bp; (void)bits; }
