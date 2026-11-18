#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  printf(1, "--- Starting Page Table Test ---\n");
  
  // This is where we call our new system call
  printpt(); 
  
  printf(1, "--- Page Table Test Complete ---\n");
  
  exit();
}