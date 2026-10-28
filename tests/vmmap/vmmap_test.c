/*
 * vmmap_test.c - checks vm_find and vm_kind on addresses whose region we know.   (GIVEN)
 * Prints one PASS or FAIL line per check; grade.py counts them.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/mman.h>

#include "vmmap.h"

int initialized_global = 42;            /* .data */
static const char message[] = "a read-only string in .rodata";

static int failures;

static void expect(const char *what, const void *addr, const char *kind)
{
    struct vm_region r;
    memset(&r, 0x55, sizeof r);
    if (vm_find(addr, &r) != 0) {
        printf("FAIL: %-34s vm_find(%p) did not find a region\n", what, addr);
        failures++;
        return;
    }
    if ((uintptr_t)addr < r.start || (uintptr_t)addr >= r.end) {
        printf("FAIL: %-34s region [%#lx, %#lx) does not contain %p\n", what, (unsigned long)r.start,
               (unsigned long)r.end, addr);
        failures++;
        return;
    }
    const char *k = vm_kind(&r);
    if (k == NULL || strcmp(k, kind) != 0) {
        printf("FAIL: %-34s expected \"%s\", got \"%s\" (perms %.4s, path \"%.60s\")\n", what, kind, k ? k : "(null)",
               r.perms, r.path);
        failures++;
        return;
    }
    printf("PASS: %-34s %s  [%#lx, %#lx) %.4s %s\n", what, kind, (unsigned long)r.start, (unsigned long)r.end, r.perms,
           r.path);
}

int main(void)
{
    int local = 7;
    void *small = malloc(64);                               /* glibc: from the brk heap */
    void *big = malloc(4 << 20);                            /* glibc: its own mmap */
    void *anon = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    void *vdso = (void *)getauxval(AT_SYSINFO_EHDR);

    expect("code of main()", (const void *)main, "text");
    expect("an initialized global variable", &initialized_global, "data");
    expect("a string constant", message, "rodata");
    expect("a local variable", &local, "stack");
    expect("a small malloc block", small, "heap");
    expect("a 4 MiB malloc block", big, "anon");
    expect("an anonymous mmap", anon, "anon");
    expect("printf (in libc)", (const void *)printf, "lib");
    if (vdso)
        expect("the vDSO", vdso, "vdso");

    struct vm_region r;
    if (vm_find(NULL, &r) == -1) {
        printf("PASS: %-34s not mapped\n", "address 0 (NULL)");
    } else {
        printf("FAIL: %-34s vm_find(NULL) should return -1\n", "address 0 (NULL)");
        failures++;
    }
    if (vm_find(main, &r) == 0 && strncmp(r.perms, "r-x", 3) == 0 && r.path[0] == '/')
        printf("PASS: %-34s perms %.4s, path %s\n", "fields of the text region", r.perms, r.path);
    else {
        printf("FAIL: %-34s expected perms r-x and an absolute path\n", "fields of the text region");
        failures++;
    }
    free(small);
    free(big);
    return failures ? 1 : 0;
}
