/*
 * mtrace.c - record the malloc calls of any program as a heaplab trace.   (GIVEN)
 *
 *   make tools                                   # builds build/libmtrace.so
 *   HEAPLAB_TRACE=out.rep LD_PRELOAD=build/libmtrace.so ls -l /usr/bin > /dev/null
 *   build/mdriver -f out.rep
 *
 * This is run-time library interpositioning (CS:APP 7.13.3): the dynamic linker resolves
 * malloc, free, calloc and realloc to the definitions below, which log each call and then
 * forward it to glibc's versions, found with dlsym(RTLD_NEXT, ...).
 *
 * Environment variables:
 *   HEAPLAB_TRACE      output file (default heaplab.rep); the program's pid is appended if the
 *                      file already exists, so child processes do not overwrite it
 *   HEAPLAB_TRACE_MAX  stop recording after this many operations (default 100000)
 *
 * Calls to memalign, posix_memalign and aligned_alloc are not recorded (their blocks are
 * simply unknown to the trace), and nothing in this file may itself call malloc.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static void *(*real_malloc)(size_t);
static void *(*real_calloc)(size_t, size_t);
static void *(*real_realloc)(void *, size_t);
static void  (*real_free)(void *);

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static __thread int busy;            /* set while we are inside our own code */
static int fd = -1, done;
static long nops, maxops = 100000;
static unsigned long next_id;

/* dlsym itself may call calloc before we know where glibc's is: serve it from this buffer */
static char boot[4096];
static size_t boot_used;

/* ---- pointer -> id table: open addressing, grown with mmap */
typedef struct { uintptr_t key; unsigned long id; } slot_t;
static slot_t *tab;
static size_t tab_cap, tab_n;
#define TOMB ((uintptr_t)1)

static size_t hash(uintptr_t k) { k ^= k >> 33; k *= 0xff51afd7ed558ccdULL; k ^= k >> 33; return (size_t)k; }

static void tab_put(uintptr_t k, unsigned long id);

static void tab_grow(void)
{
    slot_t *old = tab;
    size_t oldcap = tab_cap;
    tab_cap = oldcap ? oldcap * 2 : (1 << 16);
    tab = mmap(NULL, tab_cap * sizeof(slot_t), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    tab_n = 0;
    for (size_t i = 0; i < oldcap; i++)
        if (old[i].key > TOMB)
            tab_put(old[i].key, old[i].id);
    if (old)
        munmap(old, oldcap * sizeof(slot_t));
}

static void tab_put(uintptr_t k, unsigned long id)
{
    if ((tab_n + 1) * 2 > tab_cap)
        tab_grow();
    size_t i = hash(k) & (tab_cap - 1);
    while (tab[i].key > TOMB)
        i = (i + 1) & (tab_cap - 1);
    tab[i].key = k;
    tab[i].id = id;
    tab_n++;
}

static long tab_take(uintptr_t k)        /* remove k; its id, or -1 if unknown */
{
    if (!tab)
        return -1;
    size_t i = hash(k) & (tab_cap - 1);
    while (tab[i].key != 0) {
        if (tab[i].key == k) {
            tab[i].key = TOMB;
            return (long)tab[i].id;
        }
        i = (i + 1) & (tab_cap - 1);
    }
    return -1;
}

/* ---- output: a buffer flushed with write(2) */
static char out[1 << 16];
static size_t out_len;

static void flush(void)
{
    size_t off = 0;
    while (fd >= 0 && off < out_len) {
        ssize_t w = write(fd, out + off, out_len - off);
        if (w <= 0) break;
        off += (size_t)w;
    }
    out_len = 0;
}

static void emit(const char *line)
{
    size_t n = strlen(line);
    if (out_len + n > sizeof out)
        flush();
    memcpy(out + out_len, line, n);
    out_len += n;
}

static void open_output(void)
{
    const char *path = getenv("HEAPLAB_TRACE");
    const char *max = getenv("HEAPLAB_TRACE_MAX");
    char name[512];
    if (!path) path = "heaplab.rep";
    if (max) maxops = atol(max);
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
        snprintf(name, sizeof name, "%s.%d", path, (int)getpid());
        fd = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    }
    emit("# recorded with heaplab mtrace\n");
}

