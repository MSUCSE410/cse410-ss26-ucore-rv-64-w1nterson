#ifndef PROC_H
#define PROC_H

#include "types.h"
#include "riscv.h"

#define NPROC (16)
#define MAX_SYSCALL_NUM 500

typedef enum {
    TaskStatusUnInit = 0,
    TaskStatusReady,
    TaskStatusRunning,
    TaskStatusExited,
} TaskStatus;

typedef struct {
    TaskStatus status;
    unsigned int syscall_times[MAX_SYSCALL_NUM];
    int time;
} TaskInfo;

struct context {
    uint64 ra;
    uint64 sp;
    uint64 s0;  uint64 s1;  uint64 s2;  uint64 s3;
    uint64 s4;  uint64 s5;  uint64 s6;  uint64 s7;
    uint64 s8;  uint64 s9;  uint64 s10; uint64 s11;
};

enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

struct proc {
    enum procstate state;
    int pid;
    uint64 ustack;               // virtual address of user stack bottom
    uint64 kstack;               // virtual address of kernel stack
    struct trapframe *trapframe; // data page for trampoline.S
    struct context context;
    pagetable_t pagetable;       // user page table  <-- ch4 addition
    uint64 max_page;             // highest page number mapped  <-- ch4 addition
    // task info
    uint64 start_cycle;
    unsigned int syscall_times[MAX_SYSCALL_NUM];
    int started;
};

struct proc *curr_proc();
void exit(int);
void proc_init();
void scheduler() __attribute__((noreturn));
void sched();
void yield();
struct proc *allocproc();
void swtch(struct context *, struct context *);

#endif // PROC_H