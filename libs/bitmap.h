#ifndef __L_BITMAP_H
#define __L_BITMAP_H

#include <types.h>

#define BIT_SCAN_FAIL -1 

struct bitmap {
    uint32_t len;
    uint8_t *buff;
};

typedef struct bitmap bitmap_t;

void bitmap_init(struct bitmap *map);

int bitmap_set(struct bitmap *map, uint32_t index);

int bitmap_remove(struct bitmap *map, uint32_t index);

int bitmap_scan(struct bitmap *map, uint32_t cnt);

int bitmap_scan_partial(struct bitmap* , uint32_t, uint32_t);

void check_bitmap(void);

#endif