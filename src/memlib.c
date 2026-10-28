/*
 * memlib.c - a resettable sbrk() built on mmap() and mprotect().   (GIVEN: do not modify)
 *
 * How it works (CS:APP 9.8, memory mapping):
 *   1. mem_init() reserves HEAP_RESERVE bytes of address space with PROT_NONE. Nothing is
 *      allocated yet: touching those addresses would fault.
 *   2. mem_sbrk() moves the break up. Whenever the break passes the last accessible page,
 *      the next pages are made readable and writable with mprotect(). The kernel still only
 *      supplies a physical page when it is first touched (demand paging).
 *   3. mem_reset_brk() hands every page back with madvise(MADV_DONTNEED) and makes the range
 *      inaccessible again, so the next test starts from an empty heap at the same address.
 *
 * This file never calls malloc, so it can also be used inside the LD_PRELOAD library.
 */
#include "memlib.h"

#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define HEAP_RESERVE ((size_t)64 << 30)   /* 64 GiB of address space; only touched pages cost memory */

static char  *heap_start;      /* first byte of the reservation */
static char  *heap_brk;        /* current break: first byte past the heap */
static char  *heap_committed;  /* first byte past the readable/writable pages */
static size_t page_size;
static size_t mapped_bytes;    /* bytes currently handed out by mem_map */

static void die(const char *msg)
{
    ssize_t r = write(2, msg, strlen(msg));
    (void)r;
    _exit(1);
}

static size_t round_page(size_t n)
{
    return (n + page_size - 1) & ~(page_size - 1);
}

void mem_init(void)
{
    if (heap_start != NULL)
        return;
    page_size = (size_t)sysconf(_SC_PAGESIZE);
    void *p = mmap(NULL, HEAP_RESERVE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED)
        die("memlib: could not reserve the heap's address range\n");
    heap_start = heap_brk = heap_committed = p;
}

void mem_reset_brk(void)
{
    if (heap_start == NULL) {
        mem_init();
        return;
    }
    size_t used = (size_t)(heap_committed - heap_start);
    if (used > 0) {
        madvise(heap_start, used, MADV_DONTNEED);
        mprotect(heap_start, used, PROT_NONE);
    }
    heap_brk = heap_committed = heap_start;
}

void *mem_sbrk(intptr_t incr)
{
    if (heap_start == NULL)
        mem_init();
    size_t room = HEAP_RESERVE - (size_t)(heap_brk - heap_start);
    if (incr < 0 || (size_t)incr > room) {
        errno = ENOMEM;
        return (void *)-1;
    }
    char *old_brk = heap_brk;
    char *new_brk = heap_brk + incr;
    if (new_brk > heap_committed) {
        char *new_committed = heap_start + round_page((size_t)(new_brk - heap_start));
        if (mprotect(heap_committed, (size_t)(new_committed - heap_committed), PROT_READ | PROT_WRITE) != 0) {
            errno = ENOMEM;
            return (void *)-1;
        }
        heap_committed = new_committed;
    }
    heap_brk = new_brk;
    return old_brk;
}

void *mem_heap_lo(void)
{
    if (heap_start == NULL)
        mem_init();
    return heap_start;
}

void *mem_heap_hi(void)
{
    if (heap_start == NULL)
        mem_init();
    return heap_brk - 1;
}

size_t mem_heapsize(void)
{
    return (size_t)(heap_brk - heap_start);
}

size_t mem_pagesize(void)
{
    if (page_size == 0)
        page_size = (size_t)sysconf(_SC_PAGESIZE);
    return page_size;
}

void *mem_map(size_t len)
{
    if (page_size == 0)
        mem_init();
    if (len == 0 || len > HEAP_RESERVE)
        return NULL;
    len = round_page(len);
    void *p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED)
        return NULL;
    mapped_bytes += len;
    return p;
}

int mem_unmap(void *addr, size_t len)
{
    len = round_page(len);
    if (munmap(addr, len) != 0)
        return -1;
    mapped_bytes -= len;
    return 0;
}

size_t mem_mapped_bytes(void)
{
    return mapped_bytes;
}
