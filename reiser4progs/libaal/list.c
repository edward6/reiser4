/*
    list.c -- simple list implementation.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <aal/aal.h>

aal_list_t *aal_list_alloc(void *data) {
    aal_list_t *list;
   
    if (!(list  = (aal_list_t *)aal_calloc(sizeof(*list), 0)))
	return NULL;
    
    list->data = data;
    list->next = NULL;
    list->prev = NULL;
    return list;
}

aal_list_t *aal_list_last(aal_list_t *list) {
    while (list->next)
	list = list->next;

    return list;
}

aal_list_t *aal_list_first(aal_list_t *list) {
    while (list->prev)
	list = list->prev;

    return list;
}

aal_list_t *aal_list_next(aal_list_t *list) {
    return list->next;
}

aal_list_t *aal_list_prev(aal_list_t *list) {
    return list->prev;
}

uint32_t aal_list_length(aal_list_t *list) {
    uint32_t length = 0;

    while (list) {
	length++;
	list = list->next;
    }
    return length;
}

int aal_list_foreach(aal_list_t *list, foreach_func_t func, void *data) {

    if (!func)
	return -1;

    while (list) {
	int res;
	
	if ((res = func(list->data, data)))
	    return res;
	
	list = list->next;
    }
    return 0;
}

int32_t aal_list_pos(aal_list_t *list, void *data) {
    int32_t pos = 0;

    while (list) {
	if (list->data == data)
	    return pos;
	pos++;
	list = list->next;
    }
    return -1;
}

aal_list_t *aal_list_at(aal_list_t *list, uint32_t n) {
    while ((n-- > 0) && list)
	list = list->next;

    return list;
}

aal_list_t *aal_list_insert(aal_list_t *list, void *data, uint32_t n) {
    aal_list_t *temp;
    aal_list_t *new;
    
    if (n == 0)
	return aal_list_prepend(list, data);
    
    if (!(temp = aal_list_at(list, n)))
	return aal_list_append(list, data);

    if (!(new = aal_list_alloc(data)))
	return NULL;
    
    if (temp->prev) {
	temp->prev->next = new;
	new->prev = temp->prev;
    }
    new->next = temp;
    temp->prev = new;

    return temp == list ? new : list;
}

aal_list_t *aal_list_insert_sorted(aal_list_t *list,
    void *data, comp_func_t func)
{
    aal_list_t *tmp_list = list;
    aal_list_t *new_list;
    int cmp;

    if (!list) {
	new_list = aal_list_alloc(data);
	return new_list;
    }
  
    cmp = func(data, tmp_list->data);
  
    while ((tmp_list->next) && (cmp > 0)) {
	tmp_list = tmp_list->next;
	cmp = func(data, tmp_list->data);
    }

    new_list = aal_list_alloc(data);

    if ((!tmp_list->next) && (cmp > 0)) {
	tmp_list->next = new_list;
	new_list->prev = tmp_list;
	return list;
    }
   
    if (tmp_list->prev) {
	tmp_list->prev->next = new_list;
	new_list->prev = tmp_list->prev;
    }
    new_list->next = tmp_list;
    tmp_list->prev = new_list;
 
    if (tmp_list == list)
	return new_list;
    else
	return list;
}

aal_list_t *aal_list_prepend(aal_list_t *list, void *data) {
    aal_list_t *new;
    aal_list_t *last;
    
    if (!(new = aal_list_alloc(data)))
	return 0;
    
    if (list) {
	if (list->prev) {
	    list->prev->next = new;
	    new->prev = list->prev;
	}
	list->prev = new;
	new->next = list;
    }
    
    return new;
}

aal_list_t *aal_list_append(aal_list_t *list, void *data) {
    aal_list_t *new;
    aal_list_t *last;
    
    if (!(new = aal_list_alloc(data)))
	return 0;
    
    if (list) {
	last = aal_list_last(list);
	last->next = new;
	new->prev = last;

	return list;
    } else
	return new;
}

void aal_list_remove(aal_list_t *list, void *data) {
    aal_list_t *temp = aal_list_find(list, data);
   
    if (temp) {
	if (temp->prev)
	    temp->prev->next = temp->next;
	    
	if (temp->next)
	    temp->next->prev = temp->prev;
	    
	aal_free(temp);
    }
}

aal_list_t *aal_list_find(aal_list_t *list, void *data) {
    while (list) {
	if (list->data == data)
	    return list;
	
	list = list->next;
    }
    return NULL;
}

aal_list_t *aal_list_find_custom(aal_list_t *list, void *data, comp_func_t func) {
	
    if (!func)
	return NULL;
    
    while (list) {
	if (func(list->data, data))
	    return list;

	list = list->next;
    }
    return NULL;
}

void aal_list_free(aal_list_t *list) {
    aal_list_t *last = list;
    
    while (last->next) {
	aal_list_t *temp = last->next;
	aal_free(last);
	last = temp;
    }
}

