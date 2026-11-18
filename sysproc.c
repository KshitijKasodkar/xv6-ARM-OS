#include "types.h"
#include "param.h"
#include "defs.h"
#include "memlayout.h"
#include "arm.h"        // or "x86.h" if x86 build
#include "mmu.h"
#include "proc.h"       // <-- Needed for struct proc, myproc()
#include "spinlock.h"
#include "uproc.h"      // <-- If your tree defines struct uproc separately
#include "pstat.h"
#include "barrier.h"

#define USER_CHAN_BASE 4

static struct spinlock userchanlock;
static int next_user_chan = USER_CHAN_BASE;   // monotonic, never reused

// One "wakeup pending" flag per user channel. condWait() releases the user
// lock and then calls sleepChan(); a signal that lands between those two steps
// would otherwise be lost and the waiter would sleep forever. sigChan() and
// sigOneChan() set the flag, and sleepChan() returns at once if it is set.
// (A rare extra wakeup is harmless: waiters re-check their condition.)
#define NUCHAN_PENDING 128
static char uchan_pending[NUCHAN_PENDING];
#define UCHAN_PENDING(ch) uchan_pending[((uint)(ch) - USER_CHAN_BASE) % NUCHAN_PENDING]

#define PGSIZE 4096


extern struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;


int sys_fork(void)
{
    return fork();
}

int sys_exit(void)
{
    exit();
    return 0;  // not reached
}

int sys_wait(void)
{
    return wait();
}

int sys_kill(void)
{
    int pid;

    if(argint(0, &pid) < 0) {
        return -1;
    }

    return kill(pid);
}

int sys_getpid(void)
{
    return proc->pid;
}

int sys_sbrk(void)
{
    int addr;
    int n;

    if(argint(0, &n) < 0) {
        return -1;
    }

    // all threads share the main thread's address space
    struct proc *mt = (!proc->is_main && proc->main_thread) ? proc->main_thread : proc;

    addr = mt->sz;

    if(n > 0) {
        // Demand paging: only grow the size here. Each page is allocated and
        // mapped by the page-fault handler (dabort_handler) on first touch.
        if((uint)addr + n < (uint)addr || (uint)addr + n > UADDR_SZ) {
            return -1;
        }

        mt->sz = addr + n;
        proc->sz = mt->sz;

    } else if(growproc(n) < 0) {
        return -1;
    }

    return addr;
}

int sys_sleep(void)
{
  int n;
  if (argint(0, &n) < 0)
    return -1;

  acquire(&tickslock);
  uint start = ticks;
  release(&tickslock);

  acquire(&ptable.lock);
  proc->sleep_start = start;
  proc->sleep_ticks = n;
  proc->chan = &ticks;
  proc->state = SLEEPING;
  sched();
  release(&ptable.lock);

  return 0;
}




// return how many clock tick interrupts have occurred
// since start.
int sys_uptime(void)
{
    uint xticks;

    acquire(&tickslock);
    xticks = ticks;
    release(&tickslock);

    return xticks;
}


// ADDED ---------------------------------------------

int sys_getprocs(void) {
  struct uproc *table;
  int max;

  if(argptr(0, (void*)&table, sizeof(struct uproc)) < 0 || argint(1, &max) < 0)
    return -1;

  return getprocs(table, max);
}

int sys_settickets(void) {
    int pid, n;
    if(argint(0, &pid) < 0) return -1;
    if(argint(1, &n) < 0) return -1;
    return settickets_kernel(pid, n);
}


extern uint rseed; // declared in proc.c

int sys_srand(void) {
    int seed;
    if(argint(0, &seed) < 0) return -1;
    rseed = (uint) seed;
    return 0;
}


extern int getpinfo(struct pstat *);

int
sys_getpinfo(void)
{
    struct pstat *ps;
    if(argptr(0, (void*)&ps, sizeof(*ps)) < 0)
        return -1;
    return getpinfo(ps);
}


//// New code goes here    ////
extern int barrier_init(int n);
extern int barrier_check(void);

int
sys_barrier_init(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  return barrier_init(n);
}

int
sys_barrier_check(void)
{
  return barrier_check();
}


//// New code (Given in xv6)////

int sys_thread_create(void) {
    uint *tidptr_u;
    uint start_u;
    uint arg_u;


    if(argptr(0, (char**)&tidptr_u, sizeof(uint)) < 0)
        return -1;


    if(argint(1, (int*)&start_u) < 0)
        return -1;
    if(argint(2, (int*)&arg_u) < 0)
        return -1;



    return thread_create_kernel(myproc(), tidptr_u, (void*)start_u, (void*)arg_u);
}




