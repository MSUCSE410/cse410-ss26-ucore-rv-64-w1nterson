#include "proc.h"
#include "defs.h"
#include "loader.h"
#include "trap.h"
#include "timer.h"
#include "vm.h"

struct proc pool[NPROC];
char kstack[NPROC][PAGE_SIZE];
__attribute__((aligned(4096))) char ustack[NPROC][PAGE_SIZE];
__attribute__((aligned(4096))) char trapframe[NPROC][PAGE_SIZE];

extern char boot_stack_top[];
struct proc *current_proc;
struct proc idle;

int threadid()
{
    return curr_proc()->pid;
}

struct proc *curr_proc()
{
    return current_proc;
}

extern pagetable_t kernel_pagetable;  // defined in vm.c

void proc_init(void)
{
    struct proc *p;
    for (p = pool; p < &pool[NPROC]; p++) {
        p->state       = UNUSED;
        p->kstack      = (uint64)kstack[p - pool];
        p->ustack      = (uint64)ustack[p - pool];
        p->trapframe   = (struct trapframe *)trapframe[p - pool];
        p->pagetable   = 0;
        p->max_page    = 0;
        p->started     = 0;
        p->start_cycle = 0;
        memset(p->syscall_times, 0, sizeof(p->syscall_times));
    }
    idle.kstack    = (uint64)boot_stack_top;
    idle.pid       = 0;
    idle.pagetable = kernel_pagetable;  // <-- critical: idle needs a valid pagetable
    current_proc   = &idle;
}
int allocpid()
{
    static int PID = 1;
    return PID++;
}

struct proc *allocproc(void)
{
    struct proc *p;
    for (p = pool; p < &pool[NPROC]; p++) {
        if (p->state == UNUSED)
            goto found;
    }
    return 0;

found:
    p->pid        = allocpid();
    p->state      = USED;
    p->pagetable  = 0;   // ch4: will be set by bin_loader
    p->max_page   = 0;   // ch4: will be set by bin_loader
    p->started    = 0;
    p->start_cycle = 0;
    memset(p->syscall_times, 0, sizeof(p->syscall_times));
    memset(&p->context,   0, sizeof(p->context));
    memset(p->trapframe,  0, PAGE_SIZE);
    memset((void *)p->kstack, 0, PAGE_SIZE);
    p->context.ra = (uint64)usertrapret;
    p->context.sp = p->kstack + PAGE_SIZE;
    return p;
}

void scheduler(void)
{
    struct proc *p;
    for (;;) {
        for (p = pool; p < &pool[NPROC]; p++) {
            if (p->state == RUNNABLE) {
                if (!p->started) {
                    p->start_cycle = get_cycle();
                    p->started = 1;
                }
                p->state     = RUNNING;
                current_proc = p;
                swtch(&idle.context, &p->context);
            }
        }
    }
}

void sched(void)
{
    struct proc *p = curr_proc();
    if (p->state == RUNNING)
        panic("sched running");
    swtch(&p->context, &idle.context);
}

void yield(void)
{
    current_proc->state = RUNNABLE;
    sched();
}

void exit(int code)
{
    struct proc *p = curr_proc();
    infof("proc %d exit with %d", p->pid, code);

    if (p->pagetable && p->max_page > 0) {
        uvmunmap(p->pagetable, TRAMPOLINE, 1, 0);  // don't free, kernel code
        uvmunmap(p->pagetable, TRAPFRAME,  1, 0);  // don't free, static array

        // All user pages (binary + stack) were kalloc'd, safe to free
        uint64 base_page = BASE_ADDRESS / PGSIZE;
        uint64 npages    = p->max_page - base_page;
        uvmunmap(p->pagetable, BASE_ADDRESS, npages, 1);

        kfree(p->pagetable);
        p->pagetable = 0;
        p->max_page  = 0;
    }

    p->state = UNUSED;
    finished();
    sched();
}