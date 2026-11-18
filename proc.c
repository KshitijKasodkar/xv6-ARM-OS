#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "arm.h"
#include "proc.h"
#include "spinlock.h"
#include "uproc.h"
#include "pstat.h"

#define PGSIZE 4096
struct proc* myproc(void);
//
// Process initialization:
// process initialize is somewhat tricky.
//  1. We need to fake the kernel stack of a new process as if the process
//     has been interrupt (a trapframe on the stack), this would allow us
//     to "return" to the correct user instruction.
//  2. We also need to fake the kernel execution for this new process. When
//     swtch switches to this (new) process, it will switch to its stack,
//     and reload registers with the saved context. We use forkret as the
//     return address (in lr register). (In x86, it will be the return address
//     pushed on the stack by the process.)
//
// The design of context switch in xv6 is interesting: after initialization,
// each CPU executes in the scheduler() function. The context switch is not
// between two processes, but instead, between the scheduler. Think of scheduler
// as the idle process.
//

#define RAND_MAX 0x7fffffff
uint rseed = 0;
void
srand(uint seed) {
  rseed = seed;
}

uint rand() {
    return rseed = (rseed * 1103515245 + 12345) & RAND_MAX;
}

struct {
    struct spinlock lock;
    struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

// A thread made by thread_create (it shares its main thread's address space).
#define IS_THREAD(p) (!(p)->is_main && (p)->main_thread != 0)
struct proc *proc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void pinit(void)
{
    initlock(&ptable.lock, "ptable");
}
struct proc*
myproc(void)
{
    /* Return the currently-running process for this CPU.
       This xv6-arm tree already manages 'proc' as the current process
       pointer (see scheduler/swtch code). A simple accessor is sufficient
       to satisfy references from sysproc.c and other files. */
    return proc;
}
//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.

// in proc.c, add near other helpers (above scheduler)
extern uint ticks;   // make sure ticks is declared/accessible

void
wakeup_timed(void)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == SLEEPING && p->waketicks != 0 && p->waketicks <= ticks){
      // compute actual sleep duration
      uint slept = ticks - p->sleep_start;

      if(slept > 0){
        // instead of adding full slept time, increment boosts in steps
        p->boostsleft += slept;   // boosted for as many lotteries as ticks slept
      }

      // clear fields
      p->sleep_start = 0;
      p->waketicks = 0;

      // clear any channel (optional) and set runnable
      p->chan = 0;
      p->state = RUNNABLE;
    }
  }
  release(&ptable.lock);
}



static struct proc* allocproc(void)
{
    struct proc *p;
    char *sp;

    acquire(&ptable.lock);

    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if(p->state == UNUSED) {
            goto found;
        }

    }

    release(&ptable.lock);
    return 0;

    found:
    p->state = EMBRYO;
    p->pid = nextpid++;
    p->syscall_count = 0;   //ADDED

    p->tickets = 1; // Default to 1 ticket
    // Initialize lottery/boost fields

p->sleep_start = 0;
p->waketicks   = 0;
// 'tickets' should already be set to 1 here by your Task 1.1 (or by fork inheritance)
    // default ticket count
p->runticks = 0;
p->boostsleft = 0;

 p->is_main = 0;
p->main_thread = 0;
p->pgdir = 0;           // a new slot owns no address space yet
p->next_thread = 0;
p->ustack = 0;
p->retval = 0;
p->killed = 0;



release(&ptable.lock);

    // Allocate kernel stack.
    if((p->kstack = alloc_page ()) == 0){
        p->state = UNUSED;
        return 0;
    }

    sp = p->kstack + KSTACKSIZE;

    // Leave room for trap frame.
    sp -= sizeof (*p->tf);
    p->tf = (struct trapframe*)sp;

    // Set up new context to start executing at forkret,
    // which returns to trapret.
    sp -= 4;
    *(uint*)sp = (uint)trapret;

    sp -= 4;
    *(uint*)sp = (uint)p->kstack + KSTACKSIZE;

    sp -= sizeof (*p->context);
    p->context = (struct context*)sp;
    memset(p->context, 0, sizeof(*p->context));

    // skip the push {fp, lr} instruction in the prologue of forkret.
    // This is different from x86, in which the harderware pushes return
    // address before executing the callee. In ARM, return address is
    // loaded into the lr register, and push to the stack by the callee
    // (if and when necessary). We need to skip that instruction and let
    // it use our implementation.
    p->context->lr = (uint)forkret+4;

    return p;
}

