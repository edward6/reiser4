/*
	exception.c -- exceptions implementation.
	Copyright (C) 1996-2002 Hans Reiser.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <agl/agl.h>

#ifndef ENABLE_ALONE
#  include <stdio.h>
#  include <stdarg.h>
#  include <string.h>
#endif

static agl_exception_option_t default_handler(agl_exception_t *exception);
static agl_exception_handler_t exception_handler = default_handler;

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

static int agl_log2(int n) {
	int x;
	for (x = 0; 1 << x <= n; x++);
		return x - 1;
}

char *agl_exception_type_string(agl_exception_type_t type) {
	return type_strings[type - 1];
}

agl_exception_type_t agl_exception_type(agl_exception_t *exception) {
	return exception->type;
}

char *agl_exception_option_string(agl_exception_option_t opt) {
	return option_strings[agl_log2(opt)];
}

agl_exception_option_t agl_exception_option(agl_exception_t *exception) {
	return exception->options;
}

char *agl_exception_message(agl_exception_t *exception) {
	return exception->message;
}

char *agl_exception_hint(agl_exception_t *exception) {
	return exception->hint;
}

static agl_exception_option_t default_handler(agl_exception_t *exception) {
	if (exception->type != EXCEPTION_BUG){
		agl_print(agl_exception_type_string(exception->type));
		agl_print(": ");
		agl_print(agl_exception_hint(exception));
		agl_print(": ");
	}
	agl_print(exception->message);
	agl_print("\n");

	switch (exception->options) {
	    case EXCEPTION_OK:
	    case EXCEPTION_CANCEL:
	    case EXCEPTION_IGNORE:
	        return exception->options;
	    
	    default:
	        return EXCEPTION_UNHANDLED;
	}
}

void agl_exception_set_handler(agl_exception_handler_t handler) {
	exception_handler = handler ? handler : default_handler;
}

void agl_exception_catch(agl_exception_t *exception) {
	
	if (!exception)	
		return;
	
	agl_free(exception->message);
	agl_free(exception->hint);
	agl_free(exception);
}

static agl_exception_option_t agl_exception_actual_throw(agl_exception_t *exception) {
	agl_exception_option_t opt;

	if (fetch_count)
		return EXCEPTION_UNHANDLED;
	
	opt = exception_handler(exception);
	agl_exception_catch(exception);
	return opt;
}

agl_exception_option_t agl_exception_throw(agl_exception_type_t type,
	agl_exception_option_t opts, const char *hint, const char *message, ...)
{
	va_list arg_list;
	agl_exception_t *exception;

	if (!(exception = (agl_exception_t *)agl_malloc(sizeof(agl_exception_t))))
		goto no_memory;

	if (!(exception->message = (char*)agl_malloc(4096)))
		goto no_memory;

	if (!(exception->hint = (char*)agl_malloc(4096)))
		goto no_memory;
	
	exception->type = type;
	exception->options = opts;

	/* This call should be replaced by someone worked in alone maner */
	agl_strncpy(exception->hint, hint, 4096);

#ifndef ENABLE_ALONE
	va_start(arg_list, message);
	vsnprintf(exception->message, 4096, message, arg_list);
	va_end(arg_list);
#else
	agl_strncpy(exception->message, message, 4096);
#endif
	return agl_exception_actual_throw(exception);

no_memory:
	agl_print("Out of memory in exception handler!\n");
	return EXCEPTION_UNHANDLED;
}

void agl_exception_fetch_all(void) {
	fetch_count++;
}

void agl_exception_leave_all(void) {
	if (fetch_count > 0)
		fetch_count--;
}

