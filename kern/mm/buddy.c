#include <types.h>
#include <string.h>
#include <buddy.h>
#include <assert.h>
#include <pmm.h>

#define DEBUG 0 

#if DEBUG
    #define dprintf(f_, ...) printf((f_), ##__VA_ARGS__)
#else
    #define dprintf(f_, ...)
#endif

#define buddy_parent(index) (((index) - 1) / 2)
#define buddy_left(index)   ((index) * 2 + 1)
#define buddy_right(index)  ((index) * 2 + 2)
#define buddy_elem(bd_ptr, index)   ((buddy_elem_t*)(bd_ptr)->buff + index)


void dprintf_buff(buddy_t *bd) {
    int total_elem = bd->size * 2 - 1;
    buddy_elem_t *cur_elem;
    for (int i=0; i<total_elem; ++i) {
        cur_elem = buddy_elem(bd, i);
        dprintf("%d ", cur_elem->elem_size);
    }
    dprintf("\n");
}

// return binary tree level for the given index, start from 0
static int index2level(int index) {
    ++index;
    int cur = 0;
    while (index) {
        index >>= 1;
        ++cur;
    }
    return cur - 1;
}

// return reverse level by input level_size, start form 0
static int size2level(int size) {
    int res = 0;
    for (; size; ++res)
        size >>= 1;
    return res - 1;
}

static void buddy_check(buddy_t *bd);

void buddy_init(buddy_t *bd, uint32_t size, uintptr_t bd_buff) {
    // assert size is pow of 2
    ASSERT(is_pow_of_2(size));
    
    int total_elem = size * 2 - 1;

    // for phy mem pool, should be specific addr
    // int pages = total_len / PAGE_SIZE;   
    void *addr_st = NULL;
    // addr_st = malloc(total_elem * sizeof(buddy_elem_t));
    addr_st = (void*)bd_buff;
    // void *addr_ed = (uint8_t*)addr_st + pages * PAGE_SIZE;
    
    // init binary tree
    bd->buff = addr_st;
    bd->size = size;
    bd->level = size2level(size);

    int level_size = size, level_len = 1;
    int cur_level_index = 0;
    buddy_elem_t *cur_elem = NULL;
    for (int i=0; i<total_elem; ++i) {
        cur_elem = buddy_elem(bd, i);
        cur_elem->elem_size = level_size;
        ++cur_level_index;
        if (cur_level_index == level_len) {
            cur_level_index = 0;
            level_len <<= 1;
            level_size >>= 1;
        }
    }

    buddy_check(bd);
}


static void fix_parent_buddy_size(buddy_t *bd, int index) {
    if (index == 0)
        return;

    int parent = buddy_parent(index);
    buddy_elem_t *left, *right, *elem;

    while (parent >= 0) {
        elem = buddy_elem(bd, parent);
        left = buddy_elem(bd, buddy_left(parent));
        right = buddy_elem(bd, buddy_right(parent));
        
        if (left->elem_size != right->elem_size)
            elem->elem_size = left->elem_size > right->elem_size ? 
                                        left->elem_size : right->elem_size;
        else
            elem->elem_size = left->elem_size << 1;
        
        if (parent == 0)
            break;
        parent = buddy_parent(parent);
    }
}

// private func to find proper buddy slot in a sub tree
static int find_buddy_slot(buddy_t *bd, int pages) {
    buddy_elem_t *elem, *left_elem, *right_elem;
    int index = 0;
    const int level = bd->level - size2level(pages);
    int clevel = 0;

    for (;;) {
        elem = buddy_elem(bd, index);
        if (elem->elem_size == pages && clevel == level) {
            elem->elem_size = 0;
            fix_parent_buddy_size(bd, index);
            return index;
        }

        if (elem->elem_size >= pages) {
            ++clevel;
            left_elem = buddy_elem(bd, buddy_left(index));
            if (left_elem->elem_size >= pages) {
                index = buddy_left(index);
                continue;
            }

            right_elem = buddy_elem(bd, buddy_right(index));
            if (right_elem->elem_size >= pages) {
                index = buddy_right(index);
                continue;
            }
        }
        else {
            return -1;
        }
    }
}

