#include <types.h>
#include <x86.h>
#include <mmu.h>
#include <memlayout.h>
#include <pmm.h>
#include <stdio.h>
#include <assert.h>
#include <buddy.h>

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

/* temporary kernel stack */
uint8_t stack0[1024];

/* gdt_init - initialize the default GDT and TSS */
static void
gdt_init(void) {
    // Setup a TSS so that we can get the right stack when we trap from
    // user to the kernel. But not safe here, it's only a temporary value,
    // it will be set to KSTACKTOP in lab2.
    ts.ts_esp0 = (uint32_t)&stack0 + sizeof(stack0);
    ts.ts_ss0 = KERNEL_DS;

    // initialize the TSS filed of the gdt
    gdt[SEG_TSS] = SEG16(STS_T32A, (uint32_t)&ts, sizeof(ts), DPL_KERNEL);
    gdt[SEG_TSS].sd_s = 0;

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

// 
static void setup_mm_page(void) {
    uintptr_t mem_end = get_mem_layout();
    mem_end = ROUNDDOWN(mem_end, PAGE_SIZE);
    size_t kern_size, user_size;

    extern char end[];
    uintptr_t mem_st = ROUNDUP((uintptr_t)end, PAGE_SIZE);

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

    cprintf("kern phy st: %x\n", kern_st);
    cprintf("user phy st: %x\n", user_st);
    cprintf("kern phy pages: %d\n", kern_pages);
    cprintf("user phy pages: %d\n", user_pages);
    cprintf("mem st: %x\n", mem_st);

    kpages = (page_t*)mem_st;
    page_t *page = kpages;

    for (uintptr_t st = 0; st < user_st; st += PAGE_SIZE) {
        page_set_reserved(page);
        ++page;
    }

    assert((uintptr_t)page < kern_st);

    uintptr_t kbuddy_addr = (uintptr_t)page;
    uintptr_t kbuddy_buff = kbuddy_addr + sizeof(buddy_t);
    uintptr_t kbuddy_buff_ed = kbuddy_buff + kern_pages * 8;

    assert(kbuddy_buff_ed < kern_st);

    cprintf("buddy_buff: %x\n", kbuddy_buff);
    cprintf("buddy_buff_ed: %x\n", kbuddy_buff_ed);

    // set kern buddy addr
    kern_bd = (buddy_t*)kbuddy_addr;
    buddy_init(kern_bd, kern_pages, kbuddy_buff);

    
}


static void map_kern_addr_liner(uintptr_t phy_ed) {

}

/* pmm_init - initialize the physical memory management */
void
pmm_init(void) {
    gdt_init();
    setup_mm_page();
    cprintf("pmm init done.\n");
}

