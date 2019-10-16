#ifndef __L_VMM_H
#define __L_VMM_H

#include <types.h>
#include <list.h>

typedef struct vmm {
    list_t      vma_set;
    uint32_t    *pgdir;
    uint32_t    ref_count;
    uintptr_t   brk_start;
    uintptr_t   brk;
} vmm_t;

typedef struct vma 
{
    vmm_t       *mm;
    uintptr_t   st_addr;
    uintptr_t   ed_addr;
    uint32_t    flag;
    list_elem_t list_tag;
} vma_t;

#define vma_num(mm) ((mm)->vma_set.len)    

#define le2vma(le)  elem2entry(vma_t, list_tag, le)

#define     VM_READ     0x00000001
#define     VM_WRITE    0x00000002
#define     VM_EXEC     0x00000004
#define     VM_STACK    0x00000008


vma_t *vma_create(uintptr_t vm_start, uintptr_t vm_end, uint32_t vm_flags);

vma_t *find_vma(vmm_t *mm, uintptr_t addr);

int vma_add(vmm_t *mm, vma_t *vma);

void vmm_init(void);

vmm_t *mm_create(void);

void mm_destroy(vmm_t *mm);

int do_pgfault(vmm_t *mm, uint32_t error_code, uintptr_t addr);

#endif