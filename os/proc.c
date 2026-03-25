#include "proc.h"
#include "defs.h"
#include "loader.h"
#include "trap.h"
#include "vm.h"
#include "timer.h"

struct proc pool[NPROC];
__attribute__((aligned(16))) char kstack[NPROC][PAGE_SIZE];
__attribute__((aligned(4096))) char trapframe[NPROC][TRAP_PAGE_SIZE];

extern char boot_stack_top[];
extern pagetable_t kernel_pagetable;
struct proc *current_proc;
struct proc idle;

int threadid() { return curr_proc()->pid; }
struct proc *curr_proc() { return current_proc; }

void proc_init(void)
{
    struct proc *p;
    for (p = pool; p < &pool[NPROC]; p++) {
        p->state    = UNUSED;
        p->kstack   = (uint64)kstack[p - pool];
        p->trapframe = (struct trapframe *)trapframe[p - pool];
        // LAB1
        p->started    = 0;
        p->start_cycle = 0;
        memset(p->syscall_times, 0, sizeof(p->syscall_times));
    }
    idle.kstack    = (uint64)boot_stack_top;
    idle.pid       = 0;
    idle.pagetable = kernel_pagetable;
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
    p->pid      = allocpid();
    p->state    = USED;
    p->pagetable = 0;
    p->ustack   = 0;
    p->max_page = 0;
    // LAB1
    p->started    = 0;
    p->start_cycle = 0;
    memset(p->syscall_times, 0, sizeof(p->syscall_times));
    memset(&p->context,  0, sizeof(p->context));
    memset((void *)p->kstack,   0, KSTACK_SIZE);
    memset((void *)p->trapframe, 0, TRAP_PAGE_SIZE);
    p->context.ra = (uint64)usertrapret;
    p->context.sp = p->kstack + KSTACK_SIZE;
    return p;
}

void scheduler(void)
{
    struct proc *p;
    for (;;) {
        for (p = pool; p < &pool[NPROC]; p++) {
            if (p->state == RUNNABLE) {
                // LAB1
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

void freeproc(struct proc *p)
{
    p->state = UNUSED;
    // uvmfree(p->pagetable, p->max_page);
}

void exit(int code)
{
    struct proc *p = curr_proc();
    infof("proc %d exit with %d", p->pid, code);
    freeproc(p);
    finished();
    sched();
}