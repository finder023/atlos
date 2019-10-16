#ifndef __L_SLAB_H
#define __L_SLAB_H

#include <lib/list.h>
#include <types.h>


// 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192
#define slab_min_shift      4 
#define num_cached          10
#define slab_obj_min_size   (1 << slab_min_shift)

#define slab_t_in_slab_index (8 - slab_min_shift)

#define slab_max_shift  (slab_min_shift + num_cached - 1)

typedef struct slab {
    uint32_t    free;
    uint32_t    obj_order;
    uint32_t    total_objs;
    uint32_t    free_objs;
    list_elem_t tag;
    uintptr_t   buff;
    uint32_t    obj_list[1];
} slab_t;


typedef struct mem_cache {
    uint32_t    obj_order;
    list_t      full;
    list_t      empty;
    list_t      partial; 
} mem_cache_t;


#define tag2slab(addr) (elem2entry(slab_t, tag, addr))

void slab_init(void);

uintptr_t slab_alloc(uint32_t n);

void slab_free(uintptr_t addr);

// try to release empty cache, do NOT call this group func easily
// try release will keep at least half of the empty slab
void slab_try_release_cache(void);

// release all the empty slab, actually, should never call it.
void slab_release_cache(void);

#endif