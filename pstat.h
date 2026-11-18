// pstat.h
#ifndef _PSTAT_H_
#define _PSTAT_H_

#include "param.h"   // for NPROC

struct pstat {
    int inuse[NPROC];      // whether this slot of the process table is in use
    int tickets[NPROC];    // number of tickets this process has
    int pid[NPROC];        // process ID
    int runticks[NPROC];   // number of times the process has been scheduled
    int boostsleft[NPROC];// number of boosts left
};

#endif // _PSTAT_H_
