/*
    list.c -- small list example.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>

int main(int argc, char *argv[]) {
    aal_list_t *list = NULL;
    aal_list_t *walk = NULL;

    list = aal_list_append(list, "First test line");
    aal_list_append(list, "Second test line");
    aal_list_append(list, "Third test line");
    
    aal_list_remove(list, "Second test line");
    
    aal_list_for_each_forward(walk, list)
    	aal_printf("%s\n", walk->data);
	
    aal_list_free(list);
    return 0;
}

