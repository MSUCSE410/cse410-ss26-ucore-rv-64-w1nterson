#include "syscall.h"
#include "console.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"
#include "vm.h"

uint64 console_write(uint64 va, uint64 len)
{
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	tracef("write size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return len;
}

uint64 console_read(uint64 va, uint64 len)
{
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	tracef("read size = %d", len);
	for (int i = 0; i < len; ++i) {
		int c = consgetc();
		str[i] = c;
	}
	copyout(p->pagetable, va, str, len);
	return len;
}

uint64 sys_write(int fd, uint64 va, uint64 len)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d\n", fd);
		return -1;
	}
	switch (f->type) {
	case FD_STDIO:
		return console_write(va, len);
	case FD_INODE:
		return inodewrite(f, va, len);
	default:
		panic("unknown file type %d\n", f->type);
	}
}

uint64 sys_read(int fd, uint64 va, uint64 len)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d\n", fd);
		return -1;
	}
	switch (f->type) {
	case FD_STDIO:
		return console_read(va, len);
	case FD_INODE:
		return inoderead(f, va, len);
	default:
		panic("unknown file type %d\n", f->type);
	}
}

__attribute__((noreturn)) void sys_exit(int code)
{
	exit(code);
	__builtin_unreachable();
}

uint64 sys_sched_yield()
{
	yield();
	return 0;
}

uint64 sys_gettimeofday(uint64 val, int _tz)
{
	struct proc *p = curr_proc();
	uint64 cycle = get_cycle();
	TimeVal t;
	t.sec = cycle / CPU_FREQ;
	t.usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	copyout(p->pagetable, val, (char *)&t, sizeof(TimeVal));
	return 0;
}

uint64 sys_getpid()
{
	return curr_proc()->pid;
}

uint64 sys_getppid()
{
	struct proc *p = curr_proc();
	return p->parent == NULL ? IDLE_PID : p->parent->pid;
}

uint64 sys_clone()
{
	debugf("fork!");
	return fork();
}

static inline uint64 fetchaddr(pagetable_t pagetable, uint64 va)
{
	uint64 *addr = (uint64 *)useraddr(pagetable, va);
	return *addr;
}

uint64 sys_exec(uint64 path, uint64 uargv)
{
	struct proc *p = curr_proc();
	char name[MAX_STR_LEN];
	copyinstr(p->pagetable, name, path, MAX_STR_LEN);
	uint64 arg;
	static char strpool[MAX_ARG_NUM][MAX_STR_LEN];
	char *argv[MAX_ARG_NUM];
	int i;
	for (i = 0; uargv && (arg = fetchaddr(p->pagetable, uargv));
	     uargv += sizeof(char *), i++) {
		copyinstr(p->pagetable, (char *)strpool[i], arg, MAX_STR_LEN);
		argv[i] = (char *)strpool[i];
	}
	argv[i] = NULL;
	return exec(name, (char **)argv);
}

uint64 sys_wait(int pid, uint64 va)
{
	struct proc *p = curr_proc();
	int *code = (int *)useraddr(p->pagetable, va);
	return wait(pid, code);
}

// CH5: restored sys_spawn
uint64 sys_spawn(uint64 va)
{
	struct proc *p = curr_proc();
	char name[200];
	copyinstr(p->pagetable, name, va, 200);

	struct inode *ip = namei(name);
	if (ip == 0)
		return -1;

	struct proc *np = allocproc();
	if (np == NULL) {
		iput(ip);
		return -1;
	}

	init_stdio(np);
	np->parent = p;

	bin_loader(ip, np);
	iput(ip);

	char *argv[2];
	argv[0] = name;
	argv[1] = NULL;
	np->trapframe->a0 = push_argv(np, argv);

	// stride scheduler finds it by pool scan
	return np->pid;
}

// CH5: restored sys_set_priority
uint64 sys_set_priority(long long prio)
{
	if (prio < 2)
		return -1;
	struct proc *p = curr_proc();
	p->priority = prio;
	p->pass     = BIG_STRIDE / prio;
	return prio;
}

// CH4: restored sys_task_info
uint64 sys_task_info(uint64 ti_va)
{
	if (ti_va == 0) return -1;
	struct proc *p = curr_proc();
	uint64 pa = useraddr(p->pagetable, ti_va);
	if (pa == 0) return -1;
	TaskInfo *ti = (TaskInfo *)pa;
	ti->status = TaskStatusRunning;
	for (int i = 0; i < MAX_SYSCALL_NUM; i++)
		ti->syscall_times[i] = p->syscall_times[i];
	uint64 now = get_cycle();
	ti->time = (p->started && now >= p->start_cycle) ?
	           (int)((now - p->start_cycle) * 1000 / CPU_FREQ) : 0;
	return 0;
}

// CH4: restored sys_mmap
uint64 sys_mmap(uint64 start, uint64 len, int port, int flag, int fd)
{
	if (len == 0) return 0;
	if (len > (1UL << 30)) return -1;
	if (port & ~0x7) return -1;
	if ((port & 0x7) == 0) return -1;
	if (!PGALIGNED(start)) return -1;
	len = PGROUNDUP(len);
	int perm = PTE_U;
	if (port & 0x1) perm |= PTE_R;
	if (port & 0x2) perm |= PTE_W;
	if (port & 0x4) perm |= PTE_X;
	struct proc *p = curr_proc();
	uint64 end = start + len;
	for (uint64 va = start; va < end; va += PGSIZE) {
		if (walkaddr(p->pagetable, va) != 0)
			return -1;
	}
	for (uint64 va = start; va < end; va += PGSIZE) {
		void *mem = kalloc();
		if (mem == 0) return -1;
		memset(mem, 0, PGSIZE);
		if (mappages(p->pagetable, va, PGSIZE, (uint64)mem, perm) != 0) {
			kfree(mem);
			return -1;
		}
	}
	uint64 new_max = (end - 1) / PGSIZE;
	if (new_max > p->max_page)
		p->max_page = new_max;
	return 0;
}

