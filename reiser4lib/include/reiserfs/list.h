/*
	list.h -- simple list implementation
	Copyright (C) 1996 - 2002 Hans Reiser
*/

#ifndef LIST_H
#define LIST_H

struct list {
	void **body;
	int size, count, inc;
};

typedef struct list list_t;

extern list_t *list_create(int inc);
extern void list_free(list_t *list);

extern int list_expand(list_t *list);
extern int list_shrink(list_t *list);

extern void *list_at(list_t *list, int pos);
extern int list_pos(list_t *list, void *item);

extern int list_insert(list_t *list, void *item, int pos);
extern int list_delete(list_t *list, int pos);

extern int list_add(list_t *list, void *item);
extern int list_remove(list_t *list, void *item);

extern void *list_run(list_t *list, int (*item_func)(void *, void *), void *data);

#endif

