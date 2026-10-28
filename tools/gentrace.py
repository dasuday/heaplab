#!/usr/bin/env python3
"""Generate the synthetic allocation traces in traces/.

usage: python3 tools/gentrace.py [--seed N] [--scale X] OUTDIR

Every trace is deterministic for a given seed, so the instructor can regenerate the public set
(seed 1) and build hidden sets with other seeds. Trace format: see driver/mdriver.c.
"""
import argparse
import math
import os
import random


class Trace:
    def __init__(self, name, comment):
        self.name, self.lines, self.live = name, [f"# {comment}"], {}
        self.next_id = 0

    def alloc(self, size):
        i = self.next_id
        self.next_id += 1
        self.lines.append(f"a {i} {size}")
        self.live[i] = size
        return i

    def calloc(self, n, size):
        i = self.next_id
        self.next_id += 1
        self.lines.append(f"c {i} {n} {size}")
        self.live[i] = n * size
        return i

    def realloc(self, i, size):
        self.lines.append(f"r {i} {size}")
        self.live[i] = size

    def free(self, i):
        self.lines.append(f"f {i}")
        del self.live[i]

    def free_all(self, rng=None):
        ids = list(self.live)
        if rng:
            rng.shuffle(ids)
        for i in ids:
            self.free(i)

    def write(self, outdir):
        with open(os.path.join(outdir, self.name), "w") as f:
            f.write("\n".join(self.lines) + "\n")
        return len(self.lines) - 1


def log_uniform(rng, lo, hi):
    return int(math.exp(rng.uniform(math.log(lo), math.log(hi))))


def tiny(rng, s):
    t = Trace("01-tiny.rep", "hand-written edge cases: odd sizes, malloc(0), realloc both ways, calloc")
    a = [t.alloc(n) for n in (1, 2, 7, 8, 15, 16, 17, 31, 32, 33, 100, 4095, 4096, 4097)]
    z = t.alloc(0)
    t.free(a[1]); t.free(a[3]); t.free(a[2])          # free neighbours: coalescing
    t.realloc(a[0], 200); t.realloc(a[0], 10); t.realloc(a[0], 5000)
    c = t.calloc(10, 7)
    t.free(z)
    for i in a[4:]:
        t.free(i)
    t.free(a[0]); t.free(c)
    b = [t.alloc(48) for _ in range(20)]
    for i in b[::2]:
        t.free(i)
    for i in b[1::2]:
        t.free(i)
    return t


def lifo(rng, s):
    t = Trace("02-lifo.rep", "stack order: the last block allocated is the first freed")
    stack = []
    for _ in range(int(12000 * s)):
        if stack and (rng.random() < 0.48 or len(stack) > 3000):
            t.free(stack.pop())
        else:
            stack.append(t.alloc(rng.randint(16, 512)))
    while stack:
        t.free(stack.pop())
    return t


def fifo(rng, s):
    t = Trace("03-fifo.rep", "queue order: blocks are freed in the order they were allocated")
    q = []
    for _ in range(int(12000 * s)):
        if q and (rng.random() < 0.48 or len(q) > 3000):
            t.free(q.pop(0))
        else:
            q.append(t.alloc(rng.randint(16, 512)))
    for i in q:
        t.free(i)
    return t


def random_small(rng, s):
    t = Trace("04-random-small.rep", "random malloc/free of 1-256 bytes")
    for _ in range(int(20000 * s)):
        if t.live and (rng.random() < 0.45 or len(t.live) > 2500):
            t.free(rng.choice(list(t.live)))
        else:
            t.alloc(rng.randint(1, 256))
    t.free_all(rng)
    return t


def random_mixed(rng, s):
    t = Trace("05-random-mixed.rep", "random malloc/free, sizes spread evenly over 1 byte to 32 KiB on a log scale")
    for _ in range(int(16000 * s)):
        if t.live and (rng.random() < 0.47 or len(t.live) > 1500):
            t.free(rng.choice(list(t.live)))
        else:
            t.alloc(log_uniform(rng, 1, 32768))
    t.free_all(rng)
    return t


def binary(rng, s):
    t = Trace("06-binary.rep", "fragmentation: small and large blocks alternate, the large ones are freed, "
              "then slightly larger blocks are requested")
    n = int(2000 * s)
    small, large = [], []
    for _ in range(n):
        small.append(t.alloc(64))
        large.append(t.alloc(448))
    for i in large:
        t.free(i)
    big = [t.alloc(512) for _ in range(n)]
    for i in small + big:
        t.free(i)
    return t


