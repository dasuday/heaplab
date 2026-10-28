/*
 * vmexplore.c - watch glibc's malloc use brk and mmap.             (GIVEN)
 *
 * Prints this process's memory map with your vm_kind() labels, then allocates a small block and
 * a large block and shows where each one landed and how the program break (sbrk(0)) moved.
 *
 *   build/vmexplore
 *   strace -e trace=brk,mmap,munmap build/vmexplore     # see the system calls behind malloc
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vmmap.h"

static void show_map(void)
{
    FILE *f = fopen("/proc/self/maps", "r");
    char line[512];
    printf("%-8s %s\n", "kind", "region (from /proc/self/maps)");
    while (f && fgets(line, sizeof line, f)) {
        unsigned long start;
        struct vm_region r;
        if (sscanf(line, "%lx-", &start) == 1 && vm_find((void *)start, &r) == 0)
            printf("%-8s %s", vm_kind(&r), line);
        else
            printf("%-8s %s", "?", line);
    }
    if (f) fclose(f);
}

static void where(const char *what, void *p)
{
    struct vm_region r;
    if (vm_find(p, &r) == 0)
        printf("%-28s %p  -> %-6s [%#lx, %#lx) %s\n", what, p, vm_kind(&r), (unsigned long)r.start,
               (unsigned long)r.end, r.path);
    else
        printf("%-28s %p  -> (vm_find found nothing)\n", what, p);
}

int main(void)
{
    char *brk_start = sbrk(0);                  /* before anything has called malloc */
    show_map();
    char *brk_map = sbrk(0);
    void *small = malloc(100);
    char *brk_small = sbrk(0);
    void *big = malloc(1 << 20);
    char *brk_big = sbrk(0);
    printf("\nprogram break at the start of main: %p\n", (void *)brk_start);
    printf("after printing the map:             %p  (+%ld bytes: stdio's buffers came from malloc,\n"
           "                                    which grew the heap with brk)\n", (void *)brk_map, (long)(brk_map - brk_start));
    printf("after malloc(100):                  %p  (+%ld: it fit in the heap glibc already had)\n",
           (void *)brk_small, (long)(brk_small - brk_map));
    printf("after malloc(1 MiB):                %p  (+%ld: large blocks get their own mmap)\n\n",
           (void *)brk_big, (long)(brk_big - brk_small));
    where("malloc(100)", small);
    where("malloc(1 MiB)", big);
    int local;
    where("a local variable", &local);
    where("main", (void *)main);
    free(small);
    free(big);
    return 0;
}
