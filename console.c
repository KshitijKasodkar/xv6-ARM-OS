// Console input and output.
// Input is from the keyboard or serial port.
// Output is written to the screen and serial port.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

static void consputc (int);

static int panicked = 0;

static struct {
    struct spinlock lock;
    int locking;
} cons;

static void printint (int xx, int base, int sign)
{
    static char digits[] = "0123456789abcdef";
    char buf[16];
    int i;
    uint x;

    if (sign && (sign = xx < 0)) {
        x = -xx;
    } else {
        x = xx;
    }

    i = 0;

    do {
        buf[i++] = digits[x % base];
    } while ((x /= base) != 0);

    if (sign) {
        buf[i++] = '-';
    }

    while (--i >= 0) {
        consputc(buf[i]);
    }
}
//PAGEBREAK: 50

// Print to the console. only understands %d, %x, %p, %s.
void cprintf (char *fmt, ...)
{
    int i, c, locking;
    uint *argp;
    char *s;

    locking = cons.locking;

    if (locking) {
        acquire(&cons.lock);
    }

    if (fmt == 0) {
        panic("null fmt");
    }

    argp = (uint*) (void*) (&fmt + 1);

    for (i = 0; (c = fmt[i] & 0xff) != 0; i++) {
        if (c != '%') {
            consputc(c);
            continue;
        }

        c = fmt[++i] & 0xff;

        if (c == 0) {
            break;
        }

        switch (c) {
        case 'd':
            printint(*argp++, 10, 1);
            break;

        case 'x':
        case 'p':
            printint(*argp++, 16, 0);
            break;

        case 's':
            if ((s = (char*) *argp++) == 0) {
                s = "(null)";
            }

            for (; *s; s++) {
                consputc(*s);
            }
            break;

        case '%':
            consputc('%');
            break;

        default:
            // Print unknown % sequence to draw attention.
            consputc('%');
            consputc(c);
            break;
        }
    }

    if (locking) {
        release(&cons.lock);
    }
}

void panic (char *s)
{
    cli();

    cons.locking = 0;

    cprintf("cpu%d: panic: ", cpu->id);

    show_callstk(s);
    panicked = 1; // freeze other CPU

    while (1)
        ;
}

//PAGEBREAK: 50
#define BACKSPACE 0x100
#define CRTPORT 0x3d4

void consputc (int c)
{
    if (panicked) {
        cli();
        while (1)
            ;
    }

    if (c == BACKSPACE) {
        uartputc('\b');
        uartputc(' ');
        uartputc('\b');
    } else {
        uartputc(c);
    }

    // cgaputc(c);
}

#define INPUT_BUF 512
struct {
    struct spinlock lock;
    char buf[INPUT_BUF];
    uint r;  // Read index
    uint w;  // Write index
    uint e;  // Edit index
} input;

#define C(x)  ((x)-'@')  // Control-x
// Line editing with Tab completion.
//
// Tab hands the line typed so far to the reader (the shell), which completes
// the command and redraws the line itself. Two consequences handled here:
//
// 1. Echo order. Keys typed after a Tab must not appear on the screen before
//    the shell has redrawn the completed line, or a fast typist sees a
//    different command from the one that runs. So keys after a Tab are kept
//    unechoed ("held") until the reader has consumed the Tab and comes back
//    for more input; then they are echoed in order.
//
// 2. Erasing. Characters already handed to the shell cannot be erased here,
//    so Backspace / Ctrl-U with nothing left to erase locally are passed to
//    the shell, which edits its own copy of the line.

static int tab_committed = 0;   // current line was partly handed over by Tab
static uint echo_x = 0;         // buffer positions below echo_x are echoed
static int tab_holding = 0;         // a Tab is waiting for the reader
static uint hold_pos;           // position of that Tab

// Keys the shell handles and draws itself: Tab, and editing keys passed on.
// Echo stops at each of them until the shell has read it.
static int shell_key(int c)
{
    return c == '\t' || c == '\b' || c == C('U');
}

// Hand one editing key straight to the reader (never echoed).
static void console_pass_key(int c)
{
    if (input.e - input.r < INPUT_BUF) {
        if (!tab_holding) {
            tab_holding = 1;
            hold_pos = input.e;
        }

        input.buf[input.e++ % INPUT_BUF] = c;
        input.w = input.e;
        wakeup(&input.r);
    }
}

