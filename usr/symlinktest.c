#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

static int failures = 0;

static void
check(int ok, char *what)
{
    printf(1, "%s %s\n", ok ? "OK:  " : "FAIL:", what);
    if(!ok)
        failures++;
}

// read up to n-1 bytes of path into out; returns bytes read or -1
static int
readfile(char *path, char *out, int n)
{
    int fd, cc;

    fd = open(path, O_RDONLY);
    if(fd < 0)
        return -1;
    cc = read(fd, out, n - 1);
    close(fd);
    if(cc < 0)
        return -1;
    out[cc] = 0;
    return cc;
}

int main(void) {
    char buf[64];
    int fd;

    // start clean
    unlink("target.txt");
    unlink("link.txt");
    unlink("link2.txt");
    unlink("loop1");
    unlink("loop2");
    unlink("dangling");

    // the file the links point to
    fd = open("target.txt", O_CREATE | O_RDWR);
    if(fd < 0){
        printf(1, "FAIL: cannot create target.txt\n");
        exit();
    }
    write(fd, "hello from target\n", 18);
    close(fd);

    // 1. a symlink resolves to its target on open()
    check(symlink("target.txt", "link.txt") == 0, "symlink(\"target.txt\", \"link.txt\")");
    check(readfile("link.txt", buf, sizeof(buf)) == 18 && strcmp(buf, "hello from target\n") == 0,
          "open(link.txt) reads the target's data");
    printf(1, "      read: %s", buf);

    // 2. writing through the link changes the target
    fd = open("link.txt", O_RDWR);
    check(fd >= 0, "open(link.txt, O_RDWR)");
    write(fd, "HELLO", 5);
    close(fd);
    check(readfile("target.txt", buf, sizeof(buf)) > 0 && buf[0] == 'H',
          "write through link.txt is visible in target.txt");

    // 3. a link to a link is followed recursively
    check(symlink("link.txt", "link2.txt") == 0, "symlink(\"link.txt\", \"link2.txt\")");
    check(readfile("link2.txt", buf, sizeof(buf)) == 18, "open(link2.txt) follows two links");

    // 4. a link whose target does not exist cannot be opened
    check(symlink("no_such_file", "dangling") == 0, "symlink(\"no_such_file\", \"dangling\")");
    check(open("dangling", O_RDONLY) < 0, "open(dangling) fails");

    // 5. a cycle of links is detected (depth limit 10) instead of looping
    symlink("loop2", "loop1");
    symlink("loop1", "loop2");
    check(open("loop1", O_RDONLY) < 0, "open(loop1) fails on the cycle loop1 -> loop2 -> loop1");

    // 6. deleting the target leaves the link dangling
    unlink("target.txt");
    check(open("link.txt", O_RDONLY) < 0, "open(link.txt) fails after target.txt is deleted");

    // clean up
    unlink("link.txt");
    unlink("link2.txt");
    unlink("loop1");
    unlink("loop2");
    unlink("dangling");

    if(failures == 0)
        printf(1, "symlinktest: ALL TESTS PASSED\n");
    else
        printf(1, "symlinktest: %d FAILED\n", failures);
    exit();
}
