/*
 * memlib.h - the memory system your allocator runs on.            (GIVEN: do not modify)
 *
 * The heap is one contiguous region of virtual memory. memlib reserves a large range of
 * addresses once with mmap(PROT_NONE) and makes pages readable and writable only as the
 * break moves up, so it behaves like the sbrk() system call (CS:APP 9.9.1) but can be reset
 * between test traces and never collides with anything else in the process.
 *
 * Your allocator must get all of its memory from this file:
 *   - mem_sbrk()             for the heap (weeks 1-3)
 *   - mem_map()/mem_unmap()  for large blocks outside the heap (week 3)
 * Do not call malloc, sbrk, brk or mmap directly from mm.c.
 */
#ifndef MEMLIB_H
#define MEMLIB_H

#include <stddef.h>
#include <stdint.h>

void   mem_init(void);              /* reserve the heap's address range (called by the driver/shim) */
void   mem_reset_brk(void);         /* empty the heap and give its pages back to the kernel */

void  *mem_sbrk(intptr_t incr);     /* grow the heap by incr >= 0 bytes; returns the old break,
                                       or (void *)-1 with errno = ENOMEM on failure */
void  *mem_heap_lo(void);           /* address of the first heap byte */
void  *mem_heap_hi(void);           /* address of the last heap byte (lo - 1 if the heap is empty) */
size_t mem_heapsize(void);          /* current heap size in bytes */
size_t mem_pagesize(void);          /* the system page size (4096 on x86-64 Linux) */

/* Week 3: large blocks get their own anonymous mapping. len is rounded up to whole pages. */
void  *mem_map(size_t len);         /* returns a page-aligned, zero-filled region, or NULL */
int    mem_unmap(void *addr, size_t len);  /* addr and len exactly as passed to / returned by mem_map */
size_t mem_mapped_bytes(void);      /* bytes currently mapped through mem_map */

#endif
