/*
	print.c -- output functions and some useful utilities.
	Coiyright (C) 1996-2002 Hans Reiser
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <agl/agl.h>

#ifndef ENABLE_ALONE
#  include <stdio.h>
#endif

#ifndef ENABLE_ALONE
static agl_print_handler_t print_handler = (agl_print_handler_t)printf;
#else
static agl_print_handler_t print_handler = NULL;
#endif

void agl_print_set_handler(agl_print_handler_t handler) {
	print_handler = handler;
}

agl_print_handler_t agl_print_handler(void) {
	return print_handler;
}

void agl_print(const char *str) {
		
	if (!print_handler)
		return;
	
	print_handler(str);
}