int sys_thread_exit(void) {
    int retval_int;
    if(argint(0, &retval_int) < 0) retval_int = 0;
    thread_exit((void*)retval_int);
    return 0;
}

int sys_thread_join(void) {
    int tid;
    struct proc *p, *cur = myproc(), *mt;
    if (argint(0, &tid) < 0) return -1;
    mt = cur->is_main ? cur : cur->main_thread;
    if (!mt) return -1;

    acquire(&ptable.lock);
    for (;;) {
        int found = 0;
        for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
            if (p->pid != tid) continue;
            found = 1;
            if (p->main_thread != mt) { release(&ptable.lock); return -1; }


            if (p->state == ZOMBIE) {
                int pid = p->pid;

                if (p->kstack) { free_page(p->kstack); p->kstack = 0; }

                if (p->ustack) {
                    if (p->ustack + PGSIZE == mt->sz) {
                        mt->sz = deallocuvm(mt->pgdir, mt->sz, mt->sz - PGSIZE);
                    }
                    else {
                        free_user_page(mt->pgdir, p->ustack);
                    }
                    p->ustack = 0;
                }



                if (mt->next_thread == p) {
                    mt->next_thread = p->next_thread;
                } else {
                    struct proc *t;
                    for (t = mt->next_thread; t && t->next_thread; t = t->next_thread) {
                        if (t->next_thread == p) {
                            t->next_thread = p->next_thread;
                            break;
                        }
                    }
                }
                p->next_thread = 0;

                // detach the thread so nobody else can reap it, then close
                // its files without holding ptable.lock (closing may sleep)
                p->main_thread = 0;
                p->parent = 0;
                release(&ptable.lock);

                for (int i = 0; i < NOFILE; i++) if (p->ofile[i]) { fileclose(p->ofile[i]); p->ofile[i] = 0; }
                if (p->cwd) { iput(p->cwd); p->cwd = 0; }

                acquire(&ptable.lock);
                p->pgdir = 0;        // the shared page table stays with the main thread
                p->state = UNUSED;
                p->pid = 0;
                p->name[0] = 0;
                p->killed = 0;
                release(&ptable.lock);
                return pid;
            }

            sleep(p, &ptable.lock);
            break;
        }

        if (!found) { release(&ptable.lock); return -1; }
        if (cur->killed) { release(&ptable.lock); return -1; }

    }
}

int sys_waitpid(void)
{
    int pid;

    if(argint(0, &pid) < 0) {
        return -1;
    }

    return waitpid(pid);
}

void userchaninit(void){
    //initialize the userspace channel allocator for condition variables
    initlock(&userchanlock, "userchan");
    next_user_chan= USER_CHAN_BASE; 
}

static int valid(int ch){
    return ch >= USER_CHAN_BASE; //to avoid collisions with kernel
}

int sys_sleepChan(void) {
//   return -1;
    //get channel argument from user, validate and acquire process table lock and sleep on channel
    int ch;
    if(argint(0, &ch)< 0){
        return -1;
    }
    if(!valid(ch)){
        return -1;
    }
    acquire(&ptable.lock);
    // cprintf("sleepChan(%d) pid=%d\n", ch, proc->pid);
    if(UCHAN_PENDING(ch)){
        // a signal arrived just before we got here: do not sleep
        UCHAN_PENDING(ch) = 0;
        release(&ptable.lock);
        return 0;
    }
    sleep((void*)ch, &ptable.lock);
    release(&ptable.lock);
    return 0;
}

int sys_getChannel(void) {
//   return -1;
    //channel allocation with a spinlock
    int ch;
    acquire(&userchanlock);
    ch= next_user_chan++;
    release(&userchanlock);
    return ch;
}

int sys_sigChan(void) {
//   return -1;
    //wake up all processes sleeping on a user-space channel
    int ch;
    if(argint(0, &ch) < 0){
        return -1;
    }
    if(!valid(ch)){
        return -1;
    }
    acquire(&ptable.lock);
    UCHAN_PENDING(ch) = 1;
    release(&ptable.lock);
    wakeup((void*)ch);
    //   cprintf("sigChan(%d) pid=%d\n", ch, proc->pid);
    return 0;
}

int sys_sigOneChan(void) {
    //wake up exactly one process sleeping on a user-space channel
    int ch;
    if(argint(0, &ch) < 0){
        return -1;
    }
    if(!valid(ch)){
        return -1;
    }

    acquire(&ptable.lock);
    UCHAN_PENDING(ch) = 1;
    struct proc *p;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->state == SLEEPING && p->chan == (void*)ch){
            p->state = RUNNABLE;
            break;
        }
    }
    release(&ptable.lock);
    return 0;
}