def coalesce(rng, s):
    t = Trace("07-coalesce.rep", "coalescing: freed neighbours must merge so that bigger requests fit")
    for _ in range(int(6 * s)):
        blocks = [t.alloc(rng.choice((32, 64, 96))) for _ in range(1500)]
        order = list(range(len(blocks)))
        # free in an order that needs merging with the previous block, the next block, and both
        order = order[1::3] + order[0::3] + order[2::3]
        for k in order:
            t.free(blocks[k])
        bigs = [t.alloc(rng.randint(1000, 4000)) for _ in range(60)]
        for i in bigs:
            t.free(i)
    return t


def realloc_grow(rng, s):
    t = Trace("08-realloc-grow.rep", "growing buffers: realloc by small steps, with other allocations in between")
    bufs = {}
    for _ in range(int(40 * s)):
        b = t.alloc(16)
        bufs[b] = 16
        for _ in range(150):
            bufs[b] += rng.randint(8, 64)
            t.realloc(b, bufs[b])
            if rng.random() < 0.3:
                t.free(t.alloc(rng.randint(8, 64)))   # short-lived allocations in between
            if rng.random() < 0.15:
                t.alloc(rng.randint(8, 64))           # some stay alive and sit next to the buffer
        if len(bufs) > 8:
            victim = rng.choice(list(bufs))
            t.free(victim)
            del bufs[victim]
    t.free_all(rng)
    return t


def realloc_mixed(rng, s):
    t = Trace("09-realloc-mixed.rep", "realloc up and down by large factors")
    ids = [t.alloc(rng.randint(16, 2048)) for _ in range(300)]
    for _ in range(int(6000 * s)):
        i = rng.choice(ids)
        new = max(1, int(t.live[i] * rng.choice((0.25, 0.5, 1.5, 2, 3))))
        new = min(new, 64 * 1024)
        t.realloc(i, new)
    t.free_all(rng)
    return t


def calloc_trace(rng, s):
    t = Trace("10-calloc.rep", "calloc of arrays: element counts times element sizes")
    for _ in range(int(8000 * s)):
        if t.live and (rng.random() < 0.45 or len(t.live) > 1000):
            t.free(rng.choice(list(t.live)))
        else:
            t.calloc(rng.randint(1, 64), rng.choice((1, 2, 4, 8, 12, 16, 24, 40)))
    t.free_all(rng)
    return t


def large(rng, s):
    t = Trace("11-large.rep", "large blocks (64 KiB to 2 MiB) mixed with small ones")
    for _ in range(int(1500 * s)):
        if t.live and (rng.random() < 0.48 or len(t.live) > 60):
            t.free(rng.choice(list(t.live)))
        elif rng.random() < 0.4:
            t.alloc(log_uniform(rng, 64 * 1024, 2 * 1024 * 1024))
        else:
            t.alloc(rng.randint(16, 1024))
    t.free_all(rng)
    return t


def phases(rng, s):
    t = Trace("12-phases.rep", "a program in phases: many small objects, most freed, then bigger objects")
    for _ in range(int(5 * s)):
        nodes = [t.alloc(rng.choice((24, 32, 48))) for _ in range(3000)]
        rng.shuffle(nodes)
        keep = nodes[:300]
        for i in nodes[300:]:
            t.free(i)
        arrays = [t.alloc(rng.randint(200, 3000)) for _ in range(400)]
        for i in arrays[::2]:
            t.free(i)
        for i in keep:
            t.free(i)
        for i in arrays[1::2]:
            t.free(i)
    return t


GENERATORS = [tiny, lifo, fifo, random_small, random_mixed, binary, coalesce, realloc_grow, realloc_mixed,
              calloc_trace, large, phases]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("outdir")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--scale", type=float, default=1.0, help="multiply the length of every trace")
    ap.add_argument("--prefix", default="", help="prefix for file names (e.g. 'h-' for a hidden set)")
    args = ap.parse_args()
    os.makedirs(args.outdir, exist_ok=True)
    for k, gen in enumerate(GENERATORS):
        rng = random.Random(args.seed * 1000 + k)
        t = gen(rng, args.scale)
        t.name = args.prefix + t.name
        n = t.write(args.outdir)
        print(f"{t.name:28s} {n:7d} operations")


if __name__ == "__main__":
    main()
