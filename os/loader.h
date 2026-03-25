#ifndef LOADER_H
#define LOADER_H

#include "const.h"
#include "types.h"
#include "proc.h"
#include "riscv.h"

int finished();
void loader_init();
int run_all_app();
pagetable_t bin_loader(uint64 start, uint64 end, struct proc *p);

#define MAX_APP_SIZE (0x20000)
#define BASE_ADDRESS (0x80420000)
#define USTACK_SIZE  (PAGE_SIZE)
#define KSTACK_SIZE  (PAGE_SIZE)
#define TRAP_PAGE_SIZE (PAGE_SIZE)

#endif // LOADER_H