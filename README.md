# 🖥️ xv6-ARM: Lottery Scheduler, Demand Paging, Threads and a Larger File System

[![Language: C](https://img.shields.io/badge/language-C%20%2B%20ARM%20assembly-blue.svg)](#-repository-structure)
[![ARMv6](https://img.shields.io/badge/CPU-ARMv6%20(ARM1176)-orange.svg)](#-system-architecture)
[![QEMU](https://img.shields.io/badge/runs%20on-QEMU%20versatilepb-lightgrey.svg)](#-quickstart)
[![usertests](https://img.shields.io/badge/xv6%20usertests-ALL%20PASSED-brightgreen.svg)](#-tests-and-results)
[![Parts](https://img.shields.io/badge/kernel%20extensions-5%20parts-informational.svg)](#-key-highlights)

An extended version of **xv6**, MIT's teaching Unix, running on the **ARM** architecture. On top of the base kernel it adds new system calls and shell features, a **lottery scheduler**, **demand paging**, **kernel threads with user-level synchronization**, and a file system with **doubly-indirect blocks** and **symbolic links**. The kernel passes the complete xv6 `usertests` regression suite.

---

## 🎓 Course & Project Information

- **Course:** CS 330: Operating Systems
- **Professor:** Abhishek Bichhawat
- **Institute:** Indian Institute of Technology Gandhinagar
- **Duration:** Aug 2025 – Nov 2025 (five programming assignments)
- **Author:** Kshitij Kasodkar (`kshitij.kasodkar@iitgn.ac.in`)

---

## 📚 Table of Contents

1. [Key Highlights](#-key-highlights)
2. [System Architecture](#-system-architecture)
3. [Part 1: System Calls and the Shell](#-part-1-system-calls-and-the-shell)
4. [Part 2: Lottery Scheduler](#-part-2-lottery-scheduler)
5. [Part 3: Page Tables and Demand Paging](#-part-3-page-tables-and-demand-paging)
6. [Part 4: Threads and Synchronization](#-part-4-threads-and-synchronization)
7. [Part 5: File System](#-part-5-file-system)
8. [Quickstart](#-quickstart)
9. [Tests and Results](#-tests-and-results)
10. [Repository Structure](#-repository-structure)
11. [Limitations & Future Work](#-limitations--future-work)
12. [References](#-references)

---

## 🌟 Key Highlights

- 🧰 **New system calls:** `getprocs`, `settickets`, `srand`, `getpinfo`, `printpt`, `thread_create` / `thread_join` / `thread_exit`, `barrier_init` / `barrier_check`, `getChannel` / `sleepChan` / `sigChan` / `sigOneChan`, `waitpid`, `symlink`.
- ⌨️ **Shell with Tab completion:** unique prefixes complete, ambiguous ones list the candidates; Backspace and Ctrl-U keep working across a completion, even when typing fast.
- 🎟️ **Lottery scheduling:** CPU share proportional to tickets, with **sleep compensation** (tickets doubled for as many lotteries as ticks slept) so interactive programs stay responsive.
- 🧠 **Demand paging on ARM:** `sbrk()` only reserves memory; the **data-abort handler** reads the ARM fault registers (DFSR, FAR) and maps each page on first touch. Invalid accesses kill only the faulting process, and kernel memory is not accessible from user mode.
- 🧵 **Kernel threads:** threads share one address space, files and heap, each with its own stack; spinlocks built on the ARM **LDREX/STREX** atomic instructions, condition variables, counting semaphores and an N-process barrier.
- 💾 **8 MB files:** doubly-indirect blocks raise the maximum file size from **70 KB to about 8 MB** (140 → 16,523 blocks).
- 🔗 **Symbolic links:** followed recursively by `open()`, with cycle detection.
- ✅ **Tested:** the full xv6 `usertests` suite passes, plus a test program for every feature.

---

## 🗺️ System Architecture

```
+--------------------------------------------------------------------------+
|  USER SPACE (usr/)                                                       |
|    sh (Tab completion)   ps  uptime  pause  pinfo   test programs        |
|    ulib.c: spinlocks (LDREX/STREX), condition variables, semaphores      |
+------------------------------------+-------------------------------------+
                                     |  SWI (system call)
+------------------------------------v-------------------------------------+
|  KERNEL                                                                  |
|    trap_asm.S / trap.c   SWI, IRQ (timer), data abort (page fault)       |
|    syscall.c            dispatch + per-process syscall count             |
|    proc.c               lottery scheduler, sleep boosts, threads,        |
|                         wait / waitpid, getprocs, getpinfo               |
|    sysproc.c            lazy sbrk, printpt, thread and channel syscalls  |
|    barrier.c            N-process barrier                                |
|    vm.c                 two-level ARM page tables, fork copy             |
|    fs.c / sysfile.c     doubly-indirect bmap / itrunc, symlink + open    |
|    console.c            line editing for Tab completion                  |
+------------------------------------+-------------------------------------+
                                     |
+------------------------------------v-------------------------------------+
|  HARDWARE (QEMU versatilepb)  ARM1176 CPU + MMU, timer, UART, RAM disk   |
+--------------------------------------------------------------------------+
```

---

## 🧰 Part 1: System Calls and the Shell

| Feature | How it works |
|---|---|
| `uptime` | Timer ticks since boot / 10 (the timer runs at 10 ticks per second) |
| `pause <seconds>` | `sleep(seconds × 10)`; prints usage without an argument |
| `ps` | New `getprocs(table, max)` system call copies every process (PID, parent PID, state, syscall count, name) to user space. `syscall()` increments a per-process counter on every system call |
| Tab completion | The console driver hands the partial line to the shell when Tab is pressed. The shell completes a unique match, lists all matches when there are several, or rings the bell. The console holds the echo of keys typed after Tab until the shell has redrawn the line, and passes Backspace / Ctrl-U to the shell for characters it already holds |

**Adding a system call** touches `syscall.h` (number), `syscall.c` (table), `sysproc.c` (implementation), `usr/usys.S` (user stub), `usr/user.h` (declaration) and `usr/Makefile`.

---

## 🎟️ Part 2: Lottery Scheduler

- **Lottery:** at every scheduling point the scheduler sums the tickets of all RUNNABLE processes, draws a random number in that range and runs the process holding that ticket.
- **Sleep compensation:** when a process wakes after being blocked for *x* ticks, its tickets count **double for the next *x* lotteries** it takes part in. Unused boosts accumulate.
- **Inheritance:** `fork()` copies the parent's tickets but not its boosts.
- **No false wakeups:** a process sleeping in `sleep(n)` becomes runnable only when its own `n` ticks have passed.
- **System calls:** `settickets(n)`, `srand(seed)`, `getpinfo(&pstat)` (tickets, times scheduled, remaining boosts), shown by `pinfo`.

```
$ lotterytest
Parent [31] starting (tickets=50)
Child [32] starting (tickets=5)
Parent [31] finished with count=1613180000
Child [32] finished with count=93580000
```

The 50-ticket parent gets far more CPU than the 5-ticket child; over many runs the ratio averages about 10:1.

---

## 🧠 Part 3: Page Tables and Demand Paging

**`printpt()`** walks the ARM two-level page table (4096 first-level entries of 1 MB, 256 second-level entries of 4 KB per table) and prints the first and last 10 user pages:

| VA | Contents | Flags |
|---|---|---|
| `0x0` | program text | `0x3e` user read/write |
| `0x1000` | data | `0x3e` |
| `0x2000` | stack guard page | `0x1e` kernel only |
| `0x3000` | user stack | `0x3e` |

**Demand paging:**

1. `sbrk(n)` only increases the process size; no memory is allocated.
2. The first access to a new page raises an ARM **data abort**.
3. The abort entry switches to SVC mode and builds a trap frame on the process's kernel stack.
4. `dabort_handler` reads the **fault status (DFSR)** and **fault address (FAR)** registers. For a translation fault inside the process it allocates a zeroed page, maps it, invalidates the TLB entry and returns; the faulting instruction is retried. This also works when the kernel touches such a page inside a system call.
5. Any other fault in user mode (outside the process, or kernel memory) kills only that process.
6. `fork()` copies only the pages a process has actually touched.

```
$ demandtest
Allocated 8 pages at 4000
Page table BEFORE touching ...      VA: 0x4000 -> Not Mapped   (sbrk only reserved it)
Touched page 0 at VA 4000 ... Touched page 7 at VA B000
Page table AFTER touching ...       VA: 0x4000 -> PA: 0x231e000, Flags: 0x3e
child: touching unowned VA 4C000, expect to be killed
pid 599 demandtest: segmentation fault at 0x4c000 (pc 0x1f4, fsr 0x807), killed
parent: child was killed, kernel still running
```

---

## 🧵 Part 4: Threads and Synchronization

| Component | Implementation |
|---|---|
| `thread_create(&tid, fn, arg)` | New kernel process that **shares the main thread's page table**, files and working directory; gets its own stack page; starts at `fn` with `arg` in `r0` |
| `thread_join(tid)` | Sleeps until the thread has finished, then frees its kernel stack and user stack |
| `thread_exit()` / `exit()` in a thread | Ends only that thread. A main thread that exits stops and reaps its threads first; `wait()` never reaps threads |
| Barrier | `barrier_init(n)`, `barrier_check()`: each caller sleeps until the n-th arrives, then all continue |
| Wait channels | `getChannel()`, `sleepChan(ch)`, `sigChan(ch)` (wake all), `sigOneChan(ch)` (wake one), with a per-channel "wakeup pending" flag so a signal sent just before a waiter sleeps is never lost |
| Spinlock | `acquireLock` / `releaseLock` on an atomic exchange built from ARM **LDREX/STREX** |
| Condition variable | `condWait(cv, lock)` releases the lock and sleeps on the channel; `signal` / `broadcast` |
| Semaphore | `semInit`, `semDown` (wait while the count is 0), `semUp` |
| `waitpid(pid)` | Waits for that child only; `-1` if it is not a child |

```
$ t_lock
Final value of x: 2000000

$ t_sem2
I am thread3 and I should print first
I am thread 1
Only one other thread has printed by now
I am thread 2
Both the other threads have printed by now

$ t_waitpid
return value of wrong waitpid -1
return value of correct waitpid 34
return value of wait -1
child reaped
```

---

## 💾 Part 5: File System

**Doubly-indirect blocks.** Each inode now holds 11 direct pointers, 1 single-indirect pointer and 1 doubly-indirect pointer:

| | Original xv6 | This kernel |
|---|---|---|
| Direct blocks | 12 | 11 |
| Single-indirect | 128 | 128 |
| Doubly-indirect | — | 128 × 128 = 16,384 |
| **Max file size** | 140 blocks = **70 KB** | 16,523 blocks ≈ **8 MB** |

`bmap()` maps file block *n* to a disk block, allocating blocks on demand (for *n* ≥ 139: index 1 = (*n* − 139) / 128, index 2 = (*n* − 139) % 128). `itrunc()` frees data blocks, second-level blocks and the doubly-indirect block when a file is deleted.

**Symbolic links.** `symlink(target, path)` creates a `T_SYMLINK` inode whose data is the target path. `open()` follows links recursively and fails after 10 hops (a cycle) or when the target does not exist.

```
$ symlinktest
OK:   symlink("target.txt", "link.txt")
OK:   open(link.txt) reads the target's data
OK:   write through link.txt is visible in target.txt
OK:   open(link2.txt) follows two links
OK:   open(dangling) fails
OK:   open(loop1) fails on the cycle loop1 -> loop2 -> loop1
symlinktest: ALL TESTS PASSED
```

---

## 🚀 Quickstart

### 1. Requirements
- `arm-none-eabi-gcc` (ARM cross compiler), `qemu-system-arm`, `make`

### 2. Ubuntu / WSL2 (Windows) 🐧
```bash
sudo apt update
sudo apt install -y gcc-arm-none-eabi qemu-system-arm make
```

### 3. macOS 🍎
```bash
brew install --cask gcc-arm-embedded
brew install qemu make
```

### 4. Build and run ▶️
```bash
make
make qemu        # quit QEMU with Ctrl-A, then X
```
You should see:
```
starting xv6 for ARM...
init: starting sh
$
```

---

## ✅ Tests and Results

| Command | Part | Expected result |
|---|---|---|
| `usertests` | all | `ALL TESTS PASSED` (the full xv6 regression suite) |
| `ps`, `uptime`, `pause 3`, Tab | 1 | process table, uptime in seconds, 3-second pause, completion |
| `lotterytest`, `pinfo` | 2 | 50-ticket process gets ~10× the CPU of a 5-ticket process |
| `pttest`, `demandtest` | 3 | pages mapped only on first touch; bad access kills only that process |
| `t_barrier` | 4 | all processes reach the barrier before any passes it |
| `t_threads`, `t_l_cv1`, `t_sem1` | 4 | `Value of x = 11` |
| `t_lock` | 4 | `2000000` (2 threads × 1,000,000 locked increments) |
| `t_l_cv2` | 4 | threads 1 and 2 strictly alternate |
| `t_sem2` | 4 | thread 3 first, then one thread at a time |
| `t_sleepwake`, `t_waitpid` | 4 | woken by the child; `-1`, child PID, `-1` |
| `bigfile` | 5 | blocks written and verified through the doubly-indirect region; all blocks freed on delete |
| `symlinktest` | 5 | `ALL TESTS PASSED` |

> 💡 The disk is a 512 KB RAM disk built into the kernel image, so `bigfile` writes 151 blocks (past the 140-block limit of the original xv6) rather than the full 8 MB. Every boot starts from a fresh copy of the disk.

---

## 📂 Repository Structure

| Path | What it is |
|---|---|
| `proc.c`, `proc.h` | processes, lottery scheduler, sleep boosts, threads, `wait` / `waitpid`, `getprocs` / `getpinfo` |
| `sysproc.c` | system calls: lazy `sbrk`, `settickets`, `printpt`, threads, wait channels, barrier |
| `trap.c`, `trap_asm.S` | system calls, timer interrupt, page-fault (data abort) handler |
| `vm.c`, `mmu.h` | ARM page tables, `fork` address-space copy |
| `barrier.c` | N-process barrier |
| `fs.c`, `fs.h`, `file.h` | file system with doubly-indirect blocks |
| `sysfile.c` | file system calls, `symlink`, link following in `open` |
| `console.c` | console driver and line editing |
| `usr/sh.c` | shell with Tab completion |
| `usr/ulib.c` | user library: spinlocks, condition variables, semaphores |
| `usr/` | commands and test programs (`ps`, `pinfo`, `lotterytest`, `pttest`, `demandtest`, `t_*`, `bigfile`, `symlinktest`, `usertests`) |
| `device/` | UART, timer and interrupt-controller drivers |
| `tools/mkfs.c` | builds the file-system image |

---

## 🚧 Limitations & Future Work

- Single CPU: the kernel spinlocks only disable interrupts, so the kernel is not multi-core.
- The RAM disk is 512 KB, so a full 8 MB file cannot be stored even though the inode supports it.
- User spinlocks spin until preempted; a futex-style sleep would save CPU under contention.
- No copy-on-write `fork()`; touched pages are copied eagerly.

---

## 📚 References

1. R. Cox, F. Kaashoek, R. Morris, *xv6: a simple, Unix-like teaching operating system*, MIT.
2. R. Arpaci-Dusseau, A. Arpaci-Dusseau, *Operating Systems: Three Easy Pieces* (lottery scheduling, paging, locks, condition variables, semaphores, file systems).
3. C. Waldspurger, W. Weihl, "Lottery Scheduling: Flexible Proportional-Share Resource Management," *OSDI*, 1994.
4. ARM Ltd., *ARM1176JZF-S Technical Reference Manual* (MMU, fault status and fault address registers).

---

## 📄 License

Built on **xv6**, Copyright (c) 2006-2018 Frans Kaashoek, Robert Morris, Russ Cox, Massachusetts Institute of Technology, released under the MIT License (see [`LICENSE`](LICENSE)), using the ARM port of xv6 provided as the course's starting code. The extensions described above were added on top of it for CS 330.
