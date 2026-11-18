#include "types.h"
#include "stat.h"
#include "user.h"

int
main(void)
{
  int p = fork();
  if(p == 0){
    // child: few tickets, but will sleep (gets boosts later)
    int me = getpid();
    settickets(me, 5);
    sleep(200);       // ~2 seconds, accumulates boosts
    // burn CPU a bit after waking
    volatile long x = 0;
    for(long i = 0; i < 200000000; i++) x += i;
    printf(1, "child done\n");
    exit();
  } else {
    // parent: more tickets, runs while child sleeps
    int me = getpid();
    settickets(me, 20);
    volatile long y = 0;
    for(long i = 0; i < 200000000; i++) y += i;
    printf(1, "parent mid\n");
    wait();
    printf(1, "parent done\n");
    exit();
  }
}
