#include <types.h>
#include <x86.h>
#include <mmu.h>
#include <memlayout.h>
#include <pmm.h>
#include <stdio.h>
#include <assert.h>
#include <buddy.h>
#include <error.h>

/* *
 * Task State Segment:
 *
 * The TSS may reside anywhere in memory. A special segment register called
 * the Task Register (TR) holds a segment selector that points a valid TSS
 * segment descriptor which resides in the GDT. Therefore, to use a TSS
 * the following must be done in function gdt_init:
 *   - create a TSS descriptor entry in GDT
 *   - add enough information to the TSS in memory as needed
 *   - load the TR register with a segment selector for that segment
 *
 * There are several fileds in TSS for specifying the new stack pointer when a
 * privilege level change happens. But only the fields SS0 and ESP0 are useful
 * in our os kernel.
 *
 * The field SS0 contains the stack segment selector for CPL = 0, and the ESP0
 * contains the new ESP value for CPL = 0. When an interrupt happens in protected
 * mode, the x86 CPU will look in the TSS for SS0 and ESP0 and load their value
 * into SS and ESP respectively.
 * */
static struct taskstate ts = {0};

// virtual address of boot-time page directory
extern uintptr_t __boot_pgdir;
uintptr_t *boot_pgdir = &__boot_pgdir;
// physical address of boot-time page directory
uintptr_t boot_cr3;


/* *
 * Global Descriptor Table:
 *
 * The kernel and user segments are identical (except for the DPL). To load
 * the %ss register, the CPL must equal the DPL. Thus, we must duplicate the
 * segments for the user and the kernel. Defined as follows:
 *   - 0x0 :  unused (always faults -- for trapping NULL far pointers)
 *   - 0x8 :  kernel code segment
 *   - 0x10:  kernel data segment
 *   - 0x18:  user code segment
 *   - 0x20:  user data segment
 *   - 0x28:  defined for tss, initialized in gdt_init
 * */
static struct segdesc gdt[] = {
    SEG_NULL,
    [SEG_KTEXT] = SEG(STA_X | STA_R, 0x0, 0xFFFFFFFF, DPL_KERNEL),
    [SEG_KDATA] = SEG(STA_W, 0x0, 0xFFFFFFFF, DPL_KERNEL),
    [SEG_UTEXT] = SEG(STA_X | STA_R, 0x0, 0xFFFFFFFF, DPL_USER),
    [SEG_UDATA] = SEG(STA_W, 0x0, 0xFFFFFFFF, DPL_USER),
    [SEG_TSS]    = SEG_NULL,
};

static struct pseudodesc gdt_pd = {
    sizeof(gdt) - 1, (uint32_t)gdt
};

/* *
 * lgdt - load the global descriptor table register and reset the
 * data/code segement registers for kernel.
 * */
static inline void
lgdt(struct pseudodesc *pd) {
    asm volatile ("lgdt (%0)" :: "r" (pd));
    asm volatile ("movw %%ax, %%gs" :: "a" (USER_DS));
    asm volatile ("movw %%ax, %%fs" :: "a" (USER_DS));
    asm volatile ("movw %%ax, %%es" :: "a" (KERNEL_DS));
    asm volatile ("movw %%ax, %%ds" :: "a" (KERNEL_DS));
    asm volatile ("movw %%ax, %%ss" :: "a" (KERNEL_DS));
    // reload cs
    asm volatile ("ljmp %0, $1f\n 1:\n" :: "i" (KERNEL_CS));
}

/* *
 * load_esp0 - change the ESP0 in default task state segment,
 * so that we can use different kernel stack when we trap frame
 * user to kernel.
 * */
void
load_esp0(uintptr_t esp0) {
    ts.ts_esp0 = esp0;
}


/* gdt_init - initialize the default GDT and TSS */
static void
gdt_init(void) {
    // set boot kernel stack and default SS0
    load_esp0((uintptr_t)bootstacktop);
    ts.ts_ss0 = KERNEL_DS;

    // initialize the TSS filed of the gdt
    gdt[SEG_TSS] = SEGTSS(STS_T32A, (uintptr_t)&ts, sizeof(ts), DPL_KERNEL);

    // reload all segment registers
    lgdt(&gdt_pd);

    // load the TSS
    ltr(GD_TSS);
}


// get memory layout, return max_memory area
static uintptr_t get_mem_layout(void) {
    e820map_t *mmap = (e820map_t*)(E820MAP_ADDR);
    uintptr_t mem_end = 0;
    for (int i=0; i<mmap->nr_map; ++i) {
        uint64_t begin, end;
        begin = mmap->map[i].addr;
        end = begin + mmap->map[i].size;
        cprintf("  memory: %08llx, [%08llx, %08llx], type = %d.\n",
                mmap->map[i].size, begin, end - 1, mmap->map[i].type);

        if (mmap->map[i].type == E820_ARM) {
            if (end > mem_end && begin < KMAXSIZE) {
                mem_end = end;
            }
        }
    }

    extern char end[];
    cprintf("end addr: %x\n", end);
    return mem_end;
}

static page_t *kpages;

static buddy_t *kern_bd;

static size_t bd_page_off = 0;

inline uintptr_t page2kpaddr(page_t *page) {
    return (page - kpages) << PAGE_SHIFT;
}

inline uintptr_t page2kvaddr(page_t *page) {
    return page2kpaddr + KERNBASE;
}


