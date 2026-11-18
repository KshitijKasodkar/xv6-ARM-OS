#include "types.h"
#include "stat.h"
#include "user.h"

#define RUNTIME 100   // shorter runtime so counts stay in int range

void work(int *count) {
  int i;
  for (i = 0; i < 10000; i++) {   // reduced workload
    (*count)++;
  }
}

int
main(void)
{
  int pid;

  printf(1, "Parent [%d] starting (tickets=50)\n", getpid());
  settickets(getpid(), 50);

  pid = fork();
  if (pid < 0) {
    printf(1, "Fork failed!\n");
    exit();
  }

  if (pid == 0) {
    // child
    int count = 0;
    int start = uptime();
    printf(1, "Child [%d] starting (tickets=5)\n", getpid());
    settickets(getpid(), 5);

    while (uptime() - start < RUNTIME) {
      work(&count);
    }

    printf(1, "Child [%d] finished with count=%d\n", getpid(), count);
    exit();
  } else {
    // parent
    int count = 0;
    int start = uptime();

    while (uptime() - start < RUNTIME) {
      work(&count);
    }

    printf(1, "Parent [%d] finished with count=%d\n", getpid(), count);
    wait();
  }

  exit();
}
