/*
 * vmmap.c - YOUR CODE for the week 1 warm-up.                     (STUDENT FILE)
 *
 * Implement the two functions declared in vmmap.h:
 *
 *   vm_find   open /proc/self/maps, read it line by line (fgets), parse each line
 *             (sscanf with "%lx-%lx %4s %lx ..." is a good start; the path is optional and may be
 *             missing) and return the region whose [start, end) contains addr.
 *   vm_kind   classify the region using its path and permissions (see vmmap.h). To recognise
 *             the program's own executable, compare the path with readlink("/proc/self/exe").
 *
 * Test with:  make && build/tests/vmmap_test
 * Explore:    build/vmexplore     and     strace -e trace=brk,mmap,munmap build/vmexplore
 */
#include "vmmap.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

int vm_find(const void *addr, struct vm_region *out)
{
    (void)addr;
    (void)out;
    return -1;      /* TODO */
}

const char *vm_kind(const struct vm_region *r)
{
    (void)r;
    return "?";     /* TODO */
}