static int pgdir_add(uintptr_t vaddr, uintptr_t paddr, uint32_t perm) {
    uint32_t *pdep, *ptep;
    pdep = PDE_PTR(vaddr);
    page_t *page;
    
    if (!PAGE_P(*pdep)) {
        if ((page = kalloc_pages(1)) == NULL) {
            warn("alloc pde failed at: %x.\n", vaddr);
            return E_NO_MEM;
        }

        uintptr_t ppdep = page2kpaddr(page);
        memset(KADDRP2V(ppdep), 0, PAGE_SIZE);
        *pdep = ppdep | perm | PTE_P;
    }

    assert(*pdep & PTE_P);

    ptep = PTE_PTR(vaddr);
    if (!(*ptep & PTE_P)) {
        *ptep = paddr | perm | PTE_P; 
    } else {
        warn("pte exist at: %x.\n", vaddr);
        return E_INVAL;
    }
    return 0;
}

static void init_reserved_pages(uintptr_t reserved_end) {
    page_t *page = kpages;
    for (uintptr_t st = 0; st < reserved_end; st += PAGE_SIZE) {
        page_set_reserved(page);
        ++page;
    }

    cprintf("reserved end page_addr: %x.\n", page);
    assert((uintptr_t)page < KADDRP2V(reserved_end));
}

static void map_kern_addr_liner(uintptr_t ed) {
    cprintf("__boot_pgdir: %x\n", __boot_pgdir);
    cprintf("boot_pgdir: %x\n", boot_pgdir);

    // do self map
    uintptr_t pboot_pgdir = KADDRV2P((uintptr_t)boot_pgdir);
    boot_pgdir[1023] = pboot_pgdir | PTE_P | PTE_W;

    

    for (uintptr_t addr = 0x400000; addr < ed; addr += PGSIZE) {
        if (pgdir_add(addr + KERNBASE, addr, PTE_W) != 0) {
            panic("map kern addr liner failed in: %x", addr);
        }
    }
}


// return end addr of bd_buff
static uintptr_t setup_kbuddy(buddy_t *bd, size_t n) {
    assert(bd && n);

    uintptr_t bd_buff = (uintptr_t)bd + sizeof(buddy_t);
    kern_bd = bd;
    buddy_init(bd, n, bd_buff);

    cprintf("buddy_buff: %x\n", bd_buff);
    bd_buff += n * 8;
    cprintf("buddy_buff_ed: %x\n", bd_buff);
    
    return bd_buff;
}



static void setup_mm_page(void) {
    uintptr_t mem_end = get_mem_layout();
    mem_end = ROUNDDOWN(mem_end, PAGE_SIZE);
    size_t kern_size, user_size;

    extern char end[];
    uintptr_t mem_st = ROUNDUP((uintptr_t)KADDRV2P(end), PAGE_SIZE);

    size_t all_mem = mem_end - mem_st;
    kern_size = all_mem / 2 + mem_st;

    if (kern_size > KMAXSIZE) {
        kern_size = KMAXSIZE;
    } else {
        kern_size = next_pow_of_2(kern_size);
        kern_size >>= 1;
    }

    size_t kern_pages = kern_size >> PAGE_SHIFT;
    // setup kern page
    size_t add_mem = 0;
    size_t nkpages = ((kern_size + mem_st) >> PAGE_SHIFT);
    // another one page_t for buddy_t;
    add_mem = (nkpages + 1) * sizeof(page_t);
    // add buddy buffer
    add_mem += kern_pages * 8;
    add_mem += (add_mem >> PAGE_SHIFT) * sizeof(page_t) + sizeof(page_t);
    add_mem = ROUNDUP(add_mem, PAGE_SIZE);

    uintptr_t kern_st = mem_st + add_mem;
    uintptr_t user_st = kern_st + kern_size;
    user_size = mem_end - user_st;
    user_size = next_pow_of_2(user_size) >> 1;

    size_t user_pages = user_size >> PAGE_SHIFT;

    cprintf("kern phy st: %x\n", (kern_st));
    cprintf("user phy st: %x\n", (user_st));
    cprintf("kern phy pages: %d\n", kern_pages);
    cprintf("user phy pages: %d\n", user_pages);
    cprintf("mem st: %x\n", mem_st);

    // set up buddy system
    kpages = (page_t*)setup_kbuddy((buddy_t*)(KADDRP2V(mem_st)), kern_pages);
    kpages = ADDRALIGN(kpages, sizeof(page_t));
    assert((uintptr_t)kpages < KADDRP2V(0x400000));

    bd_page_off = kern_st  >> PAGE_SHIFT;
    
    
    map_kern_addr_liner(user_st);

    init_reserved_pages(kern_st);

}




/* pmm_init - initialize the physical memory management */
void
pmm_init(void) {

    boot_cr3 = KADDRV2P(boot_pgdir);
    map_kern_addr_liner(0);
    gdt_init();
    setup_mm_page();
    cprintf("pmm init done.\n");
}


page_t *kalloc_pages(size_t n) {
    if (n == 0)
        return NULL;
    
    int bd_off;
    if ((bd_off = alloc_page_buddy(kern_bd, n) < 0))
        return NULL;
    
    return kpages + bd_off + bd_page_off;
}

void kfree_pages(page_t *page, size_t n) {
    assert(page);
    if (n == 0)
        return;

    size_t bd_off = (page - kpages) - bd_page_off;
    free_page_buddy(kern_bd, bd_off, n);
}
