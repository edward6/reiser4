/*
	exception.c -- exceptions implementation.
	Copyright (C) 1996-2002 Hans Reiser.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <aal/aal.h>
#include <stdarg.h>

#ifndef ENABLE_ALONE
#  include <stdio.h>
#  include <string.h>
#endif

static aal_exception_option_t default_handler(aal_exception_t *exception);
static aal_exception_handler_t exception_handler = default_handler;

static char *type_strings[] = {
	"Information", 
	"Warning", 
	"Error", 
	"Fatal", 
	"Bug"
};

static char *option_strings[] = {
	"Yes", 
	"No", 
	"OK", 
	"Retry", 
	"Ignore", 
	"Cancel"
};

static int fetch_count = 0;

static int aal_log2(int n) {
	int x;
	for (x = 0; 1 << x <= n; x++);
		return x - 1;
}

char *aal_exception_type_string(aal_exception_type_t type) {
	return type_strings[type - 1];
}

aal_exception_type_t aal_exception_type(aal_exception_t *exception) {
	return exception->type;
}

char *aal_exception_option_string(aal_exception_option_t opt) {
	return option_strings[aal_log2(opt)];
}

aal_exception_option_t aal_exception_option(aal_exception_t *exception) {
	return exception->options;
}

char *aal_exception_message(aal_exception_t *exception) {
	return exception->message;
}

char *aal_exception_hint(aal_exception_t *exception) {
	return exception->hint;
}

static aal_exception_option_t default_handler(aal_exception_t *exception) {
	if (exception->type != EXCEPTION_BUG){
		aal_printf("%s: %s: ", aal_exception_type_string(exception->type),
			aal_exception_hint(exception));
	}
	aal_printf("%s\n", exception->message);

	switch (exception->options) {
	    case EXCEPTION_OK:
	    case EXCEPTION_CANCEL:
	    case EXCEPTION_IGNORE:
	        return exception->options;
	    
	    default:
	        return EXCEPTION_UNHANDLED;
	}
}

void aal_exception_set_handler(aal_exception_handler_t handler) {
	exception_handler = handler ? handler : default_handler;
}

void aal_exception_catch(aal_exception_t *exception) {
	
	if (!exception)	
		return;
	
	aal_free(exception->message);
	aal_free(exception->hint);
	aal_free(exception);
}

static aal_exception_option_t aal_exception_actual_throw(aal_exception_t *exception) {
	aal_exception_option_t opt;

	if (fetch_count)
		return EXCEPTION_UNHANDLED;
	
	opt = exception_handler(exception);
	aal_exception_catch(exception);
	return opt;
}

aal_exception_option_t aal_exception_throw(aal_exception_type_t type,
	aal_exception_option_t opts, const char *hint, const char *message, ...)
{
	va_list arg_list;
	aal_exception_t *exception;

	if (!(exception = (aal_exception_t *)aal_malloc(sizeof(aal_exception_t))))
		goto no_memory;

	if (!(exception->message = (char*)aal_malloc(4096)))
		goto no_memory;

	if (!(exception->hint = (char*)aal_malloc(255)))
		goto no_memory;
	
	exception->type = type;
	exception->options = opts;

	aal_strncpy(exception->hint, hint, 255);

	va_start(arg_list, message);
	aal_vsnprintf(exception->message, 4096, message, arg_list);
	va_end(arg_list);
	
	return aal_exception_actual_throw(exception);
no_memory:
	aal_printf("Out of memory in exception handler!\n");
	return EXCEPTION_UNHANDLED;
}

void aal_exception_fetch_all(void) {
	fetch_count++;
}

void aal_exception_leave_all(void) {
	if (fetch_count > 0)
		fetch_count--;
}

