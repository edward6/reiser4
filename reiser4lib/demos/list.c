/*
    list.c -- small list example.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>

int main(int argc, char *argv[]) {
    int i;
    aal_list_t *list;

    if (!(list = aal_list_create(10)))
    	return 1;
	
    aal_list_add(list, "First test line");
    aal_list_add(list, "Second test line");
    aal_list_add(list, "Third test line");
	
    for (i = 0; i < aal_list_count(list); i++)
    	aal_printf("%s\n", aal_list_at(list, i));
	
    aal_list_free(list);
    return 0;
}