void error_init ()
{
    panic ("failed to craft first process\n");
}


//PAGEBREAK: 32
// hand-craft the first user process. We link initcode.S into the kernel
// as a binary, the linker will generate __binary_initcode_start/_size
void userinit(void)
{
    struct proc *p;
    extern char _binary_initcode_start[], _binary_initcode_size[];

    p = allocproc();
    initproc = p;

    if((p->pgdir = kpt_alloc()) == NULL) {
        panic("userinit: out of memory?");
    }

    inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);

    p->sz = PTE_SZ;

    // craft the trapframe as if
    memset(p->tf, 0, sizeof(*p->tf));

    p->tf->r14_svc = (uint)error_init;
    p->tf->spsr = spsr_usr ();
    p->tf->sp_usr = PGSIZE;	// set the user stack
    p->tf->lr_usr = 0;

    p->is_main = 1;
    p->main_thread = p;
    p->next_thread = 0;
    p->ustack = 0;
    p->retval = 0;
    // set the user pc. The actual pc loaded into r15_usr is in
    // p->tf, the trapframe.
    p->tf->pc = 0;					// beginning of initcode.S

    safestrcpy(p->name, "initcode", sizeof(p->name));
    p->cwd = namei("/");

    p->state = RUNNABLE;
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
    uint sz;
    // all threads share one address space, owned by the main thread
    struct proc *mt = IS_THREAD(proc) ? proc->main_thread : proc;

    sz = mt->sz;

    if(n > 0){
        if((sz = allocuvm(mt->pgdir, sz, sz + n)) == 0) {
            return -1;
        }

    } else if(n < 0){
        if((sz = deallocuvm(mt->pgdir, sz, sz + n)) == 0) {
            return -1;
        }
    }

    mt->sz = sz;
    proc->sz = sz;
    switchuvm(proc);

    return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int fork(void)
{
    int i, pid;
    struct proc *np;

    // Allocate process.
    if((np = allocproc()) == 0) {
        return -1;
    }

    np->tickets = proc->tickets; // Child inherits parent's tickets
    // In fork(): inherit tickets from parent, but NOT boosts
  // (or p->tickets) depending on your variable name
np->boostsleft = 0;                 // children never inherit boosts
np->waketicks   = 0;                 // clear any wake target


    // Copy process state from p.
    if((np->pgdir = copyuvm(proc->pgdir, proc->sz)) == 0){
        free_page(np->kstack);
        np->kstack = 0;
        np->state = UNUSED;
        return -1;
    }

    np->sz = proc->sz;
    np->parent = proc;
    *np->tf = *proc->tf;

    np->is_main = 1;          // forked process is a main thread
    np->main_thread = np;     // main thread points to itself
    np->next_thread = 0;
    np->ustack = 0;
    np->retval = 0;

    // Clear r0 so that fork returns 0 in the child.
    np->tf->r0 = 0;

    for(i = 0; i < NOFILE; i++) {
        if(proc->ofile[i]) {
            np->ofile[i] = filedup(proc->ofile[i]);
        }
    }

    np->cwd = idup(proc->cwd);

    pid = np->pid;
    np->state = RUNNABLE;
    safestrcpy(np->name, proc->name, sizeof(proc->name));

    return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void exit(void)
{
    struct proc *p;
    int fd;

    if(proc == initproc) {
        panic("init exiting");
    }

    // exit() called from a thread ends only that thread (same as thread_exit);
    // thread_join() in the main thread then reaps it.
    if(IS_THREAD(proc)) {
        thread_exit(0);
    }

    // The main thread is leaving: stop and reap its other threads first, so
    // none of them keeps running on the address space the parent will free.
    reap_threads(proc);

    // Close all open files.
    for(fd = 0; fd < NOFILE; fd++){
        if(proc->ofile[fd]){
            fileclose(proc->ofile[fd]);
            proc->ofile[fd] = 0;
        }
    }

    iput(proc->cwd);
    proc->cwd = 0;

    acquire(&ptable.lock);

    // Parent might be sleeping in wait().
    wakeup1(proc->parent);

    // Pass abandoned children to init.
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->parent == proc){
            p->parent = initproc;

            if(p->state == ZOMBIE) {
                wakeup1(initproc);
            }
        }
    }

    // Jump into the scheduler, never to return.
    proc->state = ZOMBIE;
    sched();

    panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(void)
{
    struct proc *p;
    int havekids, pid;

    acquire(&ptable.lock);

    for(;;){
        // Scan through table looking for zombie children.
        havekids = 0;

        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
            if(p->parent != proc || IS_THREAD(p)) {
                continue;
            }

            havekids = 1;

            if(p->state == ZOMBIE){
                // Found one.
                pid = p->pid;
                free_page(p->kstack);
                p->kstack = 0;
                freevm(p->pgdir);

                p->pgdir = 0;
                p->sz = 0;
                p->ustack = 0;

                p->state = UNUSED;
                p->pid = 0;
                p->parent = 0;
                p->name[0] = 0;
                p->killed = 0;
                release(&ptable.lock);

                return pid;
            }
        }

        // No point waiting if we don't have any children.
        if(!havekids || proc->killed){
            release(&ptable.lock);
            return -1;
        }

        // Wait for children to exit.  (See wakeup1 call in proc_exit.)
        sleep(proc, &ptable.lock);  //DOC: wait-sleep
    }
}

