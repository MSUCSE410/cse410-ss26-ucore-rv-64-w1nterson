#include "console.h"
#include "defs.h"
#include "loader.h"
#include "timer.h"
#include "trap.h"

void clean_bss()
{
	extern char s_bss[];
	extern char e_bss[];
	memset(s_bss, 0, e_bss - s_bss);
}

void main()
{
    clean_bss();
    printf("hello world!\n");
    kinit();        // must be first: sets up kalloc free list
    kvm_init();     // second: needs kalloc, enables paging
    proc_init();    // third: needs kernel_pagetable for idle
    loader_init();
    trap_init();
    timer_init();
    run_all_app();
    infof("start scheduler!");
    scheduler();
}