// System call numbers
#define SYS_fork    1
#define SYS_exit    2
#define SYS_wait    3
#define SYS_pipe    4
#define SYS_read    5
#define SYS_kill    6
#define SYS_exec    7
#define SYS_fstat   8
#define SYS_chdir   9
#define SYS_dup    10
#define SYS_getpid 11
#define SYS_sbrk   12
#define SYS_sleep  13
#define SYS_uptime 14
#define SYS_open   15
#define SYS_write  16
#define SYS_mknod  17
#define SYS_unlink 18
#define SYS_link   19
#define SYS_mkdir  20
#define SYS_close  21
#define SYS_getprocs 22   //ADDED
#define SYS_settickets 23 // Add this line

#define SYS_srand       24
#define SYS_getpinfo    25

#define SYS_printpt    26 // Add this line

///////////////// New syscalls ///////////////


#define SYS_thread_create 27
#define SYS_thread_exit   28
#define SYS_thread_join   29
#define SYS_waitpid             30
#define SYS_barrier_init        31
#define SYS_barrier_check       32
#define SYS_sleepChan           33
#define SYS_getChannel          34
#define SYS_sigChan             35
#define SYS_sigOneChan          36
#define SYS_symlink             37