// Echo everything typed so far that is not held behind a pending Tab.
static void console_echo_pending(void)
{
    int c;

    while (echo_x < input.e && !(tab_holding && echo_x >= hold_pos)) {
        c = input.buf[echo_x % INPUT_BUF];

        // Tab and keys passed to the shell are drawn by the shell
        if (!shell_key(c)) {
            consputc(c);
        }

        echo_x++;
    }
}

// The reader is back for more input: if it has consumed the pending Tab, the
// shell has finished redrawing, so release the keys typed after it (up to
// the next Tab or passed-on key, if one was typed ahead too).
static void console_release_hold(void)
{
    uint p;

    if (!tab_holding || input.r <= hold_pos) {
        return;
    }

    tab_holding = 0;

    for (p = hold_pos + 1; p < input.e; p++) {
        if (shell_key(input.buf[p % INPUT_BUF])) {
            tab_holding = 1;
            hold_pos = p;
            break;
        }
    }

    console_echo_pending();
}

void consoleintr (int (*getc) (void))
{
    int c;

    acquire(&input.lock);

    while ((c = getc()) >= 0) {
        switch (c) {
        case C('P'):  // Process listing.
            procdump();
            break;

        case C('U'):  // Kill line.
            while ((input.e != input.w) && (input.buf[(input.e - 1) % INPUT_BUF] != '\n')) {
                input.e--;

                if (input.e < echo_x) {     // only erase what is on screen
                    echo_x = input.e;
                    consputc(BACKSPACE);
                }
            }

            // part of the line was already handed to the shell by Tab:
            // let the shell clear that part too
            if (tab_committed) {
                console_pass_key(C('U'));
            }

            break;

        case C('H'):
        case '\x7f':  // Backspace
            if (input.e != input.w) {
                input.e--;

                if (input.e < echo_x) {     // only erase what is on screen
                    echo_x = input.e;
                    consputc(BACKSPACE);
                }

            } else if (tab_committed) {
                // Nothing left to erase here, but Tab already handed the
                // start of this line (and the completed command) to the
                // shell: pass the backspace on so the shell erases it.
                console_pass_key('\b');
            }

            break;

        default:
            if ((c != 0) && (input.e - input.r < INPUT_BUF)) {
                c = (c == '\r') ? '\n' : c;

                if (c == '\t' && !tab_holding) {
                    tab_holding = 1;
                    hold_pos = input.e;
                }

                input.buf[input.e++ % INPUT_BUF] = c;
                console_echo_pending();

                if (c == '\n' || c=='\t' || c == C('D') || input.e == input.r + INPUT_BUF) {
                    input.w = input.e;
                    wakeup(&input.r);
                }

                // remember whether this line was partly handed over by Tab
                if (c == '\t') {
                    tab_committed = 1;
                } else if (c == '\n' || c == C('D')) {
                    tab_committed = 0;
                }
            }

            break;
        }
    }

    release(&input.lock);
}

int consoleread (struct inode *ip, char *dst, int n)
{
    uint target;
    int c;

    iunlock(ip);

    target = n;
    acquire(&input.lock);

    // the reader is back: keys held behind a consumed Tab can be echoed now
    console_release_hold();

    while (n > 0) {
        while (input.r == input.w) {
            console_release_hold();

            if (proc->killed) {
                release(&input.lock);
                ilock(ip);
                return -1;
            }

            sleep(&input.r, &input.lock);
        }

        c = input.buf[input.r++ % INPUT_BUF];

        if (c == C('D')) {  // EOF
            if (n < target) {
                // Save ^D for next time, to make sure
                // caller gets a 0-byte result.
                input.r--;
            }

            break;
        }

        *dst++ = c;
        --n;

        if (c == '\n') {
            break;
        }
    }

    release(&input.lock);
    ilock(ip);

    return target - n;
}

int consolewrite (struct inode *ip, char *buf, int n)
{
    int i;

    iunlock(ip);

    acquire(&cons.lock);

    for (i = 0; i < n; i++) {
        consputc(buf[i] & 0xff);
    }

    release(&cons.lock);

    ilock(ip);

    return n;
}

void consoleinit (void)
{
    initlock(&cons.lock, "console");
    initlock(&input.lock, "input");

    devsw[CONSOLE].write = consolewrite;
    devsw[CONSOLE].read = consoleread;

    cons.locking = 1;
}

