#include "types.h"
#include "stat.h"
#include "user.h"
#include "uproc.h"

#define MAXPROCS 64

char *states[] = {
  [0] "UNUSED", [1] "EMBRYO", [2] "SLEEPING",
  [3] "RUNNABLE", [4] "RUNNING", [5] "ZOMBIE"
};

int main(int argc, char *argv[]) {
  struct uproc table[MAXPROCS];
  int n, i;

  n = getprocs(table, MAXPROCS);

  printf(1, "PID\tPPID\tSTATE\t\tSYSCALLS\tNAME\n");
  for(i = 0; i < n; i++) {
    printf(1, "%d\t%d\t%s \t%d\t\t%s\n",
      table[i].pid,
      table[i].ppid,
      states[table[i].state],
      table[i].syscalls,
      table[i].name);
  }

  exit();
}
