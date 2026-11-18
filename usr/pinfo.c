#include "types.h"
#include "stat.h"
#include "user.h"
#include "pstat.h"

int
main(int argc, char *argv[])
{
  struct pstat ps;
  if (getpinfo(&ps) < 0) {
    printf(1, "getpinfo failed\n");
    exit();
  }

  printf(1, "PID inuse tickets runticks boostsleft\n");
  for (int i = 0; i < NPROC; i++) {
    if (ps.inuse[i]) {
      printf(1, "%d    %d     %d       %d        %d\n",
             ps.pid[i], ps.inuse[i], ps.tickets[i],
             ps.runticks[i], ps.boostsleft[i]);
    }
  }
  exit();
}
