/*
    list.h -- double-linked list implementation. It is used for plugins cashe.
    Copyright (C) 1996 - 2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef LIST_H
#define LIST_H

typedef struct aal_list aal_list_t;

struct aal_list {
    void *data;
    
    aal_list_t *next;
    aal_list_t *prev;
};

extern aal_list_t *aal_list_alloc(void *data);
extern aal_list_t *aal_list_last(aal_list_t *list);
extern aal_list_t *aal_list_first(aal_list_t *list);

extern aal_list_t *aal_list_next(aal_list_t *list);
extern aal_list_t *aal_list_prev(aal_list_t *list);

extern uint32_t aal_list_length(aal_list_t *list);

extern int aal_list_foreach(aal_list_t *list, 
    foreach_func_t func, void *data);

extern int32_t aal_list_pos(aal_list_t *list, void *data);
extern aal_list_t *aal_list_at(aal_list_t *list, uint32_t n);

extern aal_list_t *aal_list_insert(aal_list_t *list, 
    void *data, uint32_t n);

extern aal_list_t *aal_list_insert_sorted(aal_list_t *list,
    void *data, comp_func_t com_func);
    
extern aal_list_t *aal_list_prepend(aal_list_t *list, void *data);
extern aal_list_t *aal_list_append(aal_list_t *list, void *data);
extern void aal_list_remove(aal_list_t *list, void *data);
extern aal_list_t *aal_list_find(aal_list_t *list, void *data);

extern aal_list_t *aal_list_find_custom(aal_list_t *list, void *data, 
    comp_func_t comp_func);

extern aal_list_t *aal_list_bin_search(aal_list_t *list, void *data, 
    comp_func_t comp_func);

extern void aal_list_free(aal_list_t *list);

#define aal_list_foreach_forward(walk, list) \
    for (walk = aal_list_first(list); walk; walk = aal_list_next(walk))

#define aal_list_foreach_backward(walk, list) \
    for (walk = aal_list_last(list); walk; walk = aal_list_prev(walk))
    
#endif

