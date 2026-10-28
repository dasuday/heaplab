/*
 * mdriver.c - replays allocation traces against your allocator.    (GIVEN: do not modify)
 *
 * For every trace it
 *   1. checks correctness: every block is 16-byte aligned, lies inside memory the allocator
 *      owns, never overlaps another live block, keeps its contents until freed, and realloc
 *      and calloc behave as specified;
 *   2. measures space utilization: peak live payload / peak memory footprint
 *      (footprint = heap size + bytes obtained with mem_map), as in CS:APP 9.9.3;
 *   3. measures throughput: operations per second, best of several timed runs without checks.
 * With -l it also replays the traces against the system allocator (glibc malloc) so your
 * throughput can be compared with it on the same machine.
 *
 * Trace format (one operation per line, '#' starts a comment):
 *   a <id> <size>        p[id] = malloc(size)
 *   c <id> <n> <size>    p[id] = calloc(n, size)
 *   r <id> <size>        p[id] = realloc(p[id], size)
 *   f <id>               free(p[id])
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <search.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "memlib.h"
#include "mm.h"

/* ------------------------------------------------------------------ scoring constants */
#define UTIL_WEIGHT     0.60   /* perf index = 0.6 * utilization + 0.4 * throughput score */
#define TPUT_FRACTION   0.50   /* throughput score is 1 at half of glibc's throughput */
#define DEFAULT_REPS    3

/* ------------------------------------------------------------------ traces */
typedef enum { OP_ALLOC, OP_CALLOC, OP_REALLOC, OP_FREE } optype_t;
typedef struct { optype_t type; int id; size_t size; size_t n; int line; } op_t;
typedef struct {
    char name[256];
    op_t *ops;
    int nops;
    int nids;
} trace_t;

static int verbose;

static void fatal(const char *fmt, const char *arg)
{
    fprintf(stderr, "mdriver: ");
    fprintf(stderr, fmt, arg);
    fprintf(stderr, "\n");
    exit(2);
}

static bool read_trace(const char *path, trace_t *t)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "mdriver: cannot open %s: %s\n", path, strerror(errno));
        return false;
    }
    const char *base = strrchr(path, '/');
    snprintf(t->name, sizeof t->name, "%s", base ? base + 1 : path);
    size_t cap = 1024;
    t->ops = malloc(cap * sizeof(op_t));
    t->nops = 0;
    t->nids = 0;
    char line[512];
    int lineno = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (*s == '#' || *s == '\n' || *s == '\0') continue;
        op_t op = { .line = lineno };
        unsigned long id = 0, a = 0, b = 0;
        int k;
        switch (*s) {
        case 'a': k = sscanf(s + 1, "%lu %lu", &id, &a); op.type = OP_ALLOC; op.size = a; if (k != 2) goto bad; break;
        case 'c': k = sscanf(s + 1, "%lu %lu %lu", &id, &a, &b); op.type = OP_CALLOC; op.n = a; op.size = b; if (k != 3) goto bad; break;
        case 'r': k = sscanf(s + 1, "%lu %lu", &id, &a); op.type = OP_REALLOC; op.size = a; if (k != 2) goto bad; break;
        case 'f': k = sscanf(s + 1, "%lu", &id); op.type = OP_FREE; if (k != 1) goto bad; break;
        default: goto bad;
        }
        if (id > 10000000) goto bad;
        op.id = (int)id;
        if (op.id + 1 > t->nids) t->nids = op.id + 1;
        if ((size_t)t->nops == cap) {
            cap *= 2;
            t->ops = realloc(t->ops, cap * sizeof(op_t));
        }
        t->ops[t->nops++] = op;
        continue;
    bad:
        fprintf(stderr, "mdriver: %s:%d: cannot parse \"%s\"\n", path, lineno, strtok(line, "\n"));
        fclose(f);
        return false;
    }
    fclose(f);
    return true;
}

