#include <bitmap.h>
#include <string.h>
#include <stdio.h>

inline int bitmap_set(struct bitmap *map, uint32_t index) {
    if (index >= map->len)
        return -1;
    uint8_t *char_addr = map->buff + (index >> 3);
    uint8_t mask = 0x1 << (index & 0x7);
    *char_addr |= mask;
    return 0;
}

inline int bitmap_remove(struct bitmap *map, uint32_t index) {
    if (index >= map->len)
        return -1;
    uint8_t *char_addr = map->buff + (index >> 3);
    uint8_t mask = 0xff ^ (0x1 << (index & 0x7));
    *char_addr &= mask;
    return 0;
}

#define BYTE_BIT(byte_data, index) (*byte_data & (0x1 << index))

/* 
bit map scan 这个算法比较棘手，先整理出一个思路吧，最后有时间的话替换掉
1，跳过已占用的位
2，对于未使用位进行长度检查，不符合则跳到新的占用位，重复1
3，若符合，则返回这些位
*/
// static uint32_t bit_map_scan_(struct bit_map *map, uint32_t cnt) {
//     uint32_t start_index = 0;
//     uint8_t *byte_addr = map->buff; 
//     uint8_t *end_buff = map->buff + map->len;
//     uint32_t i = 0;
//     //uint32_t byte_index = 0;
//     uint32_t empty_count = 0;
//     while (1) {
//         // 直接跳过整字节
//         for (; *byte_addr == 0xff; ++byte_addr);
//         // 跳过单个字节中的占用位
//         for (; i<8 && BYTE_BIT(byte_addr, i); ++i); 
//         start_index = ((byte_addr - map->buff) << 3) + i;
//         // 判断连续空闲位是否有cnt个
//         while (empty_count < cnt && byte_addr < end_buff && !BYTE_BIT(byte_addr, i)) {
//             ++empty_count;
//             if (++i >= 8) {
//                 i = 0;
//                 ++byte_addr;
//             }
//         }
// 
//         if (empty_count == cnt) {
//             for (i=0; i<cnt; ++i)
//                 bit_map_set(map, start_index+i);
//             return start_index;
//         }
//         // 查找失败
//         if (byte_addr >= end_buff)
//             return BIT_SCAN_FAIL;
//         // 当前空闲位比cnt少，跳过该字节
//         //++byte_addr;
//         empty_count = 0;
//     }
// }


int bitmap_scan_partial(struct bitmap *map, uint32_t cnt, uint32_t st) {
    uint8_t *end_buff;
    uint32_t empty_count = 0;
    uint32_t index = 0;

    typedef struct map_byte {
        uint8_t *byte;
        uint8_t off;
    } byte_t;

    byte_t cur_byte, st_byte;
    
    st_byte.off = st & 7;
    st_byte.byte = map->buff + (st >> 3);
    cur_byte = st_byte;
    end_buff = map->buff + map->len;

    while (1) {
        // byte full
        if (*cur_byte.byte == 0xff) {
            ++cur_byte.byte;
            cur_byte.off = 0;
            st_byte = cur_byte;
            empty_count = 0;
            continue;
        }

        while (empty_count < cnt && cur_byte.byte < end_buff) {
            if (!BYTE_BIT(cur_byte.byte, cur_byte.off)) {
                ++empty_count;

                if (empty_count == cnt) {
                    goto found;
                }
                ++cur_byte.off;
            } 
            else {
                ++cur_byte.off;
                st_byte = cur_byte;
            }

            if (cur_byte.off== 8) {
                cur_byte.off = 0;
                ++cur_byte.byte;
                st_byte = cur_byte;
                break;
            }
        }

        if (cur_byte.byte >= end_buff) 
            return BIT_SCAN_FAIL;
    }
   
found:
    index = (uint32_t)(st_byte.byte - map->buff) << 3;
    index += st_byte.off;

    for (int i=0; i<cnt; ++i) {
        bitmap_set(map, index + i);
    }
    return index;
}


inline void bitmap_init(struct bitmap *map) {
    memset((void*)map->buff, 0, map->len);
}

inline int bitmap_scan(struct bitmap *map, uint32_t cnt) {
    return bitmap_scan_partial(map, cnt, 0);
}

#define BITMAP_ASSERT(con) ({\
    if (!(con)) {\
        cprintf("bitmap assertion fail.\n"#con"\n");\
        while (1);\
    }\
})

void check_bitmap(void) {
    struct bitmap map;
    char buff[10];
    map.len = 10;
    map.buff = (void*)buff;
    
    bitmap_init(&map);

    int res[10] = {0};
    int size = 10;

    for (int i=0; i<size; ++i) {
        res[i] = bitmap_scan(&map, 1);
    }

    for (int i=0; i<size; ++i) {
        BITMAP_ASSERT(i == res[i]);
    }

    for (int i=0; i<size/2; ++i) {
        bitmap_remove(&map, res[i*2]);
    }

    int rres[10] = {0};
    for (int i=0; i<size/2; ++i) {
        rres[i] = bitmap_scan(&map, 1);
    }

    for (int i=0; i<size/2; ++i) {
        BITMAP_ASSERT(rres[i] == i*2);
    }

}

#undef BITMAP_ASSERT



