/*
 * gc.c - YOUR conservative mark-and-sweep collector (week 3C).    (STUDENT FILE)
 *
 * Read gc.h first. A suggested plan for gc_collect():
 *
 *   1. Spill the callee-saved registers onto the stack with __builtin_unwind_init(), then call a
 *      separate (noinline) function to do the work, so the spilled registers sit in a live stack
 *      frame above the one being scanned. Make sure the compiler cannot turn that call into a
 *      tail call (which would discard the frame).
 *   2. Walk the heap once with mm_first_block/mm_next_block and record every allocated block.
 *      The walk is in address order, so this array is sorted: finding the block that contains an
 *      address is then a binary search. Get the array's memory from mem_map, not from the heap.
 *   3. Mark: scan the roots (gc.h lists them) one aligned 8-byte word at a time. A word that
 *      points into a managed, unmarked block marks it (mm_set_gcbits) and pushes it on an
 *      explicit stack (again from mem_map). Then pop blocks and scan their payloads. Do not mark
 *      recursively: a long linked list would overflow the C stack.
 *   4. Sweep: free every managed block that is not marked; clear the mark on the others.
 *
 * The stack's top is __libc_stack_end (declared below). Its bottom, for scanning, is the address
 * of a local variable in your scanning function. The data and bss segments run from __data_start
 * to _end.
 *
 * Test with:  make && build/tests/gc_test
 */
#include <stdint.h>
#include <string.h>

#include "gc.h"
#include "memlib.h"
#include "mm.h"

extern void *__libc_stack_end;           /* highest address of the main thread's stack (glibc) */
extern char __data_start[], _end[];      /* start of .data and end of .bss (set by the linker) */

void gc_init(void)
{
    /* TODO */
}

void *gc_malloc(size_t size)
{
    (void)size;
    return NULL;     /* TODO: allocate with mm_malloc, zero it, and mark it GC_MANAGED */
}

size_t gc_collect(void)
{
    return 0;        /* TODO */
}