// CH4: restored sys_munmap
uint64 sys_munmap(uint64 start, uint64 len)
{
	if (len == 0) return 0;
	if (!PGALIGNED(start)) return -1;
	len = PGROUNDUP(len);
	struct proc *p = curr_proc();
	uint64 end = start + len;
	for (uint64 va = start; va < end; va += PGSIZE) {
		if (walkaddr(p->pagetable, va) == 0)
			return -1;
	}
	uvmunmap(p->pagetable, start, len / PGSIZE, 1);
	return 0;
}

uint64 sys_openat(uint64 va, uint64 omode, uint64 _flags)
{
	struct proc *p = curr_proc();
	char path[200];
	copyinstr(p->pagetable, path, va, 200);
	return fileopen(path, omode);
}

uint64 sys_close(int fd)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d", fd);
		return -1;
	}
	fileclose(f);
	p->files[fd] = 0;
	return 0;
}

int sys_fstat(int fd, uint64 stat)
{
    if (fd < 0 || fd >= FD_BUFFER_SIZE)
        return -1;
    struct proc *p = curr_proc();
    struct file *f = p->files[fd];
    if (f == NULL || f->type != FD_INODE)
        return -1;
    Stat *st = (Stat *)useraddr(p->pagetable, stat);
    if (st == 0)
        return -1;
    struct inode *ip = f->ip;
    ivalid(ip);
    st->dev   = ip->dev;
    st->ino   = ip->inum;
    st->mode  = (ip->type == T_DIR) ? DIR : FILE;
    st->nlink = ip->nlink;
    memset(st->pad, 0, sizeof(st->pad));
    return 0;
}

int sys_linkat(int olddirfd, uint64 oldpath, int newdirfd, uint64 newpath, uint64 flags)
{
    struct proc *p = curr_proc();
    char old[MAXPATH], new[MAXPATH];
    copyinstr(p->pagetable, old, oldpath, MAXPATH);
    copyinstr(p->pagetable, new, newpath, MAXPATH);

    // error: linking to same name
    if (strncmp(old, new, MAXPATH) == 0)
        return -1;

    struct inode *ip = namei(old);
    if (ip == 0)
        return -1;

    ivalid(ip);

    // create new directory entry pointing to same inode
    struct inode *dp = root_dir();
    if (dirlink(dp, new, ip->inum) < 0) {
        iput(ip);
        iput(dp);
        return -1;
    }

    // increment link count and sync to disk
    ip->nlink++;
    iupdate(ip);

    iput(ip);
    iput(dp);
    return 0;
}

int sys_unlinkat(int dirfd, uint64 name, uint64 flags)
{
    struct proc *p = curr_proc();
    char path[MAXPATH];
    copyinstr(p->pagetable, path, name, MAXPATH);

    struct inode *ip = namei(path);
    if (ip == 0)
        return -1;

    ivalid(ip);

    // remove the directory entry
    struct inode *dp = root_dir();
    if (dirunlink(dp, path) < 0) {
        iput(ip);
        iput(dp);
        return -1;
    }

    // decrement link count — if it hits 0, iput will free the inode
    ip->nlink--;
    iupdate(ip);

    iput(ip);
    iput(dp);
    return 0;
}

extern char trap_page[];

void syscall()
{
	struct trapframe *trapframe = curr_proc()->trapframe;
	int id = trapframe->a7, ret;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	tracef("syscall %d args = [%x, %x, %x, %x, %x, %x]", id, args[0],
	       args[1], args[2], args[3], args[4], args[5]);
	// CH4: track syscall counts
	if (id >= 0 && id < MAX_SYSCALL_NUM)
		curr_proc()->syscall_times[id]++;
	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_read:
		ret = sys_read(args[0], args[1], args[2]);
		break;
	case SYS_openat:
		ret = sys_openat(args[0], args[1], args[2]);
		break;
	case SYS_close:
		ret = sys_close(args[0]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday(args[0], args[1]);
		break;
	case SYS_getpid:
		ret = sys_getpid();
		break;
	case SYS_getppid:
		ret = sys_getppid();
		break;
	case SYS_clone:
		ret = sys_clone();
		break;
	case SYS_execve:
		ret = sys_exec(args[0], args[1]);
		break;
	case SYS_wait4:
		ret = sys_wait(args[0], args[1]);
		break;
	case SYS_spawn:
		ret = sys_spawn(args[0]);
		break;
	case SYS_task_info:
		ret = sys_task_info(args[0]);
		break;
	case SYS_mmap:
		ret = sys_mmap(args[0], args[1], args[2], args[3], args[4]);
		break;
	case SYS_munmap:
		ret = sys_munmap(args[0], args[1]);
		break;
	case SYS_setpriority:
		ret = sys_set_priority((long long)args[0]);
		break;
	case SYS_fstat:
		ret = sys_fstat(args[0], args[1]);
		break;
	case SYS_linkat:
		ret = sys_linkat(args[0], args[1], args[2], args[3], args[4]);
		break;
	case SYS_unlinkat:
		ret = sys_unlinkat(args[0], args[1], args[2]);
		break;  // fixed: was missing break in ch6 framework
	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}