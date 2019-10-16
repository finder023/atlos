#ifndef __L_LIST_H
#define __L_LIST_H

#include <types.h>

/*
TODO:
1, struct list_tag
2, 接口： list_create, list_push_back, list_push_front, 
list_pop_back, list_pop_front, list_remove, list_insert
*/

#define offset(struct_type,member) (uint32_t)(&((struct_type*)0)->member)
#define elem2entry(struct_type, struct_member_name, elem_ptr) \
	 (struct_type*)((uint8_t*)elem_ptr - offset(struct_type, struct_member_name))

typedef struct list_elem {
    struct list_elem *prev;
    struct list_elem *next;
} list_elem_t;

typedef struct list {
    struct list_elem head;
    struct list_elem tail;
    uint32_t len;
} list_t;

typedef bool (function)(struct list_elem*, void *arg);

void list_elem_init(list_elem_t *elem);

void list_init(struct list *);

list_elem_t *list_replace(list_elem_t *pos, list_elem_t *nelem);

bool list_empty(struct list *);

list_elem_t *list_front(list_t *);

list_elem_t *list_back(list_t *);

void list_insert(struct list*, struct list_elem*, struct list_elem*);

void list_insert_after(list_t *, list_elem_t *, list_elem_t *);

void list_insert_before(list_t *, list_elem_t *, list_elem_t *);

void list_push_back(struct list *, struct list_elem *);

void list_push_front(struct list*, struct list_elem*);

struct list_elem * list_erase(struct list*, struct list_elem*);

struct list_elem *list_pop_back(struct list*);

struct list_elem *list_pop_front(struct list*);

bool list_elem_find(struct list*, struct list_elem*);

struct list_elem *list_traversal(struct list *l, function func, void *arg);



#endif