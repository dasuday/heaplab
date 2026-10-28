/*
 * vmmap.h - where does an address live?                           (GIVEN: do not modify)
 *
 * Week 1 warm-up (CS:APP 9.7.2, "Linux virtual memory system"). Linux describes every region
 * of a process's address space in the text file /proc/self/maps, one region per line:
 *
 *   start-end         perms offset   dev   inode   path
 *   55d0c8a00000-55d0c8a01000 r-xp 00001000 08:01 1835023  /home/you/heaplab/build/vmexplore
 *   7ffd4a1b2000-7ffd4a1d3000 rw-p 00000000 00:00 0        [stack]
 *
 * You implement vm_find and vm_kind in src/vmmap.c.
 */
#ifndef VMMAP_H
#define VMMAP_H

#include <stdint.h>

struct vm_region {
    uintptr_t start;        /* first address of the region */
    uintptr_t end;          /* first address after it */
    char perms[5];          /* e.g. "r-xp": read, write, execute, p(rivate) or s(hared) */
    unsigned long offset;   /* offset into the mapped file */
    char path[256];         /* file name, "[heap]", "[stack]", "[vdso]", ... or "" if anonymous */
};

/* Find the region containing addr: fill *out and return 0, or return -1 if addr is not mapped. */
int vm_find(const void *addr, struct vm_region *out);

/* Classify a region. Returns one of these string constants:
 *   "text"    the program's own code          (its executable file, perms contain x)
 *   "rodata"  the program's read-only data    (its executable file, neither w nor x)
 *   "data"    the program's .data/.bss        (its executable file, perms contain w)
 *   "heap"    path "[heap]": memory from brk/sbrk
 *   "stack"   path "[stack]"
 *   "lib"     any other file: shared libraries (libc.so.6, ld-linux-x86-64.so.2), mapped files
 *   "anon"    no path (or a path starting with "[anon"): mmap(MAP_ANONYMOUS) memory
 *   "vdso"    "[vdso]", "[vvar]" or "[vsyscall]": pages the kernel maps into every process
 * The program's own executable is the file /proc/self/exe links to. */
const char *vm_kind(const struct vm_region *r);

#endif
