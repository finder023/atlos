#ifndef __KERN_MM_PMM_H__
#define __KERN_MM_PMM_H__

#include <types.h>
#include <memlayout.h>

#define	 PG_P_1	  1	// 页表项或页目录项存在属性位
#define	 PG_P_0	  0	// 页表项或页目录项存在属性位
#define	 PG_RW_R  0	// R/W 属性位值, 读/执行
#define	 PG_RW_W  2	// R/W 属性位值, 读/写/执行
#define	 PG_US_S  0	// U/S 属性位值, 系统级
#define	 PG_US_U  4	// U/S 属性位值, 用户级


#define is_pow_of_2(x)  (((x) & ((x) - 1)) == 0)

#define PAGE_P(addr) ((addr) & (uintptr_t)0x1)

#define PDE_INDEX(addr) ((addr) >> 22)
#define PTE_INDEX(addr) (((addr) & 0x003ff000) >> 12)

inline uint32_t* pde_ptr(uintptr_t addr) {
    return (uint32_t*)0xfffff000 + PDE_INDEX(addr);
}

#define PDE_PTR(addr)   ((uint32_t*)0xfffff000 + PDE_INDEX(addr))

inline uint32_t* pte_ptr(uintptr_t addr) {
    return (uint32_t*)0xffc00000 + (((uint32_t)addr & 0xffc00000) >> 12) + 
                                                            PTE_INDEX(addr); 
}

#define PTE_PTR(addr)   ((uint32_t*)0xffc00000 +\
                     (((uint32_t)addr & 0xffc00000) >> 12) + PTE_INDEX(addr))


inline static uint32_t
next_pow_of_2(uint32_t x){
	if ( is_pow_of_2(x) )
		return x;
	x |= x>>1;
	x |= x>>2;
	x |= x>>4;
	x |= x>>8;
	x |= x>>16;
	return x+1;
}


/* page_t area */
typedef struct page {
    uint32_t ref_count;
    uint32_t flag;
    uint32_t bd_size;       // buddy header size
    void *slab;
} page_t;

#define POWOF2(x)   (((x) & (x-1)) == 0)

#define ROUNDDOWN(x, n) ((x) & (~(n-1)))

#define ADDRALIGN(addr, n)  ({          \
    uint32_t _t = next_pow_of_2(n) - 1; \
    ((uintptr_t)addr + _t) & (~_t);     \
})

#define KADDRV2P(kvaddr)    ((uintptr_t)(kvaddr) - KERNBASE)

#define KADDRP2V(kpaddr)    ((uintptr_t)(kpaddr) + KERNBASE)

#define ROUNDUP(x, n) (((x) + (n-1)) & (~(n-1)))

#define SET_BIT(n, flag)    ((flag) |= 1 << (n))
#define CLEAR_BIT(n, flag)  ((flag) &= ~(1 << (n)))
#define TEST_BIT(n, flag)   ((flag) & (1 << (n)))

#define PG_RESERVED     0   // page is reserved
#define PG_SLAB         1   // page used in slab system
#define PG_DIRTY        2   // page has been modified
#define PG_BDHEAD       3   // page is buddy alloc first page

#define page_set_reserved(page)     SET_BIT(PG_RESERVED, page->flag)
#define page_clear_reserved(page)   CLEAR_BIT(PG_RESERVED, page->flag)
#define page_reserved(page)         TEST_BIT(PG_RESERVED, page->flag)

#define page_set_slab(page)         SET_BIT(PG_SLAB, page->flag)
#define page_clear_slab(page)       CLEAR_BIT(PG_SLAB, page->flag)
#define page_slab(page)             TEST_BIT(PG_SLAB, page->flag) 

#define page_set_dirty(page)        SET_BIT(PG_DIRTY, page->flag)
#define page_clear_dirty(page)      CLEAR_BIT(PG_DIRTY, page->flag) 
#define page_dirty(page)            TEST_BIT(PG_DIRTY, page->flag)

#define page_set_bdhead(page)       SET_BIT(PG_BDHEAD, page->flag)
#define page_clear_bdhead(page)     CLEAR_BIT(PG_BDHEAD, page->flag)
#define page_bdhead(page)           TEST_BIT(PG_BDHEAD, page->flag)

#define kernel_vir_base  0xc0000000

#define pages_addr  (0x200000 + KERNEL_VADDR_START)

#define PAGE_SHIFT  12

#define PAGE_SIZE 4096

#define page2offset(page)   ((page_t*)(page) - pages_addr)

#define vira2offset(addr)   ((ROUNDDOWN(addr, PAGE_SIZE) - kernel_vir_base) \
                                                                >> PAGE_SHIFT)

uintptr_t page2kpaddr(page_t *page);

uintptr_t page2kvaddr(page_t *page);

page_t *kvaddr2page(uintptr_t vaddr);

page_t *kpaddr2page(uintptr_t paddr);

void load_esp0(uintptr_t esp0);

void pmm_init(void);

page_t *kalloc_pages(size_t n);

void kfree_pages(page_t *page, size_t n);

void *kmalloc(size_t n);

void kfree(void *p);

extern char bootstack[], bootstacktop[];

extern uintptr_t boot_cr3;

#endif /* !__KERN_MM_PMM_H__ */

