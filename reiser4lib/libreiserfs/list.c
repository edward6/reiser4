/*
	list.c -- simple list implementation.
	Copyright (C) 1996-2002 Hans Reiser
*/

#include <agl/agl.h>
#include <reiserfs/reiserfs.h>

#define ptr_by_pos(base, pos) (base + pos)
#define size_by_count(count) (count * sizeof(void *))

list_t *list_create(int inc) {
	list_t *list;

	if (!(list = agl_calloc(sizeof(list_t), 0)))
		return NULL;
	
	list->count = 0;
	list->size = 0;
	list->inc = inc;
	
	if (!list_expand(list))
		goto error_free_list;
	
	return list;
	
error_free_list:
	agl_free(list);
error:
	return NULL;
}

void list_free(list_t *list) {
	agl_free(list->body);
	agl_free(list);
}

int list_expand(list_t *list) {
	int i;
	
	if (list->count < list->size)
		return 1;

	if (!agl_realloc((void **)&list->body, size_by_count(list->size + list->inc)))
		return 0;
	
	for (i = list->count; i < list->count + list->inc; i++)
		*ptr_by_pos(list->body, i) = NULL;
			
	list->size += list->inc;
	
	return 1;
}

int list_shrink(list_t *list) {
	
	if (list->size - list->count < list->inc)
		return 1;
	
	if (!agl_realloc((void *)&list->body, size_by_count(list->size - list->inc)))
		return 0;
		
	list->size -= list->inc;
	
	return 1;
}

void *list_at(list_t *list, int pos) {
	return *ptr_by_pos(list->body, pos);
}

int list_pos(list_t *list, void *item) {
	int i;
	
	for (i = 0; i < list->count; i++)
		if (*ptr_by_pos(list->body, i) == item) return i;
	
	return -1;
}

int list_insert(list_t *list, void *item, int pos) {
	list_expand(list);
	
	if (pos > list->count)
		return 0;
	
	if (pos < list->count) {
		int i;
		for (i = list->count - 1; i >= pos; i--)
			*ptr_by_pos(list->body, i + 1) = *ptr_by_pos(list->body, i);
	}	
	
	*ptr_by_pos(list->body, pos) = item;
	list->count++;

	return 1;
}

int list_delete(list_t *list, int pos) {
	
	if (pos < list->count) {
		int i;
		for (i = pos; i < list->count; i++)
			*ptr_by_pos(list->body, i) = *ptr_by_pos(list->body, i + 1);
	}
	
	list->count--;
	
	list_shrink(list);
	
	return 1;
}

int list_add(list_t *list, void *item) {
	return list_insert(list, item, list->count);
}

int list_remove(list_t *list, void *item) {
	int pos;
	
	if ((pos = list_pos(list, item)) == -1)
		return 0;

	return list_delete(list, pos);
}

void *list_run(list_t *list, int (*item_func)(void *, void *), void *data) {
	int i;
	
	if (!item_func)
		return NULL;
		
	for (i = 0; i < list->count; i++) {
		void *item = list_at(list, i);
		if (item_func(item, data))
			return item;
	}
	return NULL;
}

int list_count(list_t *list) {
	return list->count;
}