// Wait for the child process with the given pid to exit and return its pid.
// Return -1 if pid is not a child of this process.
int waitpid(int pid)
{
    struct proc *p;
    int found;

    acquire(&ptable.lock);

    for(;;){
        found = 0;

        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
            if(p->pid != pid || p->parent != proc || IS_THREAD(p)) {
                continue;
            }

            found = 1;

            if(p->state == ZOMBIE){
                free_page(p->kstack);
                p->kstack = 0;
                freevm(p->pgdir);

                p->pgdir = 0;
                p->sz = 0;
                p->ustack = 0;

                p->state = UNUSED;
                p->pid = 0;
                p->parent = 0;
                p->name[0] = 0;
                p->killed = 0;
                release(&ptable.lock);

                return pid;
            }
        }

        if(!found || proc->killed){
            release(&ptable.lock);
            return -1;
        }

        // exit() of the child wakes its parent
        sleep(proc, &ptable.lock);
    }
}

// Called by a main thread on its way out: kill every other thread of the
// process, wait until each one has stopped, and free it (kernel stack, files).
// The shared page table is freed later with the main thread.
void reap_threads(struct proc *mt)
{
    struct proc *p, *z;
    int alive, fd;

    for(;;){
        z = 0;
        alive = 0;

        acquire(&ptable.lock);

        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
            if(p == mt || p->state == UNUSED || p->is_main || p->main_thread != mt) {
                continue;
            }

            if(p->state == ZOMBIE){
                z = p;
                break;
            }

            alive = 1;
            p->killed = 1;

            if(p->state == SLEEPING) {
                p->state = RUNNABLE;
            }
        }

        if(z == 0 && !alive){
            mt->next_thread = 0;
            release(&ptable.lock);
            return;
        }

        if(z == 0){
            // a thread that stops wakes its main thread (see thread_exit)
            sleep(mt, &ptable.lock);
            release(&ptable.lock);
            continue;
        }

        // detach z so no thread_join() can reap it at the same time
        z->main_thread = 0;
        z->parent = 0;
        release(&ptable.lock);

        for(fd = 0; fd < NOFILE; fd++){
            if(z->ofile[fd]){
                fileclose(z->ofile[fd]);
                z->ofile[fd] = 0;
            }
        }

        if(z->cwd){
            iput(z->cwd);
            z->cwd = 0;
        }

        acquire(&ptable.lock);

        if(z->kstack){
            free_page(z->kstack);
            z->kstack = 0;
        }

        z->pgdir = 0;
        z->ustack = 0;
        z->next_thread = 0;
        z->state = UNUSED;
        z->pid = 0;
        z->name[0] = 0;
        z->killed = 0;
        release(&ptable.lock);
    }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.

