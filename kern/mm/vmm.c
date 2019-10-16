#include <vmm.h>
#include <pmm.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>


#define min(x, y)   ((x) < (y) ? (x) : (y))

#define max(x, y)   ((x) > (y) ? (x) : (y))


inline static void remove_vma(vmm_t *mm, vma_t *vma) {
    ASSERT(vma->mm == mm);

    list_erase(&mm->vma_set, &vma->list_tag);
}

static int vma_merge_check(vma_t *vma1, vma_t *vma2) {
    if (vma1->flag != vma2->flag)
        return -1;
    
    if (vma1->st_addr > vma2->ed_addr || vma1->ed_addr < vma2->st_addr) {
        return -2;
    }
    return 0;
}

// return vma need to free
static void merge_vma(vma_t *keep, vma_t *free) {
    // keep vma1, free vma2
    if (keep->mm && free->mm) {
        ASSERT(keep->mm == free->mm);
    } 

    keep->st_addr = min(keep->st_addr, free->st_addr);
    keep->ed_addr = max(keep->ed_addr, free->ed_addr);

    if (free->mm) {
        if (!keep->mm) {
            list_replace(&free->list_tag, &keep->list_tag);
            keep->mm = free->mm;
        } else {
            remove_vma(free->mm, free);
        }
    }
    
    kfree(free);
}

static int try_merge_vma_range(vma_t *nvma, vma_t *st_vma, vma_t *ed_vma) {

    bool cmerge = 1;

    list_elem_t *st_tag = &st_vma->list_tag, *ed_tag = &ed_vma->list_tag;
    list_elem_t *tmp = st_tag, *tnxt = tmp->next;

    vma_t *tmp_vma, *next_vma;

    // check vma flag
    while (tmp != ed_tag) {
        tmp_vma = le2vma(tmp);
        next_vma = le2vma(tnxt);
        if (tmp_vma->flag != next_vma->flag || tmp_vma->flag != nvma->flag) {
            cmerge = 0;
            break;
        }
        tmp = tnxt;
        tnxt = tnxt->next;
    }

    if (!cmerge)
        return -1;

    tmp = st_tag;
    tnxt = ed_tag->next;

    while (tmp != tnxt) {
        ASSERT(vma_merge_check(nvma, le2vma(tmp)) == 0);
        merge_vma(nvma, le2vma(tmp));
        tmp = tmp->next;
    }

    return 0;
}


static bool find_vma_func(list_elem_t *le, void *arg) {
    ASSERT(le && arg);
 
    vma_t *cvma = le2vma(le);

    return cvma->ed_addr >= (uintptr_t)arg;
}


vma_t *vma_create(uintptr_t vm_start, uintptr_t vm_end, uint32_t flags) {
    ASSERT(vm_start < vm_end && flags); 

    vma_t *vma;
    if ((vma = kmalloc(sizeof(vma_t))) == NULL) {
        return NULL;
    }

    vma->st_addr = ROUNDDOWN(vm_start, PAGE_SIZE);
    vma->ed_addr = ROUNDUP(vm_end, PAGE_SIZE);

    vma->flag = flags;
    vma->mm = NULL;
    list_elem_init(&vma->list_tag);

    return vma;
}


// find vma, vma is sorted by ed addr
vma_t *find_vma(vmm_t *mm, uintptr_t addr) {
    ASSERT(mm);

    list_elem_t *le = NULL;
    if ((le = list_traversal(&mm->vma_set, find_vma_func, (void*)addr))  == NULL) {
        return NULL;
    }

    return le2vma(le);
}


int vma_add(vmm_t *mm, vma_t *vma) {
    ASSERT(mm && vma);

    vma_t *next = find_vma(mm, vma->ed_addr);
    vma_t *prev = find_vma(mm, vma->st_addr);

    // last vma in mm, just push_back 
    if (!prev && !next) {
        list_push_back(&mm->vma_set, &vma->list_tag);
        goto done;
    }

    // prev == null, the new is last vma
    if (prev && !next) {
        if (vma_merge_check(vma, next) != 0)
            return -1;
        merge_vma(vma, prev);
        goto done;
    }

retry:
    // prev == next && not NULL, 
    if (prev == next && prev) {
        if (vma_merge_check(vma, next) != 0) {
            list_insert_before(&mm->vma_set, &prev->list_tag, &vma->list_tag);
            goto done;
        }
        merge_vma(vma, prev);
        goto done;
    }

    // prev != next, next full or empty
    if (next == NULL) {
        next = le2vma(list_back(&mm->vma_set));
        goto retry;
    }

    ASSERT(prev && next);
    if (next->st_addr > vma->ed_addr) {
        next = le2vma(next->list_tag.prev);
        goto retry;
    }

    if (try_merge_vma_range(vma, prev, next) != 0)
        return -2;

done:
    vma->mm = mm;
    return 0;
}


vmm_t *mm_create(void) {
    vmm_t *vmm = kmalloc(sizeof(vmm_t));
    if (!vmm)
        return NULL;
    
    list_init(&vmm->vma_set);
    vmm->pgdir = NULL;
    vmm->ref_count = 0;
    vmm->brk = vmm->brk_start = 0;
    return vmm;
}

void mm_destroy(vmm_t *mm) {
    if (!mm)
        return;
    ASSERT(mm->ref_count == 0);

    vma_t *vma;
    list_elem_t *elem;

    for (elem = list_front(&mm->vma_set); elem != &mm->vma_set.tail; ) {
        list_erase(&mm->vma_set, elem);
        vma = le2vma(elem);
        elem = elem->next;
        kfree(vma);
    }
    kfree(mm);
}


