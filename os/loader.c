#include "loader.h"
#include "defs.h"
#include "trap.h"
#include "vm.h"

static uint64 app_num;
static uint64 *app_info_ptr;
extern char _app_num[], ekernel[];
extern char trampoline[];

int finished()
{
    static int fin = 0;
    if (++fin >= app_num)
        panic("all apps over");
    return 0;
}

void loader_init()
{
    app_info_ptr = (uint64 *)_app_num;
    app_num = *app_info_ptr;
    app_info_ptr++;
}

pagetable_t bin_loader(uint64 start, uint64 end, struct proc *p)
{
    pagetable_t pg = (pagetable_t)kalloc();
    if (pg == 0)
        panic("bin_loader: kalloc failed");
    memset(pg, 0, PGSIZE);

    // Map trampoline (kernel code, no free)
    if (mappages(pg, TRAMPOLINE, PGSIZE, (uint64)trampoline, PTE_R | PTE_X) < 0)
        panic("bin_loader: map trampoline failed");

    // Map trapframe (static array, no free)
    if (mappages(pg, TRAPFRAME, PGSIZE, (uint64)p->trapframe, PTE_R | PTE_W) < 0)
        panic("bin_loader: map trapframe failed");

    // Copy app binary into fresh kalloc'd pages
    uint64 file_size = end - start;
    uint64 map_size  = PGROUNDUP(file_size < MAX_APP_SIZE ? MAX_APP_SIZE : file_size);

    for (uint64 offset = 0; offset < map_size; offset += PGSIZE) {
        void *mem = kalloc();
        if (mem == 0)
            panic("bin_loader: out of memory");
        memset(mem, 0, PGSIZE);
        if (offset < file_size) {
            uint64 n = file_size - offset;
            if (n > PGSIZE) n = PGSIZE;
            memmove(mem, (void *)(start + offset), n);
        }
        if (mappages(pg, BASE_ADDRESS + offset, PGSIZE, (uint64)mem,
                     PTE_U | PTE_R | PTE_W | PTE_X) != 0)
            panic("bin_loader: mappages failed");
    }

    p->pagetable = pg;

    // Map user stack (guard page gap between app and stack)
    uint64 ustack_va = BASE_ADDRESS + map_size + PGSIZE;
    void *stack = kalloc();
    if (stack == 0)
        panic("bin_loader: no memory for stack");
    memset(stack, 0, PGSIZE);
    if (mappages(pg, ustack_va, USER_STACK_SIZE, (uint64)stack,
                 PTE_U | PTE_R | PTE_W | PTE_X) != 0)
        panic("bin_loader: map stack failed");

    p->ustack           = ustack_va;
    p->trapframe->epc   = BASE_ADDRESS;
    p->trapframe->sp    = ustack_va + USER_STACK_SIZE;
    p->max_page         = PGROUNDUP(ustack_va + USER_STACK_SIZE - 1) / PGSIZE;

    return pg;
}

int run_all_app()
{
    for (int i = 0; i < app_num; ++i) {
        struct proc *p = allocproc();
        uint64 start = app_info_ptr[i];
        uint64 end   = app_info_ptr[i + 1];
        if (bin_loader(start, end, p) == 0)
            panic("run_all_app: bin_loader failed");
        tracef("load app %d [%p, %p)", i, start, end);
        p->state       = RUNNABLE;
        p->started     = 0;
        p->start_cycle = 0;
        memset(p->syscall_times, 0, sizeof(p->syscall_times));
    }
    return 0;
}