// This function finds a lottery winner but does NOT release the lock if a winner is found.
// pick a winner using boosted lottery
// Choose a RUNNABLE process using lottery scheduling
struct proc* hold_lottery(int total_tickets) {
    uint random_number = rand();    // This number is between 0->4 billion
    int winner = random_number % total_tickets;   // number between 0 and total_tickets-1
    int counter = 0;

    struct proc *p;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if(p->state == RUNNABLE) {
            int effective_tickets = p->tickets;
            if(p->boostsleft > 0)
                effective_tickets *= 2;
            counter += effective_tickets;
            if(counter > winner) {   // strictly greater
                return p;
            }
        }
    }
    return 0;   // should never happen
}

// Scheduler picks a process using boosted lottery
void scheduler(void)
{
    struct proc *p;

    for(;;)
    {
        sti();
        acquire(&ptable.lock);

        int total_tickets = 0;
        // Calculate total tickets, considering boosts
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
            if(p->state == RUNNABLE) {
                if(p->boostsleft > 0)
                    total_tickets += p->tickets * 2;
                else
                    total_tickets += p->tickets;
            }
        }

        if(total_tickets > 0) {
            struct proc *chosen = hold_lottery(total_tickets);
            if(chosen) {
                proc = chosen;
                switchuvm(chosen); // switch to the chosen proc's address space
                chosen->state = RUNNING;
                chosen->runticks++;
                swtch(&cpu->scheduler, chosen->context); // context switch
                proc = 0; // finished
            }
        }
        // After every lottery, decrement boosts_left for ALL runnable participants.
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
            if(p->state == RUNNABLE && p->boostsleft > 0) {
                p->boostsleft--;
            }
        }
        release(&ptable.lock);
    }
}


// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state.
void sched(void)
{
    int intena;

    //show_callstk ("sched");

    if(!holding(&ptable.lock)) {
        panic("sched ptable.lock");
    }

    if(cpu->ncli != 1) {
        panic("sched locks");
    }

    if(proc->state == RUNNING) {
        panic("sched running");
    }

    if(int_enabled ()) {
        panic("sched interruptible");
    }

    intena = cpu->intena;
    swtch(&proc->context, cpu->scheduler);
    cpu->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void)
{
    acquire(&ptable.lock);  //DOC: yieldlock
    proc->state = RUNNABLE;
    sched();
    release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void forkret(void)
{
    static int first = 1;

    // Still holding ptable.lock from scheduler.
    release(&ptable.lock);

    if (first) {
        // Some initialization functions must be run in the context
        // of a regular process (e.g., they call sleep), and thus cannot
        // be run from main().
        first = 0;
        initlog();
    }

    // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void sleep(void *chan, struct spinlock *lk)
{
    //show_callstk("sleep");

    if(proc == 0) {
        panic("sleep");
    }

    if(lk == 0) {
        panic("sleep without lk");
    }

    // Must acquire ptable.lock in order to change p->state and then call
    // sched. Once we hold ptable.lock, we can be guaranteed that we won't
    // miss any wakeup (wakeup runs with ptable.lock locked), so it's okay
    // to release lk.
    if(lk != &ptable.lock){  //DOC: sleeplock0
        acquire(&ptable.lock);  //DOC: sleeplock1
        release(lk);
    }

    // Go to sleep.
    proc->chan = chan;
    // right before: p->state = SLEEPING;
proc->sleep_start = ticks;   // record when the proc actually goes to sleep

    proc->state = SLEEPING;
    sched();

    // Tidy up.
    proc->chan = 0;

    // Reacquire original lock.
    if(lk != &ptable.lock){  //DOC: sleeplock2
        release(&ptable.lock);
        acquire(lk);
    }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan. The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->state == SLEEPING && p->chan == chan) {

      if (chan == &ticks) {
        // Only wake if enough time has passed
        if (ticks - p->sleep_start < p->sleep_ticks)
          continue;  // not yet time to wake
      }

      // Compute how long it actually slept → convert to boosts
      int slept = (int)(ticks - p->sleep_start);
      if (slept > 0) {
         p->boostsleft += slept;
        
      }

      // Clear sleep bookkeeping
      p->sleep_start = 0;
      p->sleep_ticks = 0;

      // Finally, mark as runnable
      p->state = RUNNABLE;
    }
  }
}

// Wake up all processes sleeping on chan.
void wakeup(void *chan)
{
    acquire(&ptable.lock);
    wakeup1(chan);
    release(&ptable.lock);
}

