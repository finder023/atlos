#include <slab.h>
#include <pmm.h>
#include <buddy.h>
#include <assert.h>
#include <stdio.h>


static const uint32_t obj_removed = 0xffffffff;

// extern from mm.c
extern const uintptr_t pages;


static const uint32_t slab_pages[num_cached] = {1,1,1,1,2,4,4,4,4,8};

// if len of cache empty list larger than max_empty_slabs, free some slabs 
static const uint32_t max_empty_slabs = 4; 

static mem_cache_t  mcached[num_cached];


inline static uint32_t size2index(uint32_t n) {
    uint32_t res = 0;
    for (; n; ++res, n >>= 1);
    return res - 1;
}

// empty slab free strategy
#define NORMAL_FREE 1
#define SOFT_FREE   2
#define FORCE_FREE  4

static void try_free_empty_slabs(mem_cache_t *slot, uint32_t flag) {
    uint32_t num_release = 0;
    slab_t *slab;
    list_elem_t *elem;
    uintptr_t buff;
    page_t *page;

    if (flag & NORMAL_FREE)
        num_release = slot->empty.len > max_empty_slabs ? \
                                slot->empty.len - max_empty_slabs : 0;
    else if (flag & SOFT_FREE)
        num_release = slot->empty.len / 2;
    else if (flag & FORCE_FREE)
        num_release = slot->empty.len;
    else
        return;

    for (uint32_t i=0; i<num_release; ++i) {
        elem = list_pop_back(&slot->empty);
        slab = tag2slab(elem);

        if (i > slab_t_in_slab_index)
            buff = slab->buff;
        else
            buff = (uintptr_t)slab;

        page = kvaddr2page(buff); 
        kfree_pages(page, page->bd_size);
    }
}


static void empty_slab_t_init(mem_cache_t *slot, slab_t *slab, uint32_t objs) {
    assert(slot && slab);

    slab->free = 0;
    slab->total_objs = objs;
    slab->free_objs = objs;
    slab->obj_order = slot->obj_order;

    for (int i=0; i<objs; ++i) {
        slab->obj_list[i] = i + 1;
    }
}


static int fill_empty_slot(uint32_t index) {
    // print_str_int("fill_empty_slot:", index);
    // very complex..
    mem_cache_t *slot = mcached + index;
    assert(list_empty(&slot->empty) && list_empty(&slot->partial));

    // just add one slab in empty list
    /* TODO
        num of objs per slab
        num of pages per slab
        slab_t in slab or not
        // there should be a threshold
    */
    
    uint32_t npages = slab_pages[index];
    slab_t *slab;
    uintptr_t buddy_buff;
    page_t *page;

    if ((page = kalloc_pages(npages)) == NULL)
        return 1;

    buddy_buff = page2kvaddr(page);
    uint32_t obj_size = 1 << slot->obj_order;
    uint32_t objs = (npages << PAGE_SHIFT) / obj_size;
    uint32_t slab_t_len = sizeof(slab_t) + (objs - 1) * sizeof(uint32_t);
    
    if (index > slab_t_in_slab_index) {
        slab = (slab_t*)slab_alloc(slab_t_len);
        if (!slab)
            return 1;

        empty_slab_t_init(slot, slab, objs);
        slab->buff = buddy_buff;
    }
    else {
        slab = (slab_t*)buddy_buff;

        // resize objs
        uint32_t slab_header_objs = slab_t_len / obj_size;
        slab_header_objs += slab_t_len % obj_size == 0 ? 0 : 1;

        uint32_t add_objs = slab_header_objs * sizeof(uint32_t) / 
                                        (sizeof(uint32_t) + obj_size);
        
        objs = objs - slab_header_objs + add_objs; 

        // resize slab_t_len
        slab_t_len = sizeof(slab_t) + (objs - 1) * sizeof(uint32_t);

        empty_slab_t_init(slot, slab, objs);

        uint32_t align_off = ROUNDUP(slab_t_len, obj_size);
        slab->buff = buddy_buff + align_off;
    }

    // set page_t slab flag
    page = kvaddr2page(buddy_buff);
    // print_str_int("page_t addr:", page);
    for (int i=0; i<npages; ++i) {
        page_set_slab(page);
        page->slab = slab;
        ++page;
    }

    list_push_back(&slot->empty, &slab->tag);
    return 0;
}


static uintptr_t fetch_obj_from_slab(mem_cache_t* cache, slab_t *slab) {
    assert(slab);
    assert(slab->free_objs > 0);

    uint32_t free = slab->free;

    assert(free < slab->total_objs);

    slab->free = slab->obj_list[free];

    // mark obj as removed
    slab->obj_list[free] = obj_removed;

    // uintptr_t obj = free * slab->obj_size + slab->buff;
    uintptr_t obj = free * (1 << slab->obj_order) + slab->buff;

    slab->free_objs--;
    
    // move slab from partial to full list
    if (slab->free_objs == 0) {
        // remove slab from part
        list_erase(&cache->partial, &slab->tag);

        // add slab to full list's head
        list_push_back(&cache->full, &slab->tag);
    }

    // move slab from empty to partial list
    if (slab->free_objs == slab->total_objs - 1) {
        list_erase(&cache->empty, &slab->tag);
        list_push_back(&cache->partial, &slab->tag);
    }

    // must return an no empty obj
    return obj;
}

