/*
	list.c -- small list example.
	Copyright (C) 1996-2002 Hans Reiser.
*/

#include <stdio.h>
#include <reiserfs/reiserfs.h>

int main(int argc, char *argv[]) {
	int i;
	list_t *list;

	if (!(list = list_create(10)))
		return 1;
	
	list_add(list, "First test line");
	list_add(list, "Second test line");
	list_add(list, "Third test line");
	
	for (i = 0; i < list_count(list); i++)
		fprintf(stderr, "%s\n", list_at(list, i));
	
	list_free(list);
	return 0;
}

