// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

extern void _pgfault_upcall(void);
//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	if (!((err & FEC_WR) && (uvpd[PDX(addr)] & PTE_P) &&  (uvpt[PGNUM(addr)] & (PTE_P | PTE_COW)) == (PTE_P | PTE_COW)))
		panic("pgfault: real page fault\n");

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
	// static int sys_page_alloc(envid_t envid, void *va, int perm)
	// static int sys_page_map(envid_t srcenvid, void *srcva,
	//		envid_t dstenvid, void *dstva, int perm)
	// static int sys_page_unmap(envid_t envid, void *va)
	addr = ROUNDDOWN(addr, PGSIZE);
	
	if ((r = sys_page_alloc(0, PFTEMP, PTE_P|PTE_U|PTE_W)) < 0)
  		panic("sys_page_alloc: %e", r);
	memmove(PFTEMP, addr, PGSIZE);

	if ((r = sys_page_map(0, PFTEMP, 0, addr, PTE_P|PTE_U|PTE_W)) < 0)
  		panic("sys_page_map: %e", r);
	if ((r = sys_page_unmap(0, PFTEMP)) < 0)
		panic("sys_page_unmap: %e", r);

	//panic("pgfault not implemented");
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;

	// LAB 4: Your code here.
	//panic("duppage not implemented");
	void *addr = (void *)(pn * PGSIZE);

	if (uvpt[pn] & (PTE_W | PTE_COW)) {
		if ((r = sys_page_map((envid_t)0, addr, envid, addr, PTE_U|PTE_P|PTE_COW) < 0))
    		panic("sys_page_map: %e\n", r);
  		if ((r = sys_page_map((envid_t)0, addr, 0, addr, PTE_U|PTE_P|PTE_COW) < 0))
    		panic("sys_page_map: %e\n", r);
	} else {
  		if ((r = sys_page_map((envid_t)0, addr, envid, addr, PTE_U|PTE_P )) < 0)
    		panic("sys_page_map: %e\n", r);
	}
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	//panic("fork not implemented");

	envid_t envid;
	uintptr_t addr;
	int r;

	// step 1 : The parent installs pgfault() as the C-level page fault handler, using the set_pgfault_handler() function you implemented above.
	set_pgfault_handler(pgfault);

	// step 2 : The parent calls sys_exofork() to create a child environment.
	// static envid_t sys_exofork(void)
	// envid_t sys_getenvid(void)
	envid = sys_exofork();
	if (envid < 0)  panic("sys_exofork: %e", envid);
	
	// child environment
	if (envid == 0) {
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}

	// parent environment
	// step 3 : call duppage
	// User read-only virtual page table (see 'uvpt' below)
	// 		#define UVPT		(ULIM - PTSIZE)
	// .set uvpd, (UVPT+(UVPT>>12)*4)
	// page number field of address
	// 		#define PGNUM(la)	(((uintptr_t) (la)) >> PTXSHIFT)
	// page directory index
	// 		#define PDX(la)		((((uintptr_t) (la)) >> PDXSHIFT) & 0x3FF)
	// static int duppage(envid_t envid, unsigned pn)
	for (addr = UTEXT; addr < USTACKTOP; addr += PGSIZE) {
	    if ((uvpd[PDX(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & (PTE_P|PTE_U)) == (PTE_P|PTE_U)) {
    		duppage(envid, PGNUM(addr));
		}
	}

	// step 4 : The parent sets the user page fault entrypoint for the child to look like its own.
	// static int sys_page_alloc(envid_t envid, void *va, int perm)
	// static int sys_env_set_pgfault_upcall(envid_t envid, void *func)
	if ((r = sys_page_alloc(envid, (void *)(UXSTACKTOP-PGSIZE), PTE_U|PTE_W|PTE_P)) < 0)
    	panic("sys_page_alloc: %e\n",r);
  	if ((r = sys_env_set_pgfault_upcall(envid, _pgfault_upcall)) < 0)
    	panic("sys_env_set_pgfault_upcall: %e\n",r);

	// step 5 : The child is now ready to run, so the parent marks it runnable.
	// static int sys_env_set_status(envid_t envid, int status)
	if ((r = sys_env_set_status(envid, ENV_RUNNABLE)) < 0)
		panic("sys_env_set_status: %e\n",r);
	
	return envid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
