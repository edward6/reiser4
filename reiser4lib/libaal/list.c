/*
	list.c -- simple list implementation.
	Copyright (C) 1996-2002 Hans Reiser
*/

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>

#define ptr_by_pos(base, pos) (base + pos)
#define size_by_count(count) (count * sizeof(void *))

aal_list_t *aal_list_create(int inc) {
	aal_list_t *list;

	if (!(list = aal_calloc(sizeof(aal_list_t), 0)))
		return NULL;
	
	list->count = 0;
	list->size = 0;
	list->inc = inc;
	list->body = NULL;
	
	if (!aal_list_expand(list))
		goto error_free_list;
	
	return list;
	
error_free_list:
	aal_free(list);
error:
	return NULL;
}

void aal_list_free(aal_list_t *list) {
	if (list->body)
		aal_free(list->body);
	
	aal_free(list);
}

int aal_list_expand(aal_list_t *list) {
	int i;
	
	if (list->count < list->size)
		return 1;

	if (!aal_realloc((void **)&list->body, size_by_count(list->size + list->inc)))
		return 0;
	
	for (i = list->count; i < list->count + list->inc; i++)
		*ptr_by_pos(list->body, i) = NULL;
			
	list->size += list->inc;
	
	return 1;
}

int aal_list_shrink(aal_list_t *list) {
	
	if (list->size - list->count < list->inc)
		return 1;
	
	if (!aal_realloc((void *)&list->body, size_by_count(list->size - list->inc)))
		return 0;
		
	list->size -= list->inc;
	
	return 1;
}

void *aal_list_at(aal_list_t *list, int pos) {
	return *ptr_by_pos(list->body, pos);
}

int aal_list_pos(aal_list_t *list, void *item) {
	int i;
	
	for (i = 0; i < list->count; i++)
		if (*ptr_by_pos(list->body, i) == item) return i;
	
	return -1;
}

int aal_list_insert(aal_list_t *list, void *item, int pos) {
	aal_list_expand(list);
	
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

int aal_list_delete(aal_list_t *list, int pos) {
	
	if (pos < list->count) {
		int i;
		for (i = pos; i < list->count; i++)
			*ptr_by_pos(list->body, i) = *ptr_by_pos(list->body, i + 1);
	}
	
	list->count--;
	aal_list_shrink(list);
	
	return 1;
}

int aal_list_add(aal_list_t *list, void *item) {
	return aal_list_insert(list, item, list->count);
}

int aal_list_remove(aal_list_t *list, void *item) {
	int pos;
	
	if ((pos = aal_list_pos(list, item)) == -1)
		return 0;

	return aal_list_delete(list, pos);
}

void *aal_list_run(aal_list_t *list, int (*item_func)(void *, void *), void *data) {
	int i;
	
	if (!item_func)
		return NULL;
		
	for (i = 0; i < list->count; i++) {
		void *item = aal_list_at(list, i);
		if (item_func(item, data))
			return item;
	}
	return NULL;
}

int aal_list_count(aal_list_t *list) {
	return list->count;
}

