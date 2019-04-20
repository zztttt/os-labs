// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "backtrace", "Backtrace calling stack", mon_backtrace },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "time", "Display time about kernel", mon_time},
	{ "showmappings", "Display information about physical page mappings", mon_showmappings},
};

/***** Implementations of basic kernel monitor commands *****/
uint64_t rdtsc(){
        uint32_t lo,hi;

        __asm__ __volatile__
        (
         "rdtsc":"=a"(lo),"=d"(hi)
        );
        return (uint64_t)hi<<32|lo;
}
int
mon_showmappings(int argc, char **argv, struct Trapframe *tf)
{
	char perms[1 << 8] = {
        [0] = '-', 
		[PTE_W] = 'W',
        [PTE_U] = 'U',
        [PTE_A] = 'A',
        [PTE_D] = 'D',
        [PTE_PS] = 'S'
    };
    char *arg1 = argv[1], *arg2 = argv[2], *arg3 = argv[3];
    char *endptr;
    if (argc != 3) panic("Error! Two arguments expected!\n");
	uintptr_t va_l = strtol(arg1, &endptr, 16);
    if (*endptr) panic("Error! Start address format error!\n");
    uintptr_t va_r = strtol(arg2, &endptr, 16);
    if (*endptr) panic("Error! End address format error!\n");
    if (va_l > va_r) panic("Error! Address range error!\n");
	cprintf("----------------------------------------------------------------------------|\n");
	cprintf("      vitual addr            physical addr	  entry   permissions -|\n");
    cprintf("----------------------------------------------------------------------------|\n");

    pde_t *pgdir = (pde_t *)PGADDR(PDX(UVPT), PDX(UVPT), 0);
	char perm_w, perm_u, perm_a, perm_d, perm_s;	

	while (va_l <= va_r) 
	{
        pde_t pde = pgdir[PDX(va_l)];
        if (pde & PTE_P) 
		{
            perm_w = perms[pde & PTE_W];
            perm_u = perms[pde & PTE_U];
            perm_a = perms[pde & PTE_A];
            perm_d = perms[pde & PTE_D];
            perm_s = perms[pde & PTE_PS];
            pde = PTE_ADDR(pde);
            if (va_l < KERNBASE) {
                cprintf(" |	[0x%08x - 0x%08x]		", va_l, va_l + PTSIZE - 1);
                cprintf(" PDE[%03x] --%c%c%c--%c%cP  -|\n", PDX(va_l), perm_s, perm_d, perm_a, perm_u, perm_w);
                pte_t *pte = (pte_t *) (pde + KERNBASE);
                for (size_t i = 0; i < NPDENTRIES && va_l <= va_r; i++, va_l += PGSIZE) {
                    if (pte[i] & PTE_P) {
                        perm_w = perms[pte[i] & PTE_W];
                        perm_u = perms[pte[i] & PTE_U];
                        perm_a = perms[pte[i] & PTE_A];
                        perm_d = perms[pte[i] & PTE_D];
                        perm_s = perms[pte[i] & PTE_PS];
                        cprintf(" |-[0x%08x - 0x%08x]", va_l, va_l + PGSIZE - 1);   
                        cprintf(" [0x%08x - 0x%08x] ", PTE_ADDR(pte[i]), PTE_ADDR(pte[i]) + PGSIZE - 1);           
						cprintf(" PTE[%03x] --%c%c%c--%c%cP-|\n", i, perm_s, perm_d, perm_a, perm_u, perm_w);
					}
                }
			}
			if (va_l == 0xffc00000) break;
        }

        va_l += PTSIZE;
    }

    return 0;
}
int
mon_time(int argc, char **argv, struct Trapframe *tf){
	uint64_t begin = 0, end = 0;
	char c[256];
	bool found = false;
	int i;
	//cprintf("%s\n", argv[0]);
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[1], commands[i].name) == 0){
			begin = rdtsc();
			commands[i].func(argc-1, argv+1, tf);
			end = rdtsc();
			strcpy(c, argv[0]);
			found = true;
			break;
		}
	}
	if(found)
		cprintf("%s cycles:%d\n", c, end - begin);
	else
		cprintf("%s\n", "Command not found!");
	return 0;
}

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

// Lab1 only
// read the pointer to the retaddr on the stack
static uint32_t
read_pretaddr() {
    uint32_t pretaddr;
    __asm __volatile("leal 4(%%ebp), %0" : "=r" (pretaddr)); 
    return pretaddr;
}

void
do_overflow(void)
{
    cprintf("Overflow success\n");
}

void
start_overflow(void)
{
	// You should use a techique similar to buffer overflow
	// to invoke the do_overflow function and
	// the procedure must return normally.

    // And you must use the "cprintf" function with %n specifier
    // you augmented in the "Exercise 9" to do this job.

    // hint: You can use the read_pretaddr function to retrieve 
    //       the pointer to the function call return address;

    char str[256] = {};
    int nstr = 0;

	//Lab1 Code
    char* pret_addr = (char *) read_pretaddr();
	//char* overflow_addr = (char*) ((uint32_t)do_overflow);
    uint32_t overflow_addr = (uint32_t) do_overflow;
    int i;
	for(i = 0; i < 4; ++i){
		//store original ret_addr in before+4
		memset(pret_addr+4+i, *(pret_addr+i), 1);
	}
	for(i = 0; i < 4; ++i){
		//set overflow ret_addr
		memset(pret_addr+i, (overflow_addr>>(8*i)) & 0xFF, 1);
	}
}

void
overflow_me(void)
{
        start_overflow();
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
	cprintf("Stack backtrace\n");
	uint32_t ebp = read_ebp();
	cprintf("ebp : %x\n", ebp);
	while(ebp != 0){
		uint32_t eip = *(int*)(ebp+4);
		cprintf("  eip %08x  ebp %08x  args %08x %08x %08x %08x %08x\n",
				eip, ebp,
				*(int*)(ebp+8),*(int*)(ebp+12),*(int*)(ebp+16),*(int*)(ebp+20),*(int*)(ebp+24));
		struct Eipdebuginfo info;
		if(debuginfo_eip(eip,&info)>=0){
			cprintf("         %s:%d %.*s+%d\n",
			info.eip_file, info.eip_line,
			info.eip_fn_namelen, info.eip_fn_name, eip-info.eip_fn_addr);
		}
		ebp = *(int*)ebp;
	}
	overflow_me();
    	cprintf("Backtrace success\n");
	return 0;
}



/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	int x = 1, y = 3, z = 4;
	cprintf("x %d, y %x, z %d\n", x, y, z);
	/*unsigned int i = 0x00646c72;
    cprintf("H%x Wo%s", 57616, &i);*/
	//cprintf("x=%d y=%d", 3);




	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
