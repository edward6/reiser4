/*
	exception.c -- libreiserfs exceptions implementation
	Copyright (C) 1996 - 2002 Hans Reiser.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiserfs/reiserfs.h>

#define N_(String) (String)
#if ENABLE_NLS
#  include <libintl.h>
#  define _(String) dgettext (PACKAGE, String)
#else
#  define _(String) (String)
#endif

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

static reiserfs_exception_option_t default_handler(reiserfs_exception_t *ex);
static reiserfs_exception_handler_t exception_handler = default_handler;
static reiserfs_exception_t *exception = NULL;

static int fetch_count = 0;

static char *type_strings[] = {
	N_("Information"),
	N_("Warning"),
	N_("Error"),
	N_("Fatal"),
	N_("Bug")
};

static char *option_strings[] = {
	N_("Yes"),
	N_("No"),
	N_("OK"),
	N_("Retry"),
	N_("Ignore"),
	N_("Cancel")
};

char *reiserfs_exception_type_string(reiserfs_exception_type_t type) {
	return type_strings[type - 1];
}

reiserfs_exception_type_t reiserfs_exception_type(reiserfs_exception_t *ex) {
	return ex->type;
}

char *reiserfs_exception_option_string(reiserfs_exception_option_t opt) {
	return option_strings[reiserfs_tools_log2(opt)];
}

reiserfs_exception_option_t reiserfs_exception_option(reiserfs_exception_t *ex) {
	return ex->options;
}

char *reiserfs_exception_message(reiserfs_exception_t *ex) {
	return ex->message;
}

char *reiserfs_exception_hint(reiserfs_exception_t *ex) {
	return ex->hint;
}

static reiserfs_exception_option_t default_handler(reiserfs_exception_t *ex) {
	if (ex->type == EXCEPTION_BUG){
		fprintf (stderr, _("A bug has been detected in libreiserfs. "
	    	"Please email a bug report to umka@namesys.com containing the version (%s) "
	    	"and the following message: "), VERSION);
	} else {
		fprintf (stderr, "%s: %s: ", reiserfs_exception_type_string(ex->type), 
			reiserfs_exception_hint(ex));
	}
	fprintf (stderr, "%s\n", ex->message);

	switch (ex->options) {
	    case EXCEPTION_OK:
	    case EXCEPTION_CANCEL:
	    case EXCEPTION_IGNORE:
	        return ex->options;
	    
	    default:
	        return EXCEPTION_UNHANDLED;
	}
}

void reiserfs_exception_set_handler(reiserfs_exception_handler_t handler) {
	exception_handler = handler ? handler : default_handler;
}

void libreiserfs_exception_catch(void) {
	
	if (!exception)	return;
	
	libreiserfs_free(exception->message);
	libreiserfs_free(exception->hint);
	libreiserfs_free(exception);
	exception = NULL;
}

static reiserfs_exception_option_t do_throw(void) {
	reiserfs_exception_option_t opt;

	if (fetch_count)
		return EXCEPTION_UNHANDLED;
	
	opt = exception_handler(exception);
	reiserfs_exception_catch();
	return opt;
}

reiserfs_exception_option_t reiserfs_exception_throw(reiserfs_exception_type_t type,
	reiserfs_exception_option_t opts, const char *hint, const char *message, ...)
{
	va_list arg_list;

	if (exception)
		libreiserfs_exception_catch();

	if (!(exception = (reiserfs_exception_t *)malloc(sizeof(reiserfs_exception_t))))
		goto no_memory;

	if (!(exception->message = (char*)malloc(4096)))
		goto no_memory;

	if (!(exception->hint = (char*)malloc(4096)))
		goto no_memory;
	
	exception->type = type;
	exception->options = opts;
	strncpy(exception->hint, hint, 4096);

	va_start(arg_list, message);
	vsnprintf(exception->message, 8192, message, arg_list);
	va_end(arg_list);

	return do_throw();

no_memory:
	fprintf(stderr, "Out of memory in exception handler!\n");

	va_start(arg_list, message);
	vprintf(message, arg_list);
	va_end(arg_list);

	return EXCEPTION_UNHANDLED;
}

reiserfs_exception_option_t reiserfs_exception_rethrow(void) {
	return do_throw();
}

void reiserfs_exception_fetch_all(void) {
	fetch_count++;
}

void reiserfs_exception_leave_all(void) {
	if (fetch_count > 0)
		fetch_count--;
}

