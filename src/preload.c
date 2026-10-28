/*
 * preload.c - the C library's malloc API on top of your mm.c.    (STUDENT FILE: week 3A)
 *
 * `make` builds this file together with mm.c and memlib.c into build/libmm.so (and, with
 * -DMM_DEBUG, build/libmm-debug.so). Run any program on your allocator with
 *
 *     LD_PRELOAD=$PWD/build/libmm.so ls -l
 *
 * The dynamic linker then resolves malloc, free, ... in the program and in every shared library,
 * libc included, to the definitions below (CS:APP 7.13.3, run-time library interpositioning).
 *
 * The wrappers are complete except for two things you must add in week 3A:
 *
 *   TODO 1  Thread safety. Real programs call malloc from many threads at once, and mm.c is not
 *           reentrant. Protect every call into mm_* with one pthread mutex (lock_heap/unlock_heap).
 *
 *   TODO 2  fork() safety. If one thread forks while another holds the lock, the child gets a
 *           copy of a locked mutex whose owner does not exist in the child: its first malloc
 *           deadlocks. Register handlers with pthread_atfork() (in setup) that take the lock
 *           before fork and release it in both the parent and the child afterwards.
 *
 * Nothing in this file may call malloc or printf (they would call back into your allocator).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "memlib.h"
#include "mm.h"

static int ready;

static void lock_heap(void)
{
    /* TODO 1 */
}

static void unlock_heap(void)
{
    /* TODO 1 */
}

static void ensure_ready(void)          /* call with the lock held */
{
    if (!ready) {
        mem_init();
        mm_init();
        ready = 1;
    }
}

__attribute__((constructor)) static void setup(void)
{
    /* TODO 2: pthread_atfork(...) */
}

#ifdef MM_DEBUG
/* week 3B: remember who called malloc, and run the final checks when the program exits */
#define SET_SITE() (mm_debug_site = __builtin_return_address(0))
__attribute__((destructor)) static void finish(void)
{
    lock_heap();
    if (ready)
        mm_debug_at_exit();
    unlock_heap();
}
#else
#define SET_SITE() ((void)0)
#endif

void *malloc(size_t size)
{
    lock_heap();
    ensure_ready();
    SET_SITE();
    void *p = mm_malloc(size ? size : 1);       /* glibc returns a unique pointer for malloc(0) */
    unlock_heap();
    if (p == NULL)
        errno = ENOMEM;
    return p;
}

void free(void *ptr)
{
    if (ptr == NULL)
        return;
    lock_heap();
    ensure_ready();
    mm_free(ptr);
    unlock_heap();
}

void *calloc(size_t nmemb, size_t size)
{
    if (nmemb != 0 && size > SIZE_MAX / nmemb) {
        errno = ENOMEM;
        return NULL;
    }
    if (nmemb == 0 || size == 0)
        nmemb = size = 1;
    lock_heap();
    ensure_ready();
    SET_SITE();
    void *p = mm_calloc(nmemb, size);
    unlock_heap();
    if (p == NULL)
        errno = ENOMEM;
    return p;
}

void *realloc(void *ptr, size_t size)
{
    if (ptr == NULL) {
        lock_heap();
        ensure_ready();
        SET_SITE();
        void *p = mm_malloc(size ? size : 1);
        unlock_heap();
        if (p == NULL)
            errno = ENOMEM;
        return p;
    }
    if (size == 0) {                            /* glibc frees and returns NULL */
        free(ptr);
        return NULL;
    }
    lock_heap();
    ensure_ready();
    SET_SITE();
    void *p = mm_realloc(ptr, size);
    unlock_heap();
    if (p == NULL)
        errno = ENOMEM;
    return p;
}

void *reallocarray(void *ptr, size_t nmemb, size_t size)
{
    if (nmemb != 0 && size > SIZE_MAX / nmemb) {
        errno = ENOMEM;
        return NULL;
    }
    return realloc(ptr, nmemb * size);
}

static void *aligned(size_t alignment, size_t size)
{
    if (alignment <= MM_ALIGNMENT)
        return malloc(size);
    lock_heap();
    ensure_ready();
    SET_SITE();
    void *p = mm_memalign(alignment, size ? size : 1);
    unlock_heap();
    if (p == NULL)
        errno = ENOMEM;
    return p;
}

static int power_of_two(size_t x) { return x != 0 && (x & (x - 1)) == 0; }

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
    if (!power_of_two(alignment) || alignment % sizeof(void *) != 0)
        return EINVAL;
    int saved = errno;
    void *p = aligned(alignment, size);
    errno = saved;                              /* posix_memalign reports errors only by return value */
    if (p == NULL)
        return ENOMEM;
    *memptr = p;
    return 0;
}

void *memalign(size_t alignment, size_t size)
{
    if (!power_of_two(alignment)) {
        errno = EINVAL;
        return NULL;
    }
    return aligned(alignment, size);
}

void *aligned_alloc(size_t alignment, size_t size)
{
    return memalign(alignment, size);
}

void *valloc(size_t size)
{
    return aligned((size_t)sysconf(_SC_PAGESIZE), size);
}

void *pvalloc(size_t size)
{
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    return aligned(page, (size + page - 1) & ~(page - 1));
}

size_t malloc_usable_size(void *ptr)
{
    if (ptr == NULL)
        return 0;
    lock_heap();
    size_t n = mm_usable_size(ptr);
    unlock_heap();
    return n;
}
