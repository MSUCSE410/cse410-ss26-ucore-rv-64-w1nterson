#include "loader.h"
#include "defs.h"
#include "trap.h"
#include "vm.h"

static uint64 app_num;
static uint64 *app_info_ptr;
extern char _app_num[], ekernel[];
extern char trampoline[]; // defined in trampoline.S

int finished()
{
    static int fin = 0;
    if (++fin >= app_num)
        panic("all apps over");
    return 0;
}

void loader_init()
{
	/*
    if ((uint64)ekernel >= BASE_ADDRESS) {
        panic("kernel too large...\n");
    }
	*/
    app_info_ptr = (uint64 *)_app_num;
    app_num = *app_info_ptr;
    app_info_ptr++;
}

pagetable_t bin_loader(uint64 start, uint64 end, struct proc *p)
{
    pagetable_t pg = (pagetable_t)kalloc();
    if (pg == 0) {
        errorf("bin_loader: kalloc failed for root page table");
        return 0;
    }
    memset(pg, 0, PGSIZE);

    // Map trampoline at top of virtual address space (needed for trap entry/exit)
    if (mappages(pg, TRAMPOLINE, PGSIZE, (uint64)trampoline, PTE_R | PTE_X) < 0) {
        kfree(pg);
        errorf("bin_loader: failed to map trampoline");
        return 0;
    }

    // Map trapframe just below trampoline
    if (mappages(pg, TRAPFRAME, PGSIZE, (uint64)p->trapframe, PTE_R | PTE_W) < 0) {
        panic("bin_loader: failed to map trapframe");
    }

    // Round end up to page boundary
    end = PGROUNDUP(end);

    if (!PGALIGNED(start)) {
        panic("user program not aligned, start = %p", start);
    }

    // Map app binary: physical [start, end) -> virtual [BASE_ADDRESS, BASE_ADDRESS+length)
    uint64 length = end - start;
    if (mappages(pg, BASE_ADDRESS, length, start,
                 PTE_U | PTE_R | PTE_W | PTE_X) != 0) {
        panic("bin_loader: failed to map user binary");
    }

    p->pagetable = pg;

    // Guard page gap (one PGSIZE), then user stack
    uint64 ustack_bottom = BASE_ADDRESS + length + PGSIZE;
    void *stack_mem = kalloc();
    if (stack_mem == 0) {
        panic("bin_loader: kalloc failed for user stack");
    }
    if (mappages(pg, ustack_bottom, USTACK_SIZE, (uint64)stack_mem,
                 PTE_U | PTE_R | PTE_W | PTE_X) != 0) {
        panic("bin_loader: failed to map user stack");
    }
    p->ustack = ustack_bottom;

    // Set entry point and initial stack pointer
    p->trapframe->epc = BASE_ADDRESS;
    p->trapframe->sp  = p->ustack + USTACK_SIZE;

    // Track highest page mapped (used by exit cleanup)
    p->max_page = PGROUNDUP(p->ustack + USTACK_SIZE - 1) / PGSIZE;

    return pg;
}

int run_all_app()
{
    for (int i = 0; i < app_num; ++i) {
        struct proc *p = allocproc();

        uint64 start = app_info_ptr[i];
        uint64 end   = app_info_ptr[i + 1];

        if (bin_loader(start, end, p) == 0) {
            panic("run_all_app: bin_loader failed for app %d", i);
        }

        tracef("load app %d [%p, %p)", i, start, end);

        p->state      = RUNNABLE;
        p->started    = 0;
        p->start_cycle = 0;
        memset(p->syscall_times, 0, sizeof(p->syscall_times));
    }
    return 0;
}