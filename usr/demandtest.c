#include "types.h"
#include "stat.h"
#include "user.h"

int
main(void)
{
    int pages = 8;                 // number of pages to test
    int page_size = 4096;          // xv6 page size = 4KB
    char *p;
    int i, pid;

    // Ask kernel for 'pages' worth of memory via sbrk()
    p = sbrk(pages * page_size);

    if (p == (char*)-1) {
        printf(1, "sbrk failed\n");
        exit();
    }

    printf(1, "Allocated %d pages at %p\n", pages, p);

    // 1. sbrk() only reserved the range: none of the new pages is mapped yet
    printf(1, "\nPage table BEFORE touching (new pages start at VA %p):\n", p);
    printpt();

    // 2. Touch each page -> each first touch is a page fault that the kernel
    //    handles by allocating and mapping a zeroed page (demand paging)
    for (i = 0; i < pages * page_size; i += page_size) {
        if (p[i] != 0) {
            printf(1, "FAIL: new page %d is not zero-filled\n", i / page_size);
            exit();
        }
        p[i] = 'A';
        printf(1, "Touched page %d at VA %p\n", i / page_size, p + i);
    }

    for (i = 0; i < pages * page_size; i += page_size) {
        if (p[i] != 'A') {
            printf(1, "FAIL: page %d lost its data\n", i / page_size);
            exit();
        }
    }

    printf(1, "\nPage table AFTER touching:\n");
    printpt();

    // 3. An access outside the process must not crash the OS: only the
    //    faulting process is killed.
    pid = fork();
    if (pid == 0) {
        char *bad = sbrk(0) + 64 * page_size;   // far beyond the heap
        printf(1, "child: touching unowned VA %p, expect to be killed\n", bad);
        *bad = 'X';
        printf(1, "FAIL: child survived an invalid access\n");
        exit();
    }
    wait();
    printf(1, "parent: child was killed, kernel still running\n");

    printf(1, "--- Demand Paging Test Complete ---\n");

    exit();
}
