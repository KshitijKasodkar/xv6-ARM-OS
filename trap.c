// The ARM UART, a memory mapped device
#include "types.h"
#include "defs.h"
#include "param.h"
#include "arm.h"
#include "proc.h"
#include "memlayout.h"
#include "mmu.h"

static int mappages (pde_t *pgdir, void *va, uint size, uint pa, int ap)
{
    char *a, *last;
    pte_t *pte;

    a = (char*) align_dn(va, PTE_SZ);
    last = (char*) align_dn((uint)va + size - 1, PTE_SZ);

    for (;;) {
        if ((pte = walkpgdir(pgdir, a, 1)) == 0) {
            return -1;
        }

        if (*pte & PE_TYPES) {
            panic("remap");
        }

        *pte = pa | ((ap & 0x3) << 4) | PE_CACHE | PE_BUF | PTE_TYPE;

        if (a == last) {
            break;
        }

        a += PTE_SZ;
        pa += PTE_SZ;
    }

    return 0;
}


// trap routine
void swi_handler (struct trapframe *r)
{
    proc->tf = r;
    syscall ();

    // a killed process stops here instead of returning to user space
    if (proc->killed) {
        exit();
    }
}

// trap routine
// trap routine
void irq_handler (struct trapframe *r)
{
    // proc points to the current process. If the kernel is
    // running scheduler, proc is NULL.
    if (proc != NULL) {
        proc->tf = r;
    }

    pic_dispatch(r);

    // After PIC handled the interrupt (including the timer interrupt
    // which should have incremented `ticks`), run timed wakeups.
    wakeup_timed();

    // a killed process that was running in user mode stops here
    if (proc != NULL && proc->killed && (r->spsr & MODE_MASK) == USR_MODE) {
        exit();
    }

    // *** Preemption: force reschedule on timer interrupt ***
    if (proc != NULL && proc->state == RUNNING) {
        yield();
    }
}



// trap routine
void reset_handler (struct trapframe *r)
{
    cli();
    cprintf ("reset at: 0x%x \n", r->pc);
}

// trap routine
void und_handler (struct trapframe *r)
{
    cli();
    cprintf ("und at: 0x%x \n", r->pc);
}

// trap routine
// demand-paging handlers for xv6-arm (use terms from your vm.c)

// Set to 1 to log every demand-paging fault on the console. With lazy sbrk()
// every program that uses malloc() faults in its heap, so this is off by
// default; usr/demandtest shows the effect with printpt() instead.
#define PGFAULT_VERBOSE 0

// The main thread of p (p itself unless p is a thread made by thread_create).
static struct proc* main_of(struct proc *p)
{
    return (!p->is_main && p->main_thread) ? p->main_thread : p;
}

// Data abort = page fault.
// Demand paging: sbrk() only grows proc->sz, so the first touch of a new heap
// page lands here with a translation fault. If the address lies inside the
// process (below sz) we allocate a zeroed page, map it and return; the
// faulting instruction is then retried. This also covers the kernel touching
// such a page inside a system call (e.g. read() into a fresh sbrk buffer).
// Any other fault in user mode is a segmentation fault: the process is
// killed and the rest of the system keeps running.
void dabort_handler(struct trapframe *r)
{
    uint dfs, fa, fs;
    int user_mode;

    // read data fault status register
    asm("MRC p15, 0, %[r], c5, c0, 0": [r]"=r" (dfs)::);

    // read the fault address register
    asm("MRC p15, 0, %[r], c6, c0, 0": [r]"=r" (fa)::);

    cli();

    fs = (dfs & 0xF) | ((dfs >> 6) & 0x10);     // ARMv6 fault status DFSR[10,3:0]
    user_mode = (r->spsr & MODE_MASK) == USR_MODE;

    if (proc != NULL) {
        struct proc *mt = main_of(proc);
        uint limit = (mt->sz > proc->sz) ? mt->sz : proc->sz;

        // 0x5 / 0x7 = translation fault (section / page): page not mapped yet
        if ((fs == 0x5 || fs == 0x7) && fa < limit && fa < UADDR_SZ) {
            // Round fault address down to page boundary
            void *va = (void*) align_dn(fa, PTE_SZ);

#if PGFAULT_VERBOSE
            // Print here so you can see demand paging
            cprintf("Page fault at VA %p (allocating new page) for pid %d\n",
                    va, proc->pid);
#endif

            // Allocate a new physical page
            char *mem = alloc_page();

            if (mem != 0) {
                memset(mem, 0, PTE_SZ);

                // Map the newly allocated physical page into the process page table
                if (mappages(proc->pgdir, va, PTE_SZ, v2p(mem), AP_KU) == 0) {
                    // Invalidate the TLB entry for this VA so the CPU sees the new mapping
                    asm("MCR p15, 0, %[r], c8, c7, 1" : : [r]"r"(va) : );

                    // success: the faulting instruction will be retried
                    return;
                }
                free_page(mem);
            }

            cprintf("pid %d %s: out of memory while paging in 0x%x\n",
                    proc->pid, proc->name, fa);

            if (!user_mode) {
                panic("out of memory in kernel page fault");
            }

            proc->killed = 1;
            exit();
        }

        if (user_mode) {
            cprintf("pid %d %s: segmentation fault at 0x%x (pc 0x%x, fsr 0x%x), killed\n",
                    proc->pid, proc->name, fa, r->pc, dfs);
            proc->killed = 1;
            exit();
        }
    }

    // A fault inside the kernel itself is a kernel bug
    cprintf("data abort in kernel: pc 0x%x fa 0x%x reason 0x%x\n", r->pc, fa, dfs);
    dump_trapframe(r);
    panic("kernel data abort");
}