int do_pgfault(vmm_t *mm, uint32_t error_code, uintptr_t addr) {
    // invalid param
    int ret = -1;
    void *paddr = (void*)addr;
    vma_t *vma = find_vma(mm, paddr);
    if (vma == NULL || vma->st_addr > paddr) {
        goto failed;
    }

    // stack overflow
    ret = -2;
    if (vma->flag & VM_STACK) {
        if (addr < (uintptr_t)(vma->st_addr) + PAGE_SIZE) {
            goto failed;
        }
    }

    switch (error_code & 3) 
    {
    default:
        // 3: write, present
    case 2:
        // write, not present
        if (!(vma->flag & VM_WRITE)) {
            // permition error
            ret = -3;
            goto failed;
        }
        break;
    case 1:
        // read, present
        goto failed;
    case 0:
        // read, not present
        if (!(vma->flag & (VM_READ | VM_EXEC))) {
            goto failed;
        } 
    }

    uint32_t perm = PG_US_U;
    if (vma->flag & VM_WRITE) {
        perm |= PG_RW_W;
    }

    addr = ROUNDDOWN(addr, PAGE_SIZE);

    // no memory
    ret = -4;
//    if (pgdir_insert_page(addr, perm) != 0) 
//        goto failed;

    ret = 0;

failed:
    return ret;
}


/*
vmm test area
*/

static int check_vma_sorted(vmm_t *mm) {
    list_elem_t *cur, *nxt;
    vma_t *vcur, *vnxt;
    cur = list_front(&mm->vma_set);
    nxt = cur->next;

    while (nxt != &mm->vma_set.tail) {
        vcur = le2vma(cur);
        vnxt = le2vma(nxt);
        if (vcur->st_addr >= vcur->ed_addr ||
            vnxt->st_addr >= vnxt->ed_addr) {
            return -1;
        }
        if (vcur->ed_addr >= vnxt->st_addr) {
            return -2;
        }
        cur = nxt;
        nxt = nxt->next;
    }
    return 0;
} 


static void check_vmm_vma(void) {
    vmm_t *mm = mm_create();
    ASSERT(mm);

    uint32_t flags = VM_READ;

    uint32_t base = 10 * PAGE_SIZE;
    uint32_t offset = 0;
    uint32_t len = 2 * PAGE_SIZE;
    int size = 2;
    for (int i=0; i<size; ++i) {
        vma_t *tvma = vma_create(base * i + offset, 
                                    base * i + offset + len, flags);
        ASSERT(tvma);
        ASSERT(vma_add(mm, tvma) == 0);
    }

    ASSERT(vma_num(mm) == size);
    
    offset = 4 * PAGE_SIZE;
    for (int i=0; i<size; ++i) {
        vma_t *tvma = vma_create(base * i + offset, 
                                    base * i + offset + len, flags);
        ASSERT(tvma);
        ASSERT(vma_add(mm, tvma) == 0);
    }

    ASSERT(vma_num(mm) == size * 2);
    
    offset = PAGE_SIZE;
    len = 2 * PAGE_SIZE;
    for (int i=0; i<size; ++i) {
        vma_t *tvma = vma_create(base * i + offset, 
                                    base * i + offset + len, flags);
        ASSERT(tvma);
        ASSERT(vma_add(mm, tvma) == 0);
    }

    ASSERT(vma_num(mm) == 2 *size);

    offset = PAGE_SIZE * 7;
    len = 2 * PAGE_SIZE;
    for (int i=0; i<size; ++i) {
        vma_t *tvma = vma_create(base * i + offset, 
                                    base * i + offset + len, flags);
        ASSERT(tvma);
        ASSERT(vma_add(mm, tvma) == 0);
    }

    ASSERT(vma_num(mm) == 3 * size);

    offset = PAGE_SIZE * 2;
    len = 6 * PAGE_SIZE;
    for (int i=0; i<size; ++i) {
        vma_t *tvma = vma_create(base * i + offset, 
                                    base * i + offset + len, flags);
        ASSERT(tvma);
        ASSERT(vma_add(mm, tvma) == 0);
    }

    ASSERT(vma_num(mm) == size);

    ASSERT(check_vma_sorted(mm) == 0);

    mm_destroy(mm);

    cprintf("check vmm pass.\n");
}


vmm_t *check_mm = NULL;

static void check_pgfault(void) {
    check_mm = mm_create();
    ASSERT(check_mm);

    uintptr_t user_base = 0x400000 * 2;
    uintptr_t staddr = user_base + 0x1000;
    uintptr_t edaddr = user_base + 0x1fff;

    uint32_t perm = VM_WRITE;

    vma_t *vma = vma_create(staddr, edaddr, perm);
    ASSERT(vma);

    ASSERT(vma_add(check_mm, vma) == 0);
    
    uintptr_t test_addr = staddr + 0x100;

    ASSERT(find_vma(check_mm, test_addr) == vma);

    int *t = (int*)test_addr;
    int tnum = 2;
    for (int i=0; i<tnum; ++i) {
        t[i] = i;
    }

    t = (int*)test_addr;
    for (int i=0; i<tnum; ++i) {
        ASSERT(t[i] == i);
    }
    
    cprintf("check pgfault pass.\n");
}

void vmm_init(void) {
    check_vmm_vma();
    //check_pgfault();
}