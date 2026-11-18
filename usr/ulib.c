#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "user.h"

char*
strcpy(char *s, char *t)
{
    char *os;

    os = s;
    while((*s++ = *t++) != 0)
        ;
    return os;
}

int
strcmp(const char *p, const char *q)
{
    while(*p && *p == *q)
        p++, q++;
    return (uchar)*p - (uchar)*q;
}

int
strncmp(const char *p, const char *q, uint n)     //ADDED
{
  while(n > 0 && *p && *p == *q){
    n--;
    p++;
    q++;
  }

  if(n == 0)
    return 0;
  return (uchar)*p - (uchar)*q;
}

uint
strlen(char *s)
{
    int n;

    for(n = 0; s[n]; n++)
        ;
    return n;
}

void*
memset(void *dst, int v, uint n)
{
	uint8	*p;
	uint8	c;
	uint32	val;
	uint32	*p4;

	p   = dst;
	c   = v & 0xff;
	val = (c << 24) | (c << 16) | (c << 8) | c;

	// set bytes before whole uint32
	for (; (n > 0) && ((uint)p % 4); n--, p++){
		*p = c;
	}

	// set memory 4 bytes a time
	p4 = (uint*)p;

	for (; n >= 4; n -= 4, p4++) {
		*p4 = val;
	}

	// set leftover one byte a time
	p = (uint8*)p4;

	for (; n > 0; n--, p++) {
		*p = c;
	}

	return dst;
}

char*
strchr(const char *s, char c)
{
    for(; *s; s++)
        if(*s == c)
            return (char*)s;
    return 0;
}

char*
gets(char *buf, int max)
{
    int i, cc;
    char c;

    for(i=0; i+1 < max; ){
        cc = read(0, &c, 1);
        if(cc < 1)
            break;
        buf[i++] = c;
        if(c == '\n' || c == '\r')
            break;
    }
    buf[i] = '\0';
    return buf;
}

int
stat(char *n, struct stat *st)
{
    int fd;
    int r;

    fd = open(n, O_RDONLY);
    if(fd < 0)
        return -1;
    r = fstat(fd, st);
    close(fd);
    return r;
}

int
atoi(const char *s)
{
    int n;

    n = 0;
    while('0' <= *s && *s <= '9')
        n = n*10 + *s++ - '0';
    return n;
}

void*
memmove(void *vdst, void *vsrc, int n)
{
    char *dst, *src;

    dst = vdst;
    src = vsrc;
    while(n-- > 0)
        *dst++ = *src++;
    return vdst;
}

void*
memcpy(void *vdst, const void *vsrc, unsigned int n)
{
  return memmove(vdst, (void*)vsrc, n);
}


/// Copied from given files//

// ---------------------------------------------------------------------------
// User-level synchronization for threads (thread_create).
//
// lock:      spinlock on an atomic exchange built from the ARMv6 exclusive
//            load/store pair (LDREX/STREX), so it is safe against preemption.
// condvar:   a kernel wait channel from getChannel(); condWait() releases the
//            lock and sleeps on the channel, broadcast()/signal() wake the
//            sleepers (sigChan / sigOneChan). The kernel keeps a "wakeup
//            pending" flag per channel so a signal sent between the release
//            and the sleep is never lost.
// semaphore: a counter protected by a lock, with a condvar to wait on.
// ---------------------------------------------------------------------------

// atomically store newval in *addr and return the old value
static int
xchg_int(volatile int *addr, int newval)
{
    int old, failed;

    do {
        asm volatile("ldrex %0, [%2]\n\t"
                     "strex %1, %3, [%2]"
                     : "=&r"(old), "=&r"(failed)
                     : "r"(addr), "r"(newval)
                     : "memory");
    } while (failed);

    return old;
}

void initiateLock(struct lock* l) {
    l->lockvar = 0;
    l->isInitiated = 1;
}

void acquireLock(struct lock* l) {
    while (xchg_int(&l->lockvar, 1) != 0)
        ;
}

void releaseLock(struct lock* l) {
    xchg_int(&l->lockvar, 0);
}

void initiateCondVar(struct condvar* cv) {
    cv->var = getChannel();
    cv->isInitiated = 1;
}

// caller must hold l; returns with l held again
void condWait(struct condvar* cv, struct lock* l) {
    releaseLock(l);
    sleepChan(cv->var);
    acquireLock(l);
}

void broadcast(struct condvar* cv) {
    sigChan(cv->var);
}

void signal(struct condvar* cv) {
    sigOneChan(cv->var);
}

void semInit(struct semaphore* s, int initVal) {
    s->ctr = initVal;
    initiateLock(&s->l);
    initiateCondVar(&s->cv);
    s->isInitiated = 1;
}

void semUp(struct semaphore* s) {
    acquireLock(&s->l);
    s->ctr++;
    signal(&s->cv);
    releaseLock(&s->l);
}

void semDown(struct semaphore* s) {
    acquireLock(&s->l);
    while (s->ctr <= 0)
        condWait(&s->cv, &s->l);
    s->ctr--;
    releaseLock(&s->l);
}
