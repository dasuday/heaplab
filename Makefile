# heaplab - build everything with `make`, test with `make grade` (see README.md)

CC      ?= gcc
CFLAGS  ?= -O2 -g                         # override freely: make CFLAGS="-O0 -g"
ALL_CFLAGS := $(CFLAGS) -Wall -Wextra -std=gnu11 -Isrc
B       := build

ALLOC   := src/mm.c src/memlib.c
HEADERS := $(wildcard src/*.h)
DEBUG_TESTS := overflow underflow double_free invalid_free stack_free use_after_free leak clean

.PHONY: all tools grade clean
all: $(B)/mdriver $(B)/libmm.so $(B)/libmm-debug.so $(B)/vmexplore \
     $(B)/tests/vmmap_test $(B)/tests/api_test $(B)/tests/gc_test \
     $(B)/tests/threads $(B)/tests/forks $(addprefix $(B)/tests/debug/,$(DEBUG_TESTS)) \
     $(B)/libmtrace.so

$(B) $(B)/tests $(B)/tests/debug:
	@mkdir -p $@

# the trace-driven driver, linked directly with your allocator
$(B)/mdriver: driver/mdriver.c $(ALLOC) $(HEADERS) | $(B)
	$(CC) $(ALL_CFLAGS) -o $@ driver/mdriver.c $(ALLOC)

# your allocator as a drop-in malloc for any program (week 3A) and its debug build (week 3B)
$(B)/libmm.so: src/preload.c $(ALLOC) $(HEADERS) | $(B)
	$(CC) $(ALL_CFLAGS) -fPIC -shared -o $@ src/preload.c $(ALLOC) -lpthread

$(B)/libmm-debug.so: src/preload.c $(ALLOC) $(HEADERS) | $(B)
	$(CC) $(ALL_CFLAGS) -DMM_DEBUG -fPIC -shared -o $@ src/preload.c $(ALLOC) -lpthread

# week 1 warm-up
$(B)/vmexplore: tools/vmexplore.c src/vmmap.c src/vmmap.h | $(B)
	$(CC) $(ALL_CFLAGS) -o $@ tools/vmexplore.c src/vmmap.c

$(B)/tests/vmmap_test: tests/vmmap/vmmap_test.c src/vmmap.c src/vmmap.h | $(B)/tests
	$(CC) $(ALL_CFLAGS) -o $@ tests/vmmap/vmmap_test.c src/vmmap.c

# week 3 tests
$(B)/tests/api_test: tests/api/api_test.c $(ALLOC) $(HEADERS) | $(B)/tests
	$(CC) $(ALL_CFLAGS) -o $@ tests/api/api_test.c $(ALLOC)

$(B)/tests/gc_test: tests/gc/gc_test.c src/gc.c $(ALLOC) $(HEADERS) | $(B)/tests
	$(CC) $(ALL_CFLAGS) -o $@ tests/gc/gc_test.c src/gc.c $(ALLOC)

$(B)/tests/threads: tests/preload/threads.c | $(B)/tests
	$(CC) $(ALL_CFLAGS) -o $@ $< -lpthread

$(B)/tests/forks: tests/preload/forks.c | $(B)/tests
	$(CC) $(ALL_CFLAGS) -o $@ $< -lpthread

# the buggy programs are compiled without optimisation so every bug really happens
$(B)/tests/debug/%: tests/debug/%.c | $(B)/tests/debug
	$(CC) -O0 -g -w -o $@ $<

# record traces from real programs (see tools/mtrace.c)
$(B)/libmtrace.so: tools/mtrace.c | $(B)
	$(CC) $(ALL_CFLAGS) -fPIC -shared -o $@ $< -ldl -lpthread

tools: $(B)/libmtrace.so $(B)/vmexplore

grade: all
	python3 grade.py

clean:
	rm -rf $(B)