/* ------------------------------------------------------------------ live-block bookkeeping */
typedef struct { char *lo, *hi; int id; } ival_t;   /* [lo, hi) */

static int ival_cmp(const void *a, const void *b)
{
    const ival_t *x = a, *y = b;
    if (x->hi <= y->lo) return -1;
    if (y->hi <= x->lo) return 1;
    return 0;                     /* overlapping intervals compare equal */
}

typedef struct { char *p; size_t size; bool live; ival_t *iv; } blk_t;

static unsigned char pattern(int id)
{
    return (unsigned char)((id * 131u + 7u) % 251u + 1u);
}

static bool check_pattern(const char *p, size_t n, int id, size_t *bad_at)
{
    unsigned char c = pattern(id);
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)p[i] != c) { *bad_at = i; return false; }
    return true;
}

/* ------------------------------------------------------------------ correctness + utilization */
typedef struct { bool valid; double util; double secs; double libc_secs; } result_t;

static char errbuf[512];
static int err_line;   /* trace line of the failing operation */

#define FAIL(...) do { snprintf(errbuf, sizeof errbuf, __VA_ARGS__); goto fail; } while (0)

static bool in_heap(const char *p, size_t n)
{
    const char *lo = mem_heap_lo(), *hi = (const char *)mem_heap_hi() + 1;
    return p >= lo && p + n <= hi;
}

