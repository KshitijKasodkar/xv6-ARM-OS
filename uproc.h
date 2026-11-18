// uproc.h
#ifndef UPROC_H
#define UPROC_H

// process info structure
struct uproc {
  int pid;
  int ppid;
  char name[16];
  int state;
  int syscalls;
};

#endif
