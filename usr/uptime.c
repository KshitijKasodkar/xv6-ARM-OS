#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  int ticks = uptime();
  printf(1,"Uptime: %d seconds\n", ticks/10);
  exit();
}
