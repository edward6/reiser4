/*
    list.h -- simple list implementation. It is used for plugins cashe.
    Copyright (C) 1996 - 2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef LIST_H
#define LIST_H

struct aal_list {
    void **body;
    int size, count, inc;
};

typedef struct aal_list aal_list_t;

extern aal_list_t *aal_list_create(int inc);
extern void aal_list_free(aal_list_t *list);

extern int aal_list_expand(aal_list_t *list);
extern int aal_list_shrink(aal_list_t *list);

extern void *aal_list_at(aal_list_t *list, int pos);
extern int aal_list_pos(aal_list_t *list, void *item);

extern int aal_list_insert(aal_list_t *list, void *item, int pos);
extern int aal_list_delete(aal_list_t *list, int pos);

extern int aal_list_add(aal_list_t *list, void *item);
extern int aal_list_remove(aal_list_t *list, void *item);

extern void *aal_list_run(aal_list_t *list, int (*item_func)(void *, void *), void *data);
extern int aal_list_count(aal_list_t *list);

#endif

