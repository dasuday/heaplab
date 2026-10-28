/*
 * mm.h - the interface your allocator implements.                 (GIVEN: do not modify)
 *
 * Every pointer returned to the caller must be aligned to MM_ALIGNMENT (16) bytes, as glibc's
 * malloc guarantees on x86-64 (alignof(max_align_t) == 16).
 */
#ifndef MM_H
#define MM_H

#include <stdbool.h>
#include <stddef.h>

#define MM_ALIGNMENT      16
#define MM_MMAP_THRESHOLD (256 * 1024)   /* week 3A: requests of at least this many bytes use mem_map */

/* ---------------------------------------------------------------- weeks 1-2: the allocator */

int    mm_init(void);                        /* start a new, empty heap; 0 on success, -1 on error.
                                                Called again before every trace, after mem_reset_brk(),
                                                so it must reset all of your global state. */
void  *mm_malloc(size_t size);               /* NULL on failure; malloc(0) may return NULL */
void   mm_free(void *ptr);                   /* ptr is NULL or came from this allocator */
void  *mm_realloc(void *ptr, size_t size);   /* realloc(NULL, n) == malloc(n); realloc(p, 0) frees p
                                                and returns NULL */
void  *mm_calloc(size_t nmemb, size_t size); /* zero-filled; NULL if nmemb * size overflows */
bool   mm_checkheap(int lineno);             /* true if the heap is consistent; called with __LINE__.
                                                Print what is wrong (to stderr) before returning false. */

/* ---------------------------------------------------------------- week 3A: a real malloc */

void  *mm_memalign(size_t alignment, size_t size); /* alignment is a power of two >= 16;
                                                      NULL if it is not */
size_t mm_usable_size(void *ptr);                  /* bytes the caller may use at ptr (0 for NULL) */

/* ---------------------------------------------------------------- week 3B: debug build */

#ifdef MM_DEBUG
extern void *mm_debug_site;   /* return address of the malloc() caller, set by the shim before each call */
void mm_debug_at_exit(void);  /* final checks: leaks and writes into freed blocks */
#endif

/* ---------------------------------------------------------------- week 3C: heap walking for the GC
 *
 * A "block pointer" (bp) below is always the payload address you returned from mm_malloc.
 * Iteration covers the heap only (not blocks from mem_map), in address order, and includes
 * free blocks so the collector can skip them.
 */

void    *mm_first_block(void);            /* first block in the heap, or NULL if the heap is empty */
void    *mm_next_block(void *bp);         /* the block after bp, or NULL at the end */
size_t   mm_block_size(void *bp);         /* usable payload bytes of the block */
bool     mm_is_allocated(void *bp);
unsigned mm_get_gcbits(void *bp);         /* two spare header bits the collector may use */
void     mm_set_gcbits(void *bp, unsigned bits);   /* bits & ~3u must be 0 */

#endif
