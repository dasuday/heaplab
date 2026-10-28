/*
 * gc_test.c - week 3C checks of gc.c, linked with mm.c and memlib.c.   (GIVEN)
 * Prints one PASS or FAIL line per check; grade.py counts them.
 *
 * A conservative collector may keep a little garbage alive (an old pointer left in a stack slot
 * or a register), so tests that expect garbage to be freed allow up to 5% to survive. Tests that
 * expect blocks to survive allow none to be lost. Garbage is always built inside deep(), a few
 * kilobytes further down the stack than gc_collect ever runs, so stale pointers left in dead
 * stack frames cannot keep it alive; the stack is also scrubbed between tests.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gc.h"
#include "memlib.h"
#include "mm.h"

typedef struct node {
    struct node *next;
    struct node *other;
    long value;
    long pad;
} node_t;

node_t *root;                /* a global root (in .bss) */
char *inner_root;            /* points into the middle of a block */
static int failures;

#define NOINLINE __attribute__((noinline))

static NOINLINE void scrub(void)
{
    volatile char junk[32768];
    memset((char *)junk, 0, sizeof junk);
}

/* run fn in a stack frame 8 KiB below the caller's */
static NOINLINE void deep(void (*fn)(void))
{
    volatile char pad[8192];
    pad[0] = 1;
    fn();
    pad[sizeof pad - 1] = pad[0];
}

static size_t managed_blocks(void)
{
    size_t n = 0;
    for (void *b = mm_first_block(); b; b = mm_next_block(b))
        if (mm_is_allocated(b) && (mm_get_gcbits(b) & GC_MANAGED))
            n++;
    return n;
}

static char msg[200];

static void report(const char *name, int ok)
{
    printf("%s: %-46s %s\n", ok ? "PASS" : "FAIL", name, msg);
    failures += !ok;
}

static NOINLINE node_t *make_list(int n)
{
    node_t *h = NULL;
    for (int i = 0; i < n; i++) {
        node_t *x = gc_malloc(sizeof *x);
        if (!x) return NULL;
        x->next = h;
        x->value = i;
        h = x;
    }
    return h;
}

static NOINLINE long sum_list(node_t *h)
{
    long s = 0;
    for (; h; h = h->next) s += h->value;
    return s;
}

static NOINLINE void build_list_10k(void)  { root = make_list(10000); }
static NOINLINE void build_list_200k(void) { root = make_list(200000); }

/* 1. everything reachable from a global survives, with its contents */
static NOINLINE void t_reachable(void)
{
    deep(build_list_10k);
    scrub();
    size_t freed = gc_collect();
    int ok = root && managed_blocks() == 10000 && sum_list(root) == 10000L * 9999 / 2;
    snprintf(msg, sizeof msg, "(%zu managed blocks left, %zu freed)", managed_blocks(), freed);
    report("reachable list survives", ok);
}

/* 2. once the global is cleared the list is garbage */
static NOINLINE void t_unreachable(void)
{
    root = NULL;
    size_t freed = gc_collect();
    size_t left = managed_blocks();
    snprintf(msg, sizeof msg, "(%zu of 10000 still allocated, %zu freed)", left, freed);
    report("unreachable list is freed", left <= 500);
}

static NOINLINE void make_cycles(void)
{
    for (int i = 0; i < 2000; i++) {                 /* 2-node cycles */
        node_t *a = gc_malloc(sizeof *a), *b = gc_malloc(sizeof *b);
        a->next = b;
        b->next = a;
    }
    node_t *first = gc_malloc(sizeof *first), *p = first;   /* one big ring */
    for (int i = 0; i < 5000; i++) {
        p->next = gc_malloc(sizeof *p);
        p->next->other = first;
        p = p->next;
    }
    p->next = first;
}

/* 3. reference counting cannot free cycles; mark-and-sweep can */
static NOINLINE void t_cycles(void)
{
    deep(make_cycles);
    scrub();
    gc_collect();
    size_t left = managed_blocks();
    snprintf(msg, sizeof msg, "(%zu of 9001 still allocated)", left);
    report("unreachable cycles are freed", left <= 450);
}

static NOINLINE void make_inner(void)
{
    node_t *x = gc_malloc(sizeof *x);
    x->value = 4242;
    x->next = gc_malloc(sizeof *x);                  /* reachable only through x */
    x->next->value = 4343;
    inner_root = (char *)x + 16;                     /* points at x->value, not at x */
}