// Kill the process with the given pid. Process won't exit until it returns
// to user space (see trap in trap.c).
int kill(int pid)
{
    struct proc *p;

    acquire(&ptable.lock);

    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->pid == pid){
            p->killed = 1;

            // Wake process from sleep if necessary.
            if(p->state == SLEEPING) {
                p->state = RUNNABLE;
            }

            release(&ptable.lock);
            return 0;
        }
    }

    release(&ptable.lock);
    return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging. Runs when user
// types ^P on console. No lock to avoid wedging a stuck machine further.
void procdump(void)
{
    static char *states[] = {
            [UNUSED]    "unused",
            [EMBRYO]    "embryo",
            [SLEEPING]  "sleep ",
            [RUNNABLE]  "runble",
            [RUNNING]   "run   ",
            [ZOMBIE]    "zombie"
    };

    struct proc *p;
    char *state;

    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->state == UNUSED) {
            continue;
        }

        if(p->state >= 0 && p->state < NELEM(states) && states[p->state]) {
            state = states[p->state];
        } else {
            state = "???";
        }

        cprintf("%d %s %s\n", p->pid, state, p->name);
    }

    show_callstk("procdump: \n");
}


// ADDED ---------------------------------------- 

int
getprocs(struct uproc *table, int max)     
{
  struct proc *p;
  int count = 0;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC] && count < max; p++) {
    if(p->state == UNUSED)
      continue;

    table[count].pid = p->pid;
    table[count].ppid = p->parent ? p->parent->pid : 0;
    safestrcpy(table[count].name, p->name, sizeof(table[count].name));
    table[count].state = p->state;
    table[count].syscalls = p->syscall_count;


    count++;
  }
  release(&ptable.lock);

  return count;
}

int
getpinfo(struct pstat *ps)
{
    struct proc *p;

    if(ps == 0) return -1;

    acquire(&ptable.lock);
    for(int i = 0; i < NPROC; i++){
        p = &ptable.proc[i];
        if(p->state != UNUSED){
            ps->inuse[i]      = 1;
            ps->pid[i]        = p->pid;
            ps->tickets[i]    = p->tickets;
            ps->runticks[i]   = p->runticks;
            ps->boostsleft[i] = p->boostsleft;
        } else {
            ps->inuse[i] = 0;
            ps->pid[i] = ps->tickets[i] = ps->runticks[i] = ps->boostsleft[i] = 0;
        }
    }
    release(&ptable.lock);
    return 0;
}

int
settickets_kernel(int pid, int n_tickets)
{
    struct proc *p;

    if(n_tickets <= 0)
        return -1;

    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->pid == pid){
            p->tickets = n_tickets;
            p->boostsleft = 0;
            release(&ptable.lock);
            return 0;
        }
    }
    release(&ptable.lock);
    return -1;
}

// This is a function from vm.c that we need to use.
// Make sure this line is in proc.c before your sys_printpt function.
// pte_t *walkpgdir(pde_t*, const void*, int);-- not needed as we have made changes in vm.c made the function into it as public then def.h.

int
sys_printpt(void)
{
  cprintf("--- Page Table Entries for PID %d ---\n", proc->pid);
  pte_t *pte;
  uint va;

  // 1. Print the first 10 page table entries
  cprintf("--- First 10 Pages ---\n");
  // Use PTE_SZ instead of PGSIZE
  for(va = 0; va < 10 * PTE_SZ; va += PTE_SZ) {
    pte = walkpgdir(proc->pgdir, (void *)va, 0);

    // Instead of PTE_V, we check if the type bits are non-zero.
    // In your mmu.h, a type of 0x00 means the page is invalid.
    if(pte && (*pte & 0x3)) { 
      // Use your version's helpers: PTE_ADDR for the Physical Address (PA)
      // and a mask for the flags.
      cprintf("VA: 0x%x -> PA: 0x%x, Flags: 0x%x, PTE: 0x%x\n",
              va, PTE_ADDR(*pte), (*pte & (PTE_SZ - 1)), *pte);
    } else {
      cprintf("VA: 0x%x -> Not Mapped\n", va);
    }
  }

  // 2. Print the last 10 page table entries
  cprintf("--- Last 10 Pages ---\n");
  // Use UADDR_SZ instead of MAXVA
  for(va = UADDR_SZ - (10 * PTE_SZ); va < UADDR_SZ; va += PTE_SZ) {
    pte = walkpgdir(proc->pgdir, (void *)va, 0);

    if(pte && (*pte & 0x3)) {
      cprintf("VA: 0x%x -> PA: 0x%x, Flags: 0x%x, PTE: 0x%x\n",
              va, PTE_ADDR(*pte), (*pte & (PTE_SZ - 1)), *pte);
    } else {
      cprintf("VA: 0x%x -> Not Mapped\n", va);
    }
  }

  cprintf("-------------------------------------\n");
  return 0;
}