static bool check_trace(const trace_t *t, bool checkheap, double *util)
{
    blk_t *b = calloc((size_t)t->nids, sizeof(blk_t));
    void *tree = NULL;
    size_t live_payload = 0, peak_payload = 0, peak_footprint = 0;
    const op_t *op = NULL;
    size_t base_mapped;

    mem_reset_brk();
    base_mapped = mem_mapped_bytes();
    if (mm_init() < 0) {
        snprintf(errbuf, sizeof errbuf, "mm_init failed");
        free(b);
        return false;
    }
    for (int i = 0; i < t->nops; i++) {
        op = &t->ops[i];
        blk_t *k = &b[op->id];
        size_t old_size = k->size;
        char *p = NULL;
        size_t size = op->size;

        if (op->type == OP_FREE) {
            if (!k->live) FAIL("trace frees id %d, which is not allocated", op->id);
            if (k->p == NULL) {            /* a malloc(0) that returned NULL */
                mm_free(NULL);
                k->live = false;
                goto next;
            }
            size_t at;
            if (!check_pattern(k->p, k->size, op->id, &at))
                FAIL("payload of block %d (%zu bytes at %p) was changed at offset %zu before it was freed", op->id, k->size, (void *)k->p, at);
            tdelete(k->iv, &tree, ival_cmp);
            free(k->iv);
            k->iv = NULL;
            mm_free(k->p);
            k->live = false;
            live_payload -= k->size;
            k->p = NULL;
            k->size = 0;
        } else {
            if (op->type == OP_CALLOC) {
                if (op->n != 0 && op->size > SIZE_MAX / op->n) FAIL("calloc overflow in trace");
                size = op->n * op->size;
            }
            if (op->type != OP_REALLOC && k->live) FAIL("trace allocates id %d twice", op->id);
            if (op->type == OP_REALLOC && k->live && k->p != NULL) {
                size_t at;
                if (!check_pattern(k->p, k->size, op->id, &at))
                    FAIL("payload of block %d was changed at offset %zu before realloc", op->id, at);
                tdelete(k->iv, &tree, ival_cmp);
                free(k->iv);
                k->iv = NULL;
                live_payload -= k->size;
            }
            if (op->type == OP_ALLOC) p = mm_malloc(size);
            else if (op->type == OP_CALLOC) p = mm_calloc(op->n, op->size);
            else p = mm_realloc(k->live ? k->p : NULL, size);

            if (p == NULL) {
                if (size == 0) {           /* malloc(0) may return NULL; realloc(p, 0) frees p */
                    k->live = op->type != OP_REALLOC;
                    k->p = NULL;
                    k->size = 0;
                    goto next;
                }
                FAIL("%s(%zu) returned NULL", op->type == OP_ALLOC ? "mm_malloc" : op->type == OP_CALLOC ? "mm_calloc" : "mm_realloc", size);
            }
            if ((uintptr_t)p % MM_ALIGNMENT != 0)
                FAIL("block %d at %p is not %d-byte aligned", op->id, (void *)p, MM_ALIGNMENT);
            size_t span = size ? size : 1;
            if (!in_heap(p, span) && mem_mapped_bytes() == base_mapped)
                FAIL("block %d [%p, %p) is outside the heap [%p, %p]", op->id, (void *)p, (void *)(p + span),
                     mem_heap_lo(), mem_heap_hi());
            ival_t *iv = malloc(sizeof *iv);
            iv->lo = p; iv->hi = p + span; iv->id = op->id;
            ival_t **hit = tfind(iv, &tree, ival_cmp);
            if (hit) {
                ival_t *o = *hit;
                FAIL("block %d [%p, %p) overlaps live block %d [%p, %p)", op->id, (void *)iv->lo, (void *)iv->hi,
                     o->id, (void *)o->lo, (void *)o->hi);
            }
            tsearch(iv, &tree, ival_cmp);
            k->iv = iv;
            if (op->type == OP_CALLOC) {
                for (size_t j = 0; j < size; j++)
                    if (p[j] != 0) FAIL("mm_calloc block %d is not zeroed at offset %zu", op->id, j);
            }
            if (op->type == OP_REALLOC && k->live && k->size > 0) {
                size_t keep = old_size < size ? old_size : size, at;
                if (!check_pattern(p, keep, op->id, &at))
                    FAIL("mm_realloc of block %d did not preserve its data (offset %zu)", op->id, at);
            }
            memset(p, pattern(op->id), size);
            k->p = p;
            k->size = size;
            k->live = true;
            live_payload += size;
        }
    next:
        if (live_payload > peak_payload) peak_payload = live_payload;
        size_t fp = mem_heapsize() + (mem_mapped_bytes() - base_mapped);
        if (fp > peak_footprint) peak_footprint = fp;
        if (checkheap && !mm_checkheap(__LINE__))
            FAIL("mm_checkheap reported an inconsistent heap");
    }
    for (int id = 0; id < t->nids; id++) {
        size_t at;
        if (b[id].live && !check_pattern(b[id].p, b[id].size, id, &at))
            FAIL("payload of block %d was changed at offset %zu (checked at the end of the trace)", id, at);
    }
    *util = peak_footprint ? (double)peak_payload / (double)peak_footprint : 0.0;
    for (int id = 0; id < t->nids; id++)           /* clean up: also releases mem_map blocks */
        if (b[id].live) mm_free(b[id].p);
    tdestroy(tree, free);
    free(b);
    return true;

fail:
    err_line = op ? op->line : 0;
    free(b);   /* the heap may be corrupt: do not free blocks; mem_reset_brk() starts over */
    return false;
}

/* ------------------------------------------------------------------ timing */
typedef struct {
    int   (*init)(void);
    void *(*malloc)(size_t);
    void *(*calloc)(size_t, size_t);
    void *(*realloc)(void *, size_t);
    void  (*free)(void *);
} alloc_t;