static void return_obj_to_slab(uintptr_t obj, slab_t *slab, mem_cache_t *slot) {
    ASSERT(obj && slab && slot);
    
    // compute obj index
    uint32_t obj_off = obj - slab->buff;
    obj_off >>= slab->obj_order;
    // obj_off /= slab->obj_size;

    ASSERT(slab->obj_list[obj_off] == obj_removed);

    slab->obj_list[obj_off] = slab->free;

    slab->free = obj_off;

    slab->free_objs++;

    // from partial list to empty list
    if (slab->free_objs == slab->total_objs) {
        list_erase(&slot->partial, &slab->tag);
        list_push_back(&slot->empty, &slab->tag);
    }

    // from full list to partial list
    if (slab->free_objs == 1) {
        list_erase(&slot->full, &slab->tag);
        list_push_back(&slot->partial, &slab->tag);
    }

    try_free_empty_slabs(slot, NORMAL_FREE);

}


uintptr_t slab_alloc(uint32_t n) {
    if (n == 0)
        return 0;

    if (!is_pow_of_2(n))
        n = next_pow_of_2(n);
    
    if (n < slab_obj_min_size)
        n = slab_obj_min_size;
    
    uint32_t index = size2index(n) - slab_min_shift;
    mem_cache_t *slot = mcached + index;

    slab_t *slab;

retry:

    if (!list_empty(&slot->partial))
        slab = tag2slab(slot->partial.head.next);

    else if (!list_empty(&slot->empty))
        slab = tag2slab(slot->empty.head.next);

    else {
        if (!fill_empty_slot(index))
            goto retry;
        else
            goto failed;
    }

    return fetch_obj_from_slab(slot, slab);

failed:
    return 0;
}



void slab_free(uintptr_t addr) {
    if (!addr)
        return;

    page_t *page = (page_t*)kvaddr2page(addr);
    slab_t *slab = page->slab;
    
    uint32_t index = slab->obj_order - slab_min_shift;

    mem_cache_t *slot = mcached + index;

    ASSERT(page_slab(page));
    ASSERT(slab->free_objs < slab->total_objs);

    return_obj_to_slab(addr, slab, slot);    
}


static void test_slab(void) {
    // basic alloc and free
    // slab analyse
    /*
    obj sizes <= 256, slab_t in slab's head;
    sizeof(slab_t) == 32b, so we list num objs in one slab
  -------------------------------
    size    | objs  | alignobjs | header size
    16      | 256   | 203       | 840
    32      | 128   | 112       | 476
    64      | 64    | 59        | 264
    128     | 32    | 30        | 148
    256     | 32    | 31        | 152
    512     | 32    | 32        | 156   -> 256
    1024    | 16    | 16        | 92    -> 128
    2048    | 8     | 8         | 64    -> 64
    4096    | 4     | 4         | 44    -> 64
    8192    | 4     | 4         | 44    -> 64
  --------------------------------
    先判断空间obj数量
*/
    uint32_t objs[] = {203, 112, 59, 30, 31, 32, 16, 8, 4, 4};
    uint32_t ac_objs[] = {0, 0, 3, 1, 1, 0, 0, 0, 0, 0};
    uintptr_t obj_ptr[num_cached];

    for (int i=0; i<num_cached; ++i) {
        obj_ptr[i] = slab_alloc(slab_obj_min_size << i);
        // print_str_int("slab_alloc:", obj_ptr[i]);
    }

    for (int i=0; i<num_cached; ++i) {
        mem_cache_t *slot = mcached + i;
        ASSERT(!list_empty(&slot->partial));
        slab_t *slab = kvaddr2page(obj_ptr[i])->slab;
        ASSERT(slab->total_objs == objs[i]);
    }

    for (int i=0; i<num_cached; ++i) {
        slab_t *slab = kvaddr2page(obj_ptr[i])->slab;
        slab_free(obj_ptr[i]);
        ASSERT(ac_objs[i] + slab->free_objs == slab->total_objs);
    }

    for (int j=0; j<num_cached; ++j) {
        for (int i=0; i<num_cached; ++i)
            obj_ptr[i] = slab_alloc(slab_obj_min_size << j);
        for (int i=num_cached-1; i>=0; --i)
            slab_free(obj_ptr[i]);
    }
    
    for (int i=0; i<num_cached; ++i) {
        obj_ptr[i] = slab_alloc(slab_obj_min_size << i);
        slab_free(obj_ptr[i]);
    }

    ac_objs[2] = 8;
    for (int i=0; i<num_cached; ++i) {
        slab_t *slab = kvaddr2page(obj_ptr[i])->slab;
        // print_str_int("slab->free_objs:", slab->free_objs);

        if (slab->obj_order >= 6 && slab->obj_order <= 8) {
            ASSERT(ac_objs[i] + slab->free_objs == slab->total_objs);
        }
        else {
            ASSERT(slab->free_objs == slab->total_objs);
        }
    }
}


void slab_init(void) {
    // set all mcached list empty
    mem_cache_t *cache;
    for (int i=0; i<num_cached; ++i) {
        cache = mcached + i;

        list_init(&cache->empty);
        list_init(&cache->full);
        list_init(&cache->partial);
        
        cache->obj_order = slab_min_shift + i;
    }

    // test slab
    test_slab();
}


inline void slab_try_release_cache(void) {
    for (uint32_t i=0; i<num_cached; ++i) {
        try_free_empty_slabs(mcached + i, SOFT_FREE); 
    }
}

inline void slab_release_cache(void) {
    for (uint32_t i=0; i<num_cached; ++i) {
        try_free_empty_slabs(mcached + i, FORCE_FREE); 
    }
}
