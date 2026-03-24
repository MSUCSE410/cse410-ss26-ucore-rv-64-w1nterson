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
        errorf("bin_loader: kalloc failed");
        return 0;
    }
    memset(pg, 0, PGSIZE);

    // Map trampoline and trapframe
    if (mappages(pg, TRAMPOLINE, PGSIZE, (uint64)trampoline, PTE_R | PTE_X) < 0) {
        kfree(pg);
        errorf("bin_loader: failed to map trampoline");
        return 0;
    }
    if (mappages(pg, TRAPFRAME, PGSIZE, (uint64)p->trapframe, PTE_R | PTE_W) < 0) {
        panic("bin_loader: failed to map trapframe");
    }

    // Physical file size (what's actually stored in kernel image)
    uint64 file_size = end - start;

    // Virtual size: round up file size, but ensure we cover at least MAX_APP_SIZE
    // so .bss and other ALLOC-only sections have valid mapped pages
    uint64 map_size = PGROUNDUP(MAX_APP_SIZE);

    // Allocate and map page by page so we can zero each page
    // (important for .bss which is ALLOC but not in the file)
    for (uint64 offset = 0; offset < map_size; offset += PGSIZE) {
        void *mem = kalloc();
        if (mem == 0)
            panic("bin_loader: out of memory mapping app");
        memset(mem, 0, PGSIZE);  // zero entire page (handles .bss)

        // Copy file contents into this page if within file bounds
        if (offset < file_size) {
            uint64 copy_size = file_size - offset;
            if (copy_size > PGSIZE)
                copy_size = PGSIZE;
            memmove(mem, (void *)(start + offset), copy_size);
        }

        if (mappages(pg, BASE_ADDRESS + offset, PGSIZE, (uint64)mem,
                     PTE_U | PTE_R | PTE_W | PTE_X) != 0) {
            panic("bin_loader: mappages failed");
        }
    }

    p->pagetable = pg;

    // Stack goes just after the app virtual space
    uint64 ustack_bottom = BASE_ADDRESS + map_size + PGSIZE; // +PGSIZE for guard page
    void *stack_mem = kalloc();
    if (stack_mem == 0)
        panic("bin_loader: out of memory for stack");
    memset(stack_mem, 0, PGSIZE);
    if (mappages(pg, ustack_bottom, USTACK_SIZE, (uint64)stack_mem,
                 PTE_U | PTE_R | PTE_W | PTE_X) != 0) {
        panic("bin_loader: failed to map stack");
    }
    p->ustack = ustack_bottom;

    p->trapframe->epc = BASE_ADDRESS;
    p->trapframe->sp  = p->ustack + USTACK_SIZE;
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