static int mm_start(void) { mem_reset_brk(); return mm_init(); }
static int libc_start(void) { return 0; }
static const alloc_t MM = { mm_start, mm_malloc, mm_calloc, mm_realloc, mm_free };
static const alloc_t LIBC = { libc_start, malloc, calloc, realloc, free };

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static double time_trace(const trace_t *t, const alloc_t *A, int reps)
{
    void **p = calloc((size_t)t->nids, sizeof(void *));
    double best = 1e30;
    for (int r = 0; r < reps; r++) {
        memset(p, 0, (size_t)t->nids * sizeof(void *));
        if (A->init() < 0) break;
        double t0 = now();
        for (int i = 0; i < t->nops; i++) {
            const op_t *op = &t->ops[i];
            switch (op->type) {
            case OP_ALLOC:   p[op->id] = A->malloc(op->size); break;
            case OP_CALLOC:  p[op->id] = A->calloc(op->n, op->size); break;
            case OP_REALLOC: p[op->id] = A->realloc(p[op->id], op->size); break;
            case OP_FREE:    A->free(p[op->id]); p[op->id] = NULL; break;
            }
        }
        double dt = now() - t0;
        if (dt < best) best = dt;
        for (int id = 0; id < t->nids; id++)
            if (p[id]) A->free(p[id]);
    }
    free(p);
    return best;
}