static buddy_elem_t *buddy_alloc_elem(buddy_t *bd, uint32_t pages) {
    ASSERT(bd);

    if (pages == 0)
        return NULL;
    int res_index;

    pages = next_pow_of_2(pages);
    if ((res_index = find_buddy_slot(bd, pages)) > 0) {
        return buddy_elem(bd, res_index);
    }
    else 
        return NULL;
}


static void buddy_free_elem(buddy_t *bd, buddy_elem_t *ptr, uint32_t pages) {
    ASSERT(bd && ptr);
    // ASSERT(is_pow_of_2(pages));

    int page_align = next_pow_of_2(pages);
    int index = 0;

    if (ptr->elem_size != 0) {
        ASSERT(ptr->elem_size == page_align);
        return;
    }

    ptr->elem_size = page_align;
    index = ptr - (buddy_elem_t*)bd->buff;
    fix_parent_buddy_size(bd, index);
    return;
}


// return page index
int buddy_alloc_index(buddy_t *bd, uint32_t pages) {
    ASSERT(bd);

    buddy_elem_t *elem = buddy_alloc_elem(bd, pages);
    if (!elem)
        return -1;

    int index = elem - (buddy_elem_t*)bd->buff;
    int level = index2level(index);
    int level_size = 1 << (bd->level - level);
    int off_index = index - (1 << level) + 1;
    uint32_t res_addr = level_size * off_index;

    return res_addr;
}

// page_index
void buddy_free_index(buddy_t *bd, uint32_t page_index, uint32_t pages) {
    ASSERT(bd);

    pages = next_pow_of_2(pages);
    int rlevel = size2level(pages);

    int off_index = page_index >> rlevel;
    int index = (1 << (bd->level - rlevel)) - 1 + off_index;

    buddy_free_elem(bd, buddy_elem(bd, index), pages);

}


static void buddy_check(buddy_t *bd) {
    assert(bd);

    int res_index[10] = {0};
    
    int times = 5;
    for (int i=0; i<times; ++i) {
        res_index[i*2] = buddy_alloc_index(bd, 1);
        res_index[i*2+1] = buddy_alloc_index(bd, 1);
    }

    for (int i=0; i<times; ++i) {
        assert(res_index[i*2] == i*2);
        assert(res_index[i*2+1] == i*2+1);
    }

    for (int i=0; i<times; ++i) {
        buddy_free_index(bd, res_index[i*2], 1);
        buddy_free_index(bd, res_index[i*2+1], 1);
    }

    for (int i=0; i<times*2; ++i) {
        res_index[i] = buddy_alloc_index(bd, 1 << i);
    }

    for (int i=1; i<times*2; ++i) {
        assert(res_index[i] == 1 << i);
    }

    for (int i=0; i<times*2; ++i) {
        buddy_free_index(bd, res_index[i], 1 << i);
    }

    times = 3;

    for (int i=0; i<times; ++i) {
        res_index[3*i] = buddy_alloc_index(bd, 1);
        res_index[3*i+1] = buddy_alloc_index(bd, 2);
        res_index[3*i+2] = buddy_alloc_index(bd, 1);
    }

    for (int i=0; i<times; ++i) {
        assert(res_index[3*i] == 4*i);
        assert(res_index[3*i+1] == 4*i+2);
        assert(res_index[3*i+2] == 4*i+1);
    }

    for (int i=0; i<times; ++i) {
        buddy_free_index(bd, res_index[3*i], 1);
        buddy_free_index(bd, res_index[3*i+1], 2);
        buddy_free_index(bd, res_index[3*i+2], 1);
    }
    
    for (int i=0; i<times; ++i) {
        res_index[i*2] = buddy_alloc_index(bd, 1);
        res_index[i*2+1] = buddy_alloc_index(bd, 1);
    }

    for (int i=0; i<times; ++i) {
        assert(res_index[i*2] == i*2);
        assert(res_index[i*2+1] == i*2+1);
    }

    for (int i=0; i<times; ++i) {
        buddy_free_index(bd, res_index[i*2], 1);
        buddy_free_index(bd, res_index[i*2+1], 1);
    }
}