/* 4. a pointer into the middle of a block keeps the whole block (and what it points to) alive */
static NOINLINE void t_interior(void)
{
    deep(make_inner);
    scrub();
    gc_collect();
    node_t *x = (node_t *)(inner_root - 16);
    int ok = managed_blocks() >= 2 && x->value == 4242 && x->next && x->next->value == 4343 &&
             mm_is_allocated(x) && mm_is_allocated(x->next);
    snprintf(msg, sizeof msg, "(%zu managed blocks left)", managed_blocks());
    report("interior pointer keeps its block alive", ok);
    inner_root = NULL;
}

static void **holder;

static NOINLINE void fill_holder(void)
{
    holder = mm_malloc(64 * sizeof(void *));         /* not managed: the program frees it */
    for (int i = 0; i < 64; i++) {
        node_t *x = gc_malloc(sizeof *x);
        x->value = i;
        holder[i] = x;
    }
}

/* 5. blocks from mm_malloc are roots: what they point to survives until they are freed */
static NOINLINE void t_mixed(void)
{
    gc_collect();
    deep(fill_holder);
    scrub();
    gc_collect();
    int ok = managed_blocks() >= 64;
    for (int i = 0; i < 64 && ok; i++)
        ok = mm_is_allocated(holder[i]) && ((node_t *)holder[i])->value == i;
    int unmanaged_kept = mm_is_allocated(holder) && !(mm_get_gcbits(holder) & GC_MANAGED);
    snprintf(msg, sizeof msg, "(%zu managed blocks)", managed_blocks());
    report("mm_malloc blocks are roots, never freed", ok && unmanaged_kept);
    mm_free(holder);
    holder = NULL;
    scrub();
    gc_collect();
    size_t left = managed_blocks();
    snprintf(msg, sizeof msg, "(%zu of 64 still allocated)", left);
    report("their targets die once they are freed", left <= 3);
}

/* 6. a 200,000-node list: a recursive marker would overflow the C stack */
static NOINLINE void t_deep(void)
{
    deep(build_list_200k);
    scrub();
    gc_collect();
    int ok = managed_blocks() >= 200000 && sum_list(root) == 200000L * 199999 / 2;
    snprintf(msg, sizeof msg, "(%zu managed blocks)", managed_blocks());
    report("200,000-node list survives (no recursion)", ok);
    root = NULL;
    scrub();
    gc_collect();
}

static NOINLINE node_t *make_tree(int depth, long *count)
{
    if (depth == 0) return NULL;
    node_t *x = gc_malloc(sizeof *x);
    (*count)++;
    x->value = depth;
    x->next = make_tree(depth - 1, count);
    x->other = make_tree(depth - 1, count);
    return x;
}

static long tree_count;
static NOINLINE void build_tree(void) { tree_count = 0; root = make_tree(11, &tree_count); }

static NOINLINE long tree_sum(node_t *x)
{
    return x ? x->value + tree_sum(x->next) + tree_sum(x->other) : 0;
}

/* 7. repeated build-and-drop rounds: each collection frees the previous round's tree, and the
 *    heap stops growing because the freed space is reused */
static NOINLINE void t_stress(void)
{
    long count = 0, expect = 0;
    size_t freed = 0, heap_mid = 0;
    for (int round = 0; round < 60; round++) {
        deep(build_tree);                            /* 2047 nodes */
        count = tree_count;
        scrub();
        freed += gc_collect();
        if (round == 9)
            heap_mid = mem_heapsize();
    }
    for (int d = 1; d <= 11; d++) expect += (long)d << (11 - d);
    size_t growth = mem_heapsize() - heap_mid;
    int ok = freed >= 50 * 2047 && growth < (256u << 10) && tree_sum(root) == expect;
    snprintf(msg, sizeof msg, "(%zu blocks freed in 60 rounds of %ld; heap grew %zu bytes in the last 50)", freed,
             count, growth);
    report("60 rounds of garbage are reclaimed", ok);
    root = NULL;
}

int main(void)
{
    mem_init();
    mm_init();
    gc_init();
    t_reachable();
    scrub();
    t_unreachable();
    scrub();
    t_cycles();
    scrub();
    t_interior();
    scrub();
    t_mixed();
    scrub();
    t_deep();
    scrub();
    t_stress();
    return failures ? 1 : 0;
}