static void init(void)
{
    if (real_malloc) return;
    busy = 1;
    real_malloc = dlsym(RTLD_NEXT, "malloc");
    real_calloc = dlsym(RTLD_NEXT, "calloc");
    real_realloc = dlsym(RTLD_NEXT, "realloc");
    real_free = dlsym(RTLD_NEXT, "free");
    busy = 0;
}

static int recording(void)
{
    if (busy || done) return 0;
    if (fd < 0) open_output();
    if (nops >= maxops) { done = 1; flush(); return 0; }
    return 1;
}

static void record_new(void *p, const char *fmt, unsigned long a, unsigned long b)
{
    char line[96];
    unsigned long id = next_id++;
    tab_put((uintptr_t)p, id);
    snprintf(line, sizeof line, fmt, id, a, b);
    emit(line);
    nops++;
}

void *malloc(size_t n)
{
    if (!real_malloc) {
        if (busy) {                       /* inside dlsym */
            void *p = boot + boot_used;
            boot_used += (n + 15) & ~(size_t)15;
            return boot_used <= sizeof boot ? p : NULL;
        }
        init();
    }
    void *p = real_malloc(n);
    if (p) {
        pthread_mutex_lock(&lock);
        if (recording()) { busy = 1; record_new(p, "a %lu %lu\n", n, 0); busy = 0; }
        pthread_mutex_unlock(&lock);
    }
    return p;
}

void *calloc(size_t n, size_t s)
{
    if (!real_calloc) {
        if (busy) {
            void *p = malloc(n * s);
            if (p) memset(p, 0, n * s);
            return p;
        }
        init();
    }
    void *p = real_calloc(n, s);
    if (p) {
        pthread_mutex_lock(&lock);
        if (recording()) { busy = 1; record_new(p, "c %lu %lu %lu\n", n, s); busy = 0; }
        pthread_mutex_unlock(&lock);
    }
    return p;
}

void free(void *p)
{
    if (p == NULL) return;
    if ((char *)p >= boot && (char *)p < boot + sizeof boot) return;
    if (!real_free) init();
    pthread_mutex_lock(&lock);
    if (!busy && !done) {
        busy = 1;
        long id = tab_take((uintptr_t)p);
        if (id >= 0 && fd >= 0) {
            char line[48];
            snprintf(line, sizeof line, "f %ld\n", id);
            emit(line);
            nops++;
        }
        busy = 0;
    }
    pthread_mutex_unlock(&lock);
    real_free(p);
}

void *realloc(void *p, size_t n)
{
    if (p == NULL) return malloc(n);
    if ((char *)p >= boot && (char *)p < boot + sizeof boot) {
        void *q = malloc(n);
        if (q) memcpy(q, p, n);    /* boot blocks are tiny and never freed; over-reading is safe here */
        return q;
    }
    if (!real_realloc) init();
    void *q = real_realloc(p, n);
    pthread_mutex_lock(&lock);
    if (!busy && !done && (q != NULL || n == 0)) {
        busy = 1;
        long id = tab_take((uintptr_t)p);
        char line[64];
        if (id >= 0 && n == 0) {
            snprintf(line, sizeof line, "f %ld\n", id);
            emit(line);
            nops++;
        } else if (id >= 0) {
            tab_put((uintptr_t)q, (unsigned long)id);
            snprintf(line, sizeof line, "r %ld %lu\n", id, n);
            emit(line);
            nops++;
        }
        busy = 0;
    }
    pthread_mutex_unlock(&lock);
    return q;
}

__attribute__((destructor)) static void finish(void)
{
    pthread_mutex_lock(&lock);
    flush();
    done = 1;
    pthread_mutex_unlock(&lock);
}
