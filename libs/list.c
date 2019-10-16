#include <list.h>

inline void list_elem_init(list_elem_t *elem) {
    if (!elem)  return;
    elem->prev = elem->next = NULL;
}

inline bool list_empty(struct list *l) {
    return l->head.next == &l->tail;
}

void list_init(struct list * l) {
    if (!l) return;
    l->head.next = &l->tail;
    l->head.prev = NULL;
    l->tail.next = NULL;
    l->tail.prev = &l->head;
    l->len = 0;
}

inline list_elem_t *list_front(list_t *l) {
    if (list_empty(l))
        return NULL;
    return l->head.next;
}

inline list_elem_t *list_back(list_t *l) {
    if (list_empty(l))
        return NULL;
    return l->tail.prev;
}

void list_insert(struct list *l, struct list_elem *elem, struct list_elem *new_elem) {
    //enum intr_status old_status = intr_disable();
    if (!l || !elem || !new_elem)
        return;
    new_elem->next = elem->next;
    new_elem->prev = elem;
    elem->next->prev = new_elem;
    elem->next = new_elem;
    l->len++;
    //set_intr_status(old_status);
}

inline void list_insert_after(list_t *l, list_elem_t *p, list_elem_t *n) {
    list_insert(l, p, n);
}

inline void list_insert_before(list_t *l, list_elem_t *p, list_elem_t *n) {
    list_insert(l, p->prev, n);
}

inline void list_push_back(struct list *l, struct list_elem *elem) {
    list_insert(l, l->tail.prev, elem);
}

inline void list_push_front(struct list *l, struct list_elem *elem) {
    list_insert(l, &l->head, elem);
}

struct list_elem *list_erase(struct list *l, struct list_elem *elem) {
    //enum intr_status old_status = intr_disable();
    if (!l || !elem)
        return NULL;
    
    if (elem == &l->head || elem == &l->tail)
        return NULL;
    
    elem->prev->next = elem->next;
    elem->next->prev = elem->prev;
    l->len--;
    //set_intr_status(old_status);
    return elem;
}

list_elem_t *list_replace(list_elem_t *pos, list_elem_t *nelem) {
    if (!pos || !nelem)
        return NULL;
    nelem->next = pos->next;
    nelem->prev = pos->prev;
    pos->next->prev = nelem;
    pos->prev->next = nelem;
    return nelem;
}


inline struct list_elem *list_pop_back(struct list* l) {
    return list_erase(l, l->tail.prev);
}

inline struct list_elem *list_pop_front(struct list *l) {
    return list_erase(l, l->head.next);
}



bool list_elem_find(struct list *l, struct list_elem *elem) {
    struct list_elem *it;
    if (!l || !elem)
        return false;
    
    for (it = l->head.next; it != &l->tail; it = it->next) {
        if (it == elem)
            return true;
    }
    return false;
}

struct list_elem *list_traversal(struct list *l, function func, void *arg) {
    struct list_elem *elem = NULL;
    if (!l || list_empty(l)) return elem;

    for (elem = l->head.next; elem != &l->tail; elem = elem->next) {
        if (func(elem, arg))
            return elem;
    }
    return NULL;
}