int thread_create_kernel(struct proc *cur, uint *tidptr, void *start, void *arg) {
    struct proc *np;
    struct proc *mt;
    uint oldsz, newsz;
    uint stack_top;
    int pid;


    if (cur == 0)
        return -1;

    mt = cur->is_main ? cur : cur->main_thread;

    np = allocproc();//allocate to thread

    if (np == 0)
        return -1;

    // threads share the main thread's page table (allocproc starts with none)
    np->pgdir = mt->pgdir;//all threads of a process have same pgdir so set its equal to main threads pgdir


    oldsz = (mt->sz + PGSIZE - 1) & ~(PGSIZE - 1);
    newsz = oldsz + PGSIZE;

    if (allocuvm(mt->pgdir, oldsz, newsz) == 0) {
        np->state = UNUSED;
        return -1;
    }

    mt->sz = newsz;
    np->sz = mt->sz;
    np->ustack = oldsz;

    np->is_main = 0;
    np->main_thread = mt;
    np->next_thread = 0;
    np->retval = 0;
    np->killed = 0;

    for (int i = 0; i < NOFILE; i++)
        if (mt->ofile[i])
            np->ofile[i] = filedup(mt->ofile[i]);
    if (mt->cwd)
        np->cwd = idup(mt->cwd);

    safestrcpy(np->name, mt->name, sizeof(mt->name));
    np->parent = mt;

    stack_top = np->ustack + PGSIZE;

    *np->tf = *mt->tf;

    np->tf->sp_usr = stack_top;

    np->tf->sp_usr -= sizeof(uint);
    if (np->tf->sp_usr % sizeof(uint) != 0)
        np->tf->sp_usr -= np->tf->sp_usr % sizeof(uint);

    if (copyout(mt->pgdir, np->tf->sp_usr, (char *)&arg, sizeof(arg)) != 0) {
        deallocuvm(mt->pgdir, mt->sz, mt->sz - PGSIZE);
        mt->sz -= PGSIZE;

        for (int i = 0; i < NOFILE; i++)
            if (np->ofile[i]) { fileclose(np->ofile[i]); np->ofile[i] = 0; }

        if (np->cwd) { iput(np->cwd); np->cwd = 0; }

        if (np->kstack) { free_page(np->kstack); np->kstack = 0; }

        np->state = UNUSED;
        return -1;
    }

    np->tf->pc = (uint)start;
    np->tf->r0 = (uint)arg;
    np->tf->lr_usr = 0;

    pid = np->pid;
    if (tidptr) {
        if (copyout(mt->pgdir, (uint)tidptr, (char*)&pid, sizeof(pid)) != 0) {
            deallocuvm(mt->pgdir, mt->sz, mt->sz - PGSIZE);
            mt->sz -= PGSIZE;
            np->state = UNUSED;
            return -1;
        }
    }

    acquire(&ptable.lock);
    np->next_thread = mt->next_thread;//set np's next thread to main threads next thread
    mt->next_thread = np; // update main threads next thread
    np->state = RUNNABLE;
    release(&ptable.lock);

    return pid;
}

void thread_exit(void *retval) {
    struct proc *cur = myproc();

    if (!IS_THREAD(cur)) {
        return;
    }

    acquire(&ptable.lock);
    cur->retval = retval;
    cur->state = ZOMBIE;
    wakeup1(cur);                 // thread_join() sleeps on the thread
    wakeup1(cur->main_thread);    // reap_threads() sleeps on the main thread
    sched();
    panic("thread_exit: sched returned");
}