/* ------------------------------------------------------------------ main */
static void usage(void)
{
    fprintf(stderr,
            "usage: mdriver [-f trace | -t dir] [-l] [-c] [-n] [-r reps] [-v] [-j]\n"
            "  -f trace  run one trace file\n"
            "  -t dir    run every *.rep file in dir (default: traces)\n"
            "  -l        also time glibc malloc and compute the performance index\n"
            "  -c        call mm_checkheap after every operation\n"
            "  -n        skip the timing runs (correctness and utilization only)\n"
            "  -r reps   timing repetitions per trace (default %d, best is kept)\n"
            "  -v        print a line per trace and explain failures (the default)\n"
            "  -j        print a JSON summary (used by grade.py)\n", DEFAULT_REPS);
    exit(2);
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

int main(int argc, char **argv)
{
    const char *file = NULL, *dir = "traces";
    bool libc = false, checkheap = false, notime = false, json = false;
    int reps = DEFAULT_REPS, c;
    while ((c = getopt(argc, argv, "f:t:lcnr:vjh")) != -1) {
        switch (c) {
        case 'f': file = optarg; break;
        case 't': dir = optarg; break;
        case 'l': libc = true; break;
        case 'c': checkheap = true; break;
        case 'n': notime = true; break;
        case 'r': reps = atoi(optarg); if (reps < 1) reps = 1; break;
        case 'v': verbose = 1; break;
        case 'j': json = true; break;
        default: usage();
        }
    }
    if (!verbose && !json) verbose = 1;

    /* collect trace paths */
    char **paths = NULL;
    int npaths = 0;
    if (file) {
        paths = malloc(sizeof(char *));
        paths[npaths++] = strdup(file);
    } else {
        DIR *d = opendir(dir);
        if (!d) fatal("cannot open trace directory %s", dir);
        struct dirent *e;
        int cap = 0;
        while ((e = readdir(d))) {
            size_t len = strlen(e->d_name);
            if (len < 5 || strcmp(e->d_name + len - 4, ".rep") != 0) continue;
            if (npaths == cap) { cap = cap ? 2 * cap : 32; paths = realloc(paths, cap * sizeof(char *)); }
            char *full = malloc(strlen(dir) + len + 2);
            sprintf(full, "%s/%s", dir, e->d_name);
            paths[npaths++] = full;
        }
        closedir(d);
        qsort(paths, npaths, sizeof(char *), cmp_str);
        if (npaths == 0) fatal("no .rep files in %s", dir);
    }

    mem_init();
    trace_t *traces = calloc(npaths, sizeof(trace_t));
    result_t *res = calloc(npaths, sizeof(result_t));
    long total_ops = 0;
    double total_secs = 0, total_libc = 0, util_sum = 0;
    int nvalid = 0;

    if (verbose)
        printf("%-22s %5s %7s %9s %10s %10s%s\n", "trace", "valid", "util", "ops", "secs", "Kops/s", libc ? "  glibc Kops/s" : "");
    for (int i = 0; i < npaths; i++) {
        if (!read_trace(paths[i], &traces[i])) return 2;
        const trace_t *t = &traces[i];
        errbuf[0] = '\0';
        err_line = 0;
        res[i].valid = check_trace(t, checkheap, &res[i].util);
        if (res[i].valid) {
            nvalid++;
            util_sum += res[i].util;
            if (!notime) res[i].secs = time_trace(t, &MM, reps);
            if (libc && !notime) res[i].libc_secs = time_trace(t, &LIBC, reps);
            total_ops += t->nops;
            total_secs += res[i].secs;
            total_libc += res[i].libc_secs;
        }
        if (verbose) {
            if (res[i].valid && !notime)
                printf("%-22s %5s %6.1f%% %9d %10.6f %10.0f", t->name, "yes", 100 * res[i].util, t->nops, res[i].secs,
                       res[i].secs > 0 ? t->nops / res[i].secs / 1e3 : 0.0);
            else if (res[i].valid)
                printf("%-22s %5s %6.1f%% %9d %10s %10s", t->name, "yes", 100 * res[i].util, t->nops, "-", "-");
            else
                printf("%-22s %5s %7s %9d %10s %10s", t->name, "NO", "-", t->nops, "-", "-");
            if (libc && res[i].valid && !notime)
                printf("  %13.0f", res[i].libc_secs > 0 ? t->nops / res[i].libc_secs / 1e3 : 0.0);
            printf("\n");
            if (!res[i].valid)
                printf("    ERROR (trace line %d): %s\n", err_line, errbuf);
        }
        if (json && !verbose && !res[i].valid)
            fprintf(stderr, "%s, line %d: %s\n", t->name, err_line, errbuf);
    }

    double avg_util = npaths ? util_sum / npaths : 0;        /* invalid traces count as 0 */
    double tput = total_secs > 0 ? total_ops / total_secs : 0;
    double libc_tput = total_libc > 0 ? total_ops / total_libc : 0;
    double tput_score = 0, perf = -1;
    if (libc && libc_tput > 0) {
        tput_score = tput / (TPUT_FRACTION * libc_tput);
        if (tput_score > 1) tput_score = 1;
        perf = UTIL_WEIGHT * avg_util + (1 - UTIL_WEIGHT) * tput_score;
    }

    if (verbose) {
        printf("\n%d of %d traces correct\n", nvalid, npaths);
        printf("average utilization: %.1f%%   (invalid traces count as 0)\n", 100 * avg_util);
        if (!notime) printf("throughput: %.0f Kops/s over the correct traces\n", tput / 1e3);
        if (perf >= 0) {
            printf("glibc throughput on the same traces: %.0f Kops/s\n", libc_tput / 1e3);
            printf("performance index = %.0f%% x utilization + %.0f%% x min(1, throughput / (%.0f%% of glibc))\n",
                   100 * UTIL_WEIGHT, 100 * (1 - UTIL_WEIGHT), 100 * TPUT_FRACTION);
            printf("                  = %.0f%% x %.3f + %.0f%% x %.3f = %.1f / 100\n", 100 * UTIL_WEIGHT, avg_util,
                   100 * (1 - UTIL_WEIGHT), tput_score, 100 * perf);
        }
    }
    if (json) {
        printf("{\"traces\": [");
        for (int i = 0; i < npaths; i++)
            printf("%s{\"name\": \"%s\", \"valid\": %s, \"util\": %.4f, \"ops\": %d, \"secs\": %.6f, \"libc_secs\": %.6f}",
                   i ? ", " : "", traces[i].name, res[i].valid ? "true" : "false", res[i].valid ? res[i].util : 0.0,
                   traces[i].nops, res[i].valid ? res[i].secs : 0.0, res[i].libc_secs);
        printf("], \"avg_util\": %.4f, \"throughput\": %.1f, \"libc_throughput\": %.1f, \"tput_score\": %.4f, "
               "\"perf_index\": %.4f, \"n_valid\": %d, \"n_traces\": %d}\n",
               avg_util, tput, libc_tput, tput_score, perf, nvalid, npaths);
    }
    return nvalid == npaths ? 0 : 1;
}
