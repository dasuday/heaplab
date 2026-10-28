# HeapLab: build a memory allocator for Linux x86-64

A three-week project on dynamic memory allocation and memory management, to accompany
*Computer Systems: A Programmer's Perspective* (Bryant and O'Hallaron, 3rd edition), chapter 9.

You will write your own `malloc`, `free` and `realloc`. You will make them fast and
space-efficient, then turn them into a drop-in replacement for the C library's allocator that
runs real programs such as `ls` and `python3`. Finally you'll add a debugging mode that catches
memory bugs and a garbage collector that frees memory nobody can reach any more.

| Week | You build | Reading (CS:APP) | Tag | Points |
|---|---|---|---|---|
| 1 | An address-space inspector, then an **implicit free list** allocator with boundary-tag coalescing | 9.1–9.5, 9.7.2, 9.8.4, 9.9.1–9.9.12 | `week1` | 25 |
| 2 | **Segregated explicit free lists**, faster placement, in-place `realloc` | 9.9.13–9.9.14 | `week2` | 35 |
| 3 | **A real malloc** (3A), **a debugging allocator** (3B), **a garbage collector** (3C) | 7.13, 8.4, 9.10, 9.11, 12.7.1 | `week3` | 40 |

---

## Contents

1. [Getting started](#1-getting-started)
2. [What is in the repository](#2-what-is-in-the-repository)
3. [The rules](#3-the-rules)
4. [How your allocator is tested](#4-how-your-allocator-is-tested)
5. [Week 1: the address space and an implicit free list](#5-week-1-the-address-space-and-an-implicit-free-list)
6. [Week 2: segregated free lists and performance](#6-week-2-segregated-free-lists-and-performance)
7. [Week 3: a real malloc, a debugging allocator, a garbage collector](#7-week-3-a-real-malloc-a-debugging-allocator-a-garbage-collector)
8. [Grading](#8-grading)
9. [Submitting](#9-submitting)
10. [Debugging tips](#10-debugging-tips)
11. [References](#11-references)

---

## 1. Getting started

You need an **x86-64 Linux** machine with `gcc`, `make` and `python3`:

* a lab machine or your own Linux PC (Ubuntu 22.04 or 24.04 are known to work);
* Windows: **WSL2** with Ubuntu (`wsl --install`), which runs a real Linux kernel;
* macOS or anything else: use the provided **Dockerfile** (see the comments at its top). On
  Apple Silicon you must pass `--platform linux/amd64`, because the project is about x86-64.

Useful extra tools: `gdb`, `strace`, `valgrind` (`sudo apt install gdb strace valgrind`).

```bash
git clone <your repository URL> heaplab
cd heaplab
make                # builds everything into build/
build/mdriver       # runs the starter allocator on every trace in traces/
python3 grade.py    # the autograder: builds, tests, prints your score
```

The starter allocator is correct but never reuses memory, so it scores 0. That is your starting point.

Every push also runs the autograder on GitHub (the **Actions** tab). The score table is on the
run's **Summary** page.

## 2. What is in the repository

```
src/mm.c          YOU   your allocator (all three weeks)
src/vmmap.c       YOU   week 1 warm-up: where does an address live?
src/preload.c     YOU   week 3A: the malloc/free/... wrappers for LD_PRELOAD (add locking)
src/gc.c          YOU   week 3C: the garbage collector
src/mm.h                the interface you implement (read it carefully)
src/memlib.[ch]         the memory system under your allocator: a resettable sbrk() and mmap()
src/vmmap.h, gc.h       interfaces for the warm-up and the collector
driver/mdriver.c        replays traces against your allocator, checks and measures it
traces/*.rep            the allocation traces the grader uses (and its hidden ones are similar)
tests/                  test programs for weeks 1 and 3
tools/gentrace.py       how the synthetic traces were generated
tools/mtrace.c          record a trace from any real program (LD_PRELOAD=build/libmtrace.so)
tools/vmexplore.c       week 1: watch glibc's malloc use brk and mmap
grade.py                the autograder (also run by GitHub Actions)
```

Only the four files marked YOU are graded. Do not change the others: the grader uses its own
copies.

## 3. The rules

* All of your allocator's memory comes from `memlib`: the heap from `mem_sbrk`, and in week 3
  large blocks from `mem_map`/`mem_unmap`. **Do not call `malloc`, `calloc`, `realloc`, `free`,
  `sbrk`, `brk` or `mmap` in `mm.c`.**
* No global or static arrays bigger than 256 bytes in `mm.c`. Your data structures live in
  the heap you manage.
* Every pointer you return must be **16-byte aligned**, as glibc guarantees on x86-64.
* `mm_init` is called before every trace, after the heap has been emptied, so it must reset all
  of your global state.
* Write your own code. You may read the allocator in CS:APP 9.9.12 to understand the ideas, and
  discuss designs with classmates, but the code you submit must be yours. Cite anything you
  consulted in the comment at the top of `mm.c`.

## 4. How your allocator is tested

### Traces and the driver

A trace is a list of allocation requests, one per line:

```
a 7 120        # p[7] = malloc(120)
c 8 10 24      # p[8] = calloc(10, 24)
r 7 300        # p[7] = realloc(p[7], 300)
f 8            # free(p[8])
```

`build/mdriver` replays traces against your allocator:

```bash
build/mdriver                  # all traces in traces/
build/mdriver -f traces/06-binary.rep
build/mdriver -l               # also time glibc's malloc and print the performance index
build/mdriver -c               # call mm_checkheap after every operation (slow, but finds bugs early)
build/mdriver -n               # skip the timing runs (fast; correctness and utilization only)
```

For each trace it checks correctness. Every block must be aligned, lie inside memory you own,
never overlap another live block, and keep its contents until it is freed. `realloc` must
preserve data and `calloc` must return zeros. It then measures two things:

* **Space utilization** (CS:APP 9.9.3): the peak total of live payload bytes divided by the peak
  memory footprint, where the footprint is the heap size plus any `mem_map` memory. Requesting
  memory you never use, padding blocks, and fragmentation all lower it.
* **Throughput**: operations per second. Each trace is replayed several times without checks
  and the fastest run is kept. The same traces are also replayed against glibc's `malloc` on the
  same machine, so the comparison is fair on any computer.

The **performance index** combines them:

```
P = 0.6 × average utilization + 0.4 × min(1, your throughput / (50% of glibc's throughput))
```

The traces in `traces/` include synthetic workloads (`tools/gentrace.py` shows exactly what
each one does) and two recorded from real programs, `ls -lR` and a Python script. The
instructor grades with additional hidden traces of the same kinds, so do not tune for the
public files only.

### Recording your own traces

```bash
make tools
HEAPLAB_TRACE=mine.rep LD_PRELOAD=$PWD/build/libmtrace.so python3 -c "print(sum(range(10**6)))"
build/mdriver -f mine.rep
```

This uses run-time library interpositioning (CS:APP 7.13.3). `tools/mtrace.c` defines its own
`malloc` and `free`, which log each call and forward it to glibc's versions.

## 5. Week 1: the address space and an implicit free list

### 5A. Warm-up: where does an address live? (5 points)

Every Linux process can read a description of its own address space from the file
`/proc/self/maps`. Implement `vm_find` and `vm_kind` in `src/vmmap.c`, as specified in
`src/vmmap.h`. `vm_find` finds the region that contains an address. `vm_kind` labels it as
program code, read-only data, data, heap, stack, shared library, anonymous memory or vDSO.

```bash
make && build/tests/vmmap_test        # 11 checks
build/vmexplore                       # your labels on this program's memory map
strace -e trace=brk,mmap,munmap build/vmexplore
```

Run `vmexplore` under `strace` and look at what you see. glibc grows its heap with `brk` for
small requests and gives large ones their own `mmap`. Where does the heap start relative to the
program's data? Why does the stack sit near the top of the address space? Keep these in mind:
your allocator will make the same choices in week 3.

### 5B. An implicit free list allocator (20 points)

Replace the bump allocator in `src/mm.c` with the allocator of CS:APP 9.9.6–9.9.12:

* every block has a header and a footer (boundary tags) holding its size and an allocated bit;
* a prologue and an epilogue block remove the special cases at the two ends of the heap;
* `mm_malloc` searches the blocks for a fit (first fit is fine), splits the block if the rest is
  big enough to be a block of its own, and grows the heap with `mem_sbrk` when nothing fits;
* `mm_free` coalesces immediately with free neighbours, in all four cases of CS:APP 9.9.11;
* `mm_realloc` shrinks in place, grows in place into a free next block when it can, and
  otherwise allocates, copies and frees;
* `mm_calloc` returns zeroed memory and returns `NULL` if `nmemb * size` overflows.

Work out the block layout on paper before you write any code. You need to decide where the
header goes so that the **payload** is 16-byte aligned, what the minimum block size is, and how
a header packs a size and flags into 8 bytes.

**The heap checker.** Write `mm_checkheap(int lineno)`, which walks the heap and checks every
invariant your code relies on. It should return `false` with a clear message if any fails. For
example:

* every block is aligned and lies inside the heap;
* header and footer agree;
* no two free blocks are adjacent;
* the prologue and epilogue are intact;
* sizes are multiples of 16 and at least the minimum.

Run `build/mdriver -c` often. A good checker catches a bug at the operation that caused it,
not thousands of operations later. The checker is graded by reading it, and it will save you
days in week 2.

**Week 1 targets:** every trace correct, average utilization at least 70%.

## 6. Week 2: segregated free lists and performance

An implicit list makes `malloc` scan every block, free or not, so throughput collapses as the
heap grows. Make it fast without giving up utilization (CS:APP 9.9.13–9.9.14):

* **Explicit free lists.** Keep the free blocks in doubly linked lists whose pointers live inside
  the free blocks' payloads, so a search visits only free blocks.
* **Segregated lists.** Use one list per size class (for example exact classes for small sizes,
  then powers of two). Search the request's own class first, then larger ones.
* **Footers only where needed.** An allocated block does not need a footer if each header
  records whether the previous block is allocated. This saves 8 bytes per block, which matters
  for small blocks.
* **Placement policy.** Compare first fit, best fit, and "best of the first few fits" in each
  list. Measure; do not guess.
* **realloc.** Grow in place into a free neighbour, or at the end of the heap by extending it
  only by what is missing. Look at what `08-realloc-grow` and `09-realloc-mixed` do.
* **Fragmentation.** `06-binary` is designed to defeat simple placement policies. Think about
  where small and large blocks end up relative to each other.

Update `mm_checkheap` to check the free lists too:

* every block in a list is free;
* every free block is in exactly one list, and in the right one;
* `next` and `prev` pointers agree;
* there are no cycles.

**Week 2 target:** a performance index of 88 or more (`build/mdriver -l` prints it). The
**design comment** at the top of `mm.c` is graded. It should draw your block layouts and
explain your size classes, placement policy, `realloc` strategy and what your checker verifies.

## 7. Week 3: a real malloc, a debugging allocator, a garbage collector

### 7A. A real malloc for real programs (16 points)

`make` also builds `build/libmm.so`: your `mm.c` plus `src/preload.c`, which defines `malloc`,
`free`, `calloc`, `realloc`, `posix_memalign`, `aligned_alloc`, `memalign`, `valloc`, `pvalloc`
and `malloc_usable_size` on top of your functions. Any dynamically linked program will use it:

```bash
LD_PRELOAD=$PWD/build/libmm.so ls -la /usr/bin
LD_PRELOAD=$PWD/build/libmm.so python3 -c "import json; print(len(json.dumps(list(range(10**6)))))"
```

Make it robust enough for that:

1. **`mm_memalign(alignment, size)`**: a block whose payload is aligned to any power of two
   from 16 up. Find a free block big enough to contain an aligned payload, and give the unused
   front part back as a free block. Return `NULL` for bad alignments.
2. **`mm_usable_size(ptr)`**: how many bytes the caller may really use. Some programs call
   `malloc_usable_size` and then use every one of those bytes.
3. **Large blocks.** Requests of at least `MM_MMAP_THRESHOLD` (256 KiB) get their own mapping
   from `mem_map`, and `mm_free` gives it back with `mem_unmap`. This is what glibc does (you
   saw it in week 1). It means a freed 10 MiB buffer does not leave a hole in your heap forever.
   You need a way to tell such blocks apart in `mm_free`, `mm_realloc` and `mm_usable_size`.
   `realloc` must work across the threshold in both directions.
4. **Thread safety** (`src/preload.c`, TODO 1). Protect every call into `mm_*` with one
   `pthread_mutex_t` (CS:APP 12.5, 12.7.1).
5. **fork safety** (`src/preload.c`, TODO 2). Use `pthread_atfork` so that a child created while
   another thread holds the lock does not inherit a locked mutex (CS:APP 8.4).

Tests: `build/tests/api_test 3a`, `LD_PRELOAD=$PWD/build/libmm.so build/tests/threads`, the same
for `build/tests/forks`, and five real programs (`ls`, `python3`, `sort`, `gzip | gunzip` and a
`bash` loop). Each must produce exactly the output it produces with glibc.

### 7B. A debugging allocator (12 points)

CS:APP 9.11 lists the memory bugs C programs make. Valgrind and AddressSanitizer find them by
watching every memory access. You will catch several of them more cheaply, inside the
allocator, in a debug build: `build/libmm-debug.so` is compiled with `-DMM_DEBUG`. Put your
debug code in `#ifdef MM_DEBUG` blocks in `mm.c`.

Your debug build must print these lines to standard error, exactly starting with `heaplab: `
and the category word:

| Bug | When you detect it | Message (start) | Then |
|---|---|---|---|
| writing past the end, or just before the start, of a block | on `free`/`realloc` of that block | `heaplab: heap-buffer-overflow ...` | `abort()` |
| freeing a block twice | on the second `free` | `heaplab: double-free ...` | `abort()` |
| freeing something that is not a block start (a stack address, `p + 16`) | on that `free` | `heaplab: invalid-free ...` | `abort()` |
| writing into a block after freeing it | when the block leaves quarantine, or at exit | `heaplab: use-after-free ...` | `abort()` |
| a block never freed | at exit (`mm_debug_at_exit`) | `heaplab: leak: <n> bytes at <ptr> ...` | continue |

Suggested design:

* a small header before each block's user data holding the requested size, a magic number
  (live or freed) and the caller's address (`mm_debug_site`, which the shim sets);
* **guard bytes** with a known pattern before and after the user data;
* on `free`, fill the block with a **poison** byte and keep it in a FIFO **quarantine** instead
  of reusing it at once (the idea comes from AddressSanitizer). When a block leaves the
  quarantine, or at exit, check that the poison is intact.

Write the messages with `snprintf` into a local buffer and `write(2, ...)`. Never use `printf`
or `malloc` inside the allocator.

Tests: `tests/debug/*.c` are eight small programs with known bugs, plus one correct program
that must produce no messages. Run them like this:

```bash
LD_PRELOAD=$PWD/build/libmm-debug.so build/tests/debug/overflow
```

### 7C. A garbage collector (12 points)

Implement the conservative mark-and-sweep collector of CS:APP 9.10 in `src/gc.c`, using the
interface in `src/gc.h`. Blocks from `gc_malloc` are never freed by the program. `gc_collect`
frees those that can no longer be reached from:

* the stack;
* the callee-saved registers;
* the program's global variables;
* blocks the program allocated with `mm_malloc`.

First implement the heap-walking functions at the end of `mm.h` in your allocator:
`mm_first_block`, `mm_next_block`, `mm_block_size`, `mm_is_allocated`, and two spare header bits
(`mm_get_gcbits`, `mm_set_gcbits`). The collector uses one bit for "managed" and one for
"marked". `src/gc.c` outlines the algorithm.

Some things to get right:

* **Interior pointers.** A pointer into the middle of a block keeps the whole block alive.
* **The mark phase must not recurse.** A 200,000-node linked list would overflow the C stack.
  Use an explicit mark stack.
* **Finding blocks.** Finding the block that contains an address must be fast. Walk the heap
  once per collection into a sorted array, then binary search it.
* **Registers.** Values held only in registers are invisible to a stack scan.
  `__builtin_unwind_init()` spills the callee-saved registers into the current frame.

Tests: `build/tests/api_test 3c` (heap walking) and `build/tests/gc_test` (eight checks).

## 8. Grading

| Week | Item | How | Points |
|---|---|---|---|
| 1 | `vmmap_test` checks | automatic | 5 |
| 1 | every trace correct | automatic | 12 |
| 1 | average utilization (40% → 0, 70% → full) | automatic | 3 |
| 1 | heap checker | read by the grader | 5 |
| 2 | still correct on every trace | automatic | 5 |
| 2 | performance index (60 → 0, 88 → full, linear) | automatic | 25 |
| 2 | design comment and code quality | read by the grader | 5 |
| 3A | `api_test 3a` (6 checks) | automatic | 6 |
| 3A | threads test | automatic | 3 |
| 3A | fork test | automatic | 2 |
| 3A | five real programs under `LD_PRELOAD` | automatic | 5 |
| 3B | eight debug programs | automatic | 12 |
| 3C | heap walk (`api_test 3c`) | automatic | 2 |
| 3C | `gc_test` (8 checks) | automatic | 10 |
| | **total** | 90 automatic + 10 by hand | **100** |

* `python3 grade.py week1` (or `week2`, `week3`, `all`) shows exactly what the grader will
  compute, except that the final grading adds hidden traces.
* Week 1 is graded on your `week1` tag, week 2 on `week2` and week 3 on `week3`. Each week's
  items are graded on that week's tag only.
* Items a non-reusing allocator would pass for free (correctness, the real programs, the 3A
  checks) only count once your average utilization reaches 50%.
* The fork test counts only once the threads test passes. The "correct program" debug test
  counts only once your debug build detects at least one bug.
* A trace that crashes or times out counts as incorrect. Its utilization counts as 0 in the
  average.

## 9. Submitting

Commit and push as often as you like. To submit a week, tag the commit and push the tag before
the deadline:

```bash
git tag week1
git push origin week1
```

To resubmit before the deadline, move the tag:

```bash
git tag -f week1
git push -f origin week1
```

## 10. Debugging tips

* **Build without optimisation** so `gdb` shows every variable: `make clean && make CFLAGS="-O0 -g"`.
* **`build/mdriver -c -f <trace>`** runs your checker after every operation. The first failure
  points at the operation that broke the heap.
* **Write a `print_heap()`** function that dumps every block (address, size, flags, list links)
  and call it from `gdb` with `call print_heap()`.
* **Watchpoints find corruption.** If the driver reports that a payload changed, note the
  address and run `watch *(long *)0x...` in `gdb`; execution stops at the instruction that
  writes there.
* **Make small traces** that reproduce a bug: copy a few lines from a failing trace into a new
  `.rep` file.
* **Under `LD_PRELOAD`**, crashes happen inside other programs. Use
  `gdb --args env LD_PRELOAD=$PWD/build/libmm.so ls` and `bt` after the crash.
* **Never call `printf` from `mm.c`** in the preload build (it calls `malloc`). Use
  `write(2, ...)`.

## 11. References

* R. E. Bryant and D. R. O'Hallaron, *Computer Systems: A Programmer's Perspective*, 3rd ed.,
  Pearson, 2016:
  * chapter 9: virtual memory, dynamic allocation, garbage collection, memory bugs;
  * 7.13: library interpositioning;
  * 8.4: processes and `fork`;
  * 12.7: thread safety.
* P. R. Wilson, M. S. Johnstone, M. Neely, D. Boles, "Dynamic Storage Allocation: A Survey and
  Critical Review", *IWMM 1995*. The classic survey of placement policies and fragmentation.
* H.-J. Boehm and M. Weiser, "Garbage Collection in an Uncooperative Environment", *Software:
  Practice and Experience* 18(9), 1988. The conservative collector behind 9.10.3.
* K. Serebryany et al., "AddressSanitizer: A Fast Address Sanity Checker", *USENIX ATC 2012*.
  Shadow memory, red zones and quarantine.
* Linux manual pages: `proc(5)` (`/proc/self/maps`), `mmap(2)`, `mprotect(2)`, `brk(2)`,
  `pthread_atfork(3)`, `ld.so(8)` (`LD_PRELOAD`).
