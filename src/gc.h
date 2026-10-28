/*
 * gc.h - a conservative mark-and-sweep garbage collector on top of your allocator.
 *                                                                  (GIVEN: do not modify)
 *
 * Week 3C (CS:APP 9.10). Blocks from gc_malloc are "managed": the program never frees them;
 * gc_collect() frees every managed block that the program can no longer reach.
 *
 * Roots (where reachable pointers start):
 *   - the stack, from the current stack pointer up to __libc_stack_end, including the
 *     callee-saved registers (spill them first with __builtin_unwind_init());
 *   - the program's data and bss segments, from __data_start to _end;
 *   - the payload of every allocated block that is NOT managed (mm_malloc blocks).
 * Any aligned 8-byte word whose value points into a managed block (to its first byte or
 * anywhere inside its payload) keeps that block alive, and the block's payload is then scanned
 * in turn. This is "conservative": an integer that happens to look like a pointer also keeps a
 * block alive, which is safe but may retain some garbage.
 *
 * Limitations you may assume: one thread; managed blocks are smaller than MM_MMAP_THRESHOLD;
 * pointers stored in blocks of MM_MMAP_THRESHOLD bytes or more are not roots.
 */
#ifndef GC_H
#define GC_H

#include <stddef.h>

#define GC_MANAGED 1u   /* gc bit: this block came from gc_malloc */
#define GC_MARK    2u   /* gc bit: reached during the current collection */

void   gc_init(void);              /* call once, at the start of main, after mem_init()/mm_init() */
void  *gc_malloc(size_t size);     /* a zero-filled managed block, or NULL */
size_t gc_collect(void);           /* free unreachable managed blocks; returns how many were freed */

#endif
