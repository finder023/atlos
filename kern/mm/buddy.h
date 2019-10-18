#ifndef __L_BUDDY_H
#define __L_BUDDY_H

#include <types.h>
#include <pmm.h>

/*
更加简化的buddy系统，使用完全二叉树对节点进行组织
*/


typedef struct buddy {
    void *buff;
    void *st_addr;
    int size;
    int level;
} buddy_t;

typedef struct buddy_elem {
    int elem_size;
} buddy_elem_t;

void dprintf_buff(buddy_t*);

void buddy_init(buddy_t *, uint32_t size, uintptr_t buff_addr);

int buddy_alloc_index(buddy_t *, uint32_t pages);
void buddy_free_index(buddy_t *, uint32_t pindex, uint32_t pages);

#endif