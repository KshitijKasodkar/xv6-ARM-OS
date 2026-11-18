/*----------xv6 sync lab----------*/
#include "types.h"
#include "arm.h"
#include "spinlock.h"
#include "defs.h"
#include "barrier.h"

//define any variables needed here

static struct spinlock barrier_lock;
static int barrier_count = 0;
static int barrier_n = 0;
/* channel object — just take address of this variable as the channel */
static char barrier_chan;

int
barrier_init(int n)
{
  if(n <= 0)
    return -1;

  initlock(&barrier_lock, "barrier_lock");
  acquire(&barrier_lock);
  barrier_n = n;
  barrier_count = 0;
  /* release lock and return success */
  release(&barrier_lock);
  return 0;
}

int
barrier_check(void)
{
  acquire(&barrier_lock);

  barrier_count++;

  if(barrier_count < barrier_n) {
    /* Not enough processes yet: sleep on the channel.
       sleep will release the lock atomically and re-acquire on wakeup. */
    sleep((void*)&barrier_chan, &barrier_lock);
    /* after waking up, we hold the lock again */
    release(&barrier_lock);
    return 0;
  } else {
    /* This is the N-th process: wake all waiting processes. */
    wakeup((void*)&barrier_chan);
    /* We can release the lock and let everyone continue.*/
    release(&barrier_lock);
    return 0;
  }
}
/*----------xv6 sync lock end----------*/