// Prefetch abort: the CPU tried to execute from an address it cannot fetch.
// A user process doing this is killed; in the kernel it is a bug.
void iabort_handler (struct trapframe *r)
{
    uint ifs;

    // read instruction fault status register
    asm("MRC p15, 0, %[r], c5, c0, 1": [r]"=r" (ifs)::);

    cli();

    if (proc != NULL && (r->spsr & MODE_MASK) == USR_MODE) {
        cprintf("pid %d %s: bad instruction address 0x%x (fsr 0x%x), killed\n",
                proc->pid, proc->name, r->pc, ifs);
        proc->killed = 1;
        exit();
    }

    cprintf ("prefetch abort at: 0x%x (reason: 0x%x)\n", r->pc, ifs);
    dump_trapframe (r);
    panic("kernel prefetch abort");
}

// trap routine
void na_handler (struct trapframe *r)
{
    cli();
    cprintf ("n/a at: 0x%x \n", r->pc);
}

// trap routine
void fiq_handler (struct trapframe *r)
{
    cli();
    cprintf ("fiq at: 0x%x \n", r->pc);
}

// low-level init code: in real hardware, lower memory is usually mapped
// to flash during startup, we need to remap it to SDRAM
void trap_init ( )
{
    volatile uint32 *ram_start;
    char *stk;
    int i;
    uint modes[] = {FIQ_MODE, IRQ_MODE, ABT_MODE, UND_MODE};

    // the opcode of PC relative load (to PC) instruction LDR pc, [pc,...]
    static uint32 const LDR_PCPC = 0xE59FF000U;

    // create the excpetion vectors
    ram_start = (uint32*)VEC_TBL;

    ram_start[0] = LDR_PCPC | 0x18; // Reset (SVC)
    ram_start[1] = LDR_PCPC | 0x18; // Undefine Instruction (UND)
    ram_start[2] = LDR_PCPC | 0x18; // Software interrupt (SVC)
    ram_start[3] = LDR_PCPC | 0x18; // Prefetch abort (ABT)
    ram_start[4] = LDR_PCPC | 0x18; // Data abort (ABT)
    ram_start[5] = LDR_PCPC | 0x18; // Not assigned (-)
    ram_start[6] = LDR_PCPC | 0x18; // IRQ (IRQ)
    ram_start[7] = LDR_PCPC | 0x18; // FIQ (FIQ)

    ram_start[8]  = (uint32)trap_reset;
    ram_start[9]  = (uint32)trap_und;
    ram_start[10] = (uint32)trap_swi;
    ram_start[11] = (uint32)trap_iabort;
    ram_start[12] = (uint32)trap_dabort;
    ram_start[13] = (uint32)trap_na;
    ram_start[14] = (uint32)trap_irq;
    ram_start[15] = (uint32)trap_fiq;

    // initialize the stacks for different mode
    for (i = 0; i < sizeof(modes)/sizeof(uint); i++) {
        stk = alloc_page ();

        if (stk == NULL) {
            panic("failed to alloc memory for irq stack");
        }

        set_stk (modes[i], (uint)stk);
    }
}

void dump_trapframe (struct trapframe *tf)
{
    cprintf ("r14_svc: 0x%x\n", tf->r14_svc);
    cprintf ("   spsr: 0x%x\n", tf->spsr);
    cprintf ("     r0: 0x%x\n", tf->r0);
    cprintf ("     r1: 0x%x\n", tf->r1);
    cprintf ("     r2: 0x%x\n", tf->r2);
    cprintf ("     r3: 0x%x\n", tf->r3);
    cprintf ("     r4: 0x%x\n", tf->r4);
    cprintf ("     r5: 0x%x\n", tf->r5);
    cprintf ("     r6: 0x%x\n", tf->r6);
    cprintf ("     r7: 0x%x\n", tf->r7);
    cprintf ("     r8: 0x%x\n", tf->r8);
    cprintf ("     r9: 0x%x\n", tf->r9);
    cprintf ("    r10: 0x%x\n", tf->r10);
    cprintf ("    r11: 0x%x\n", tf->r11);
    cprintf ("    r12: 0x%x\n", tf->r12);
    cprintf ("     pc: 0x%x\n", tf->pc